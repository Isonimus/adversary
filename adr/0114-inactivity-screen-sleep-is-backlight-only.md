---
id: '0114'
title: "Inactivity screen sleep dims the backlight only and never sleeps the CPU"
type: architecture
status: superseded
date: 2026-10-24
supersedes: []
superseded_by: ['0116']
---

## Context

The device is a battery-powered field tool, and its most valuable sessions are passive: a
wardriving walk, a sniffer capture, a running beacon or deauth attack. Those sessions are driven
entirely from `loop()` in `src/main.cpp`, which every iteration calls `handleInput()` and then
the per-module `update()` path (ScreenManager, GPS, WiFi, the active attack). The screen stays at
full brightness the whole time, which is the single largest avoidable battery draw on a walk, and
there is no auto-dim.

`DisplaySettings::screenTimeoutSec` (default 0 = never) has existed in the settings struct and is
loaded and saved, but nothing reads it to drive a timer: it is a dead stub.

The trap in wiring it up is that the obvious "screen sleep" primitive on ESP32 is
`esp_light_sleep_start()` / `esp_deep_sleep_start()`. Either one halts `loop()`. On this firmware
that does not pause an idle UI, it stops the very session the operator walked away from: no more
beacon frames transmitted, no scan results harvested, no GPS NMEA ingested, no wardriving CSV
rows written. A device left "sleeping" mid-walk would silently collect nothing. So the design
question is not "how do we sleep" but "how do we save power without ever stopping the loop", and
that answer has to be written down because a later power-optimization pass would otherwise reach
for exactly the CPU-sleep call this decision forbids.

## Decision

**1. Screen sleep is backlight-only.** On inactivity the screen dims by setting the backlight to
zero (`SystemManager::setDisplayBrightness(0)`), nothing more. `loop()` keeps running at full
rate, so any active attack, scan, or wardriving session continues unaffected in the dark.
`esp_light_sleep`, `esp_deep_sleep`, and any other CPU, clock, or scheduler gating that would
pause `loop()` are forbidden in the inactivity path. This invariant is the reason the ADR exists;
it binds future work, not just this slice.

**2. The timer and wake hook centrally in `handleInput()`.** That function is where both input
models converge: the Cardputer keyboard (normalized to a routing char) and the M5StickC
`InputAction` (mapped to a routing char). On any detected input the inactivity timer resets. If
the screen is currently dimmed, that first input only wakes it (restore the backlight to the
configured `DisplaySettings::brightness`) and is swallowed: it is not routed to the active screen,
so a wake tap on a blind screen can never also fire an action. The dim decision itself is a pure
predicate, `inactivityShouldDim(now, lastActivity, timeoutSec, alreadyDimmed)`, evaluated once per
`loop()` from the live `screenTimeoutSec`; a value of 0 disables dimming entirely, which is the
default, so the stock experience is unchanged until an operator opts in.

**3. No per-screen opt-out.** A per-screen "do not dim me" flag was considered for the passive
live-status screens (wardriving and sniffer counts) and rejected. The feature is off by default,
the loop keeps the session alive while the screen is dark, and any key wakes it instantly, so a
passive watcher presses one key to read the counts. A per-screen opt-out would add a
screen-interface flag and extra state for no demonstrated need, against the KISS and
no-speculative-generality bar. It is revisited only if a concrete screen proves auto-dim actively
harmful (for example a "scan this QR" screen the operator must hold still and read without
touching keys), and that would be recorded as a decision superseding this clause rather than a
silent exception.

## Consequences

- Long field sessions save battery without stopping. The dimmed state is purely cosmetic; packet
  TX, scanning, GPS ingest, and CSV writes all continue because `loop()` never pauses.
- The dormant `screenTimeoutSec` field becomes live. A `Display > Screen Sleep` settings row edits
  it (0 shown as "Off"); the inactivity path reads the field live each loop, so a change takes
  effect without any additional notification plumbing.
- The wake-swallow means the operator's first input after the screen dims is consumed by the wake,
  matching the phone and laptop lock-wake idiom and preventing an accidental action issued at a
  screen the operator cannot see.
- Fail-loud holds: there is no fallback that can strand the screen dark. `inactivityShouldDim` only
  ever triggers the dim transition, and the input hook unconditionally restores brightness on any
  input, so the sole way to remain dimmed is continued inactivity.
- One central timer, not N per-screen timers, because of the off-by-default plus any-key-wake plus
  no-opt-out combination. The pure predicate is unit-tested on the host; the backlight and input
  wiring are device-only.
