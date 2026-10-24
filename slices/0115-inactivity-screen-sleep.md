---
id: '0115'
title: "Inactivity screen sleep (backlight dim with any-key wake)"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Save battery on long field sessions by dimming the backlight after a configurable idle timeout,
restoring it on any input, without ever stopping the main loop. Implements ADR-0114. Wires up the
dormant `DisplaySettings::screenTimeoutSec` field (default 0 = Off), which until now was loaded and
saved but drove nothing.

## Definition of Done

- **Given** `screenTimeoutSec` is 0 (default)
- **When** the device sits idle for any length of time
- **Then** the screen never dims (behaviour unchanged from before).

- **Given** `screenTimeoutSec` is set (for example 30 s) via Display > Screen Sleep
- **When** no input arrives for that long
- **Then** the backlight goes to 0, and `loop()` keeps running so an active attack / scan /
  wardriving session continues collecting in the dark.

- **Given** the screen is dimmed for inactivity
- **When** the operator presses any key (Cardputer) or triggers any action (M5Stick)
- **Then** the backlight restores to the configured brightness and that first input is swallowed
  (the active screen does not act on it), so a wake tap never fires an accidental action.

- **Given** the Display settings section
- **When** the Screen Sleep row is shown
- **Then** 0 reads as "Off", whole minutes as "Nm", otherwise "Ns", adjustable 0..300 s in 30 s
  steps.

## Design

Per ADR-0114, screen sleep is backlight-only: never `esp_light_sleep` / `esp_deep_sleep`, which
would halt `loop()` and silently stop the session the operator left running.

The dim decision is the pure predicate `inactivityShouldDim(now, lastActivity, timeoutSec,
alreadyDimmed)` in `src/modules/system/inactivity.h`, dependency-free so it is host-tested
(`test_inactivity`): true only on the transition into idle, 0 = never, no re-dim when already
dark, unsigned subtraction for millis() wraparound safety.

`SystemManager` owns the state and the two glue methods (device-only, next to the existing
brightness primitive): `noteActivityAndWake()` stamps the activity time and, if dimmed, restores
brightness and returns true so the caller swallows the input; `updateScreenSleep()` reads
`screenTimeoutSec` live each loop and applies the dim transition. Both the Cardputer keyboard path
and the M5Stick `InputAction` path call `noteActivityAndWake()` from the one central
`handleInput()` in `main.cpp`, where both input models converge, and `loop()` calls
`updateScreenSleep()` once per iteration. Reading the timeout live means a Settings change needs no
extra notification wiring.

No per-screen opt-out (ADR-0114 clause 3): off by default plus any-key wake make it unnecessary.

## Verification

- `pio test -e native -f test_inactivity`: 5/5 (disabled-never-dims, before-timeout, at/after
  timeout, no-redim-when-dimmed, wraparound). The wraparound and no-redim cases fail a naive
  `now - last >= timeout*1000` written with signed math or without the dimmed guard.
- `pio run -e cardputer` and `pio run -e m5stick`: both SUCCESS (built serially).
- On-device checks (not yet run, hardware-gated): with Screen Sleep set, the backlight dims after
  the timeout while a wardriving session keeps counting; any key restores it without acting; the
  Settings row reads Off / Ns / Nm.

## As built

As designed. Files: `adr/0114-...` (decision), `src/modules/system/inactivity.h` (pure predicate),
`src/modules/system/system_manager.{h,cpp}` (state + glue), `src/main.cpp` (central input hook +
per-loop dim), `src/ui/screens/settings_screen.{h,cpp}` (Screen Sleep row + label),
`test/test_inactivity/test_inactivity.cpp` (regression).

Hardware verification of the dim/wake behaviour across the three input models (Cardputer matrix,
Cardputer ADV TCA8418, M5StickC buttons) remains a ledger follow-up.
