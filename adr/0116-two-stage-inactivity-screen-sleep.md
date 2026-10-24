---
id: '0116'
title: "Inactivity screen sleep dims in two stages (readable dim, then off)"
type: architecture
status: accepted
date: 2026-10-24
supersedes: ['0114']
superseded_by: []
---

## Context

ADR-0114 shipped inactivity screen sleep as a single transition: after one timeout the
backlight went straight to 0, and any input woke it and was swallowed. On-device it worked
exactly as intended, and the backlight-only invariant (never sleep the CPU, so an active
session keeps running) held. Using it surfaced two things the one-shot model handled poorly.

First, the readable-screen case. ADR-0114 clause 3 declined a per-screen opt-out for the
passive live-status views (wardriving and sniffer counts) on the grounds that the operator can
tap to wake. But a one-shot jump to black means the operator who just wants to glance at the
running count has to wake the screen (and, with the old swallow rule, waste a tap) every time.
There was no middle state where the screen is cheap to run yet still legible.

Second, OPSEC. A red-team device left with a visibly lit screen during a covert engagement is a
tell, so "eventually fully dark" is a feature, not just battery saving. A single knob could give
either "dim but always visible" or "goes black quickly", not both.

A readable intermediate brightness resolves both at once: dim to a low-but-legible level first
(most of the backlight draw saved, counts still glanceable), then to full black after longer
idle (OPSEC, maximum saving). That is a change to the dim curve ADR-0114 fixed as a single step,
and it also refines the wake rule, so it supersedes ADR-0114 rather than amending it.

## Decision

Inactivity screen sleep has **two stages**, driven by two configurable delays on the canonical
`DisplaySettings` fields:

- `screenDimAfterSec` is the master switch and the stage-1 delay. 0 disables the feature
  entirely (the default, so the stock experience is unchanged). When set, after this long idle
  the backlight drops to a fixed readable level, `SCREEN_DIM_BRIGHTNESS_PCT` (15%), chosen in
  the 10-20% band: low enough to save most of the draw, high enough to read live counts without
  waking.
- `screenOffAfterSec` is the stage-2 delay. 0 means dim-only (never fully off); when nonzero it
  is kept `>= screenDimAfterSec` (clamped at the settings boundary) so the readable stage is
  never skipped, and after this long idle the backlight goes to 0.

The stage is a pure function of elapsed idle time, `inactivityStage(now, lastActivity,
dimAfterSec, offAfterSec) -> {ACTIVE, DIMMED, OFF}`, evaluated once per loop. The two carried
invariants from ADR-0114 are unchanged: **backlight only, never `esp_light_sleep` /
`esp_deep_sleep` or any CPU gating that pauses `loop()`** (the dimmed and off states are cosmetic;
the session keeps running), and the **timer and wake hook centrally in `handleInput()`** where the
Cardputer and M5Stick input models converge, reading the delays live so a Settings change needs
no extra wiring.

The **wake rule is refined**: any input restores full brightness, but the triggering input is
swallowed **only when the screen was fully OFF** (blind). At the readable DIMMED stage the
operator can see what they are pressing, so that input wakes the screen and still routes to the
active screen. ADR-0114 swallowed every wake input; that was correct only because every dimmed
state there was black.

Clause 3 of ADR-0114 (no per-screen opt-out) still holds and for the same reasons; the readable
DIMMED stage reduces the pressure for one, since the passive-watch screens stay legible through
stage 1.

## Consequences

- The passive live-status screens keep their information through stage 1 while still saving most
  of the backlight power, which is the concern ADR-0114 could only wave off.
- OPSEC: the screen still reaches full black after longer idle, so a readable dim does not trade
  away invisibility.
- Settings grows from one row to two (`Dim After`, `Screen Off`), each 0 = Off. The off delay is
  clamped `>= ` the dim delay on save so a stored config can never skip the readable stage; the
  pure policy is also safe if a hand-edited config violates that (OFF past its threshold wins).
- The replaced `screenTimeoutSec` field and its one-shot predicate are gone; the JSON gains
  `screenDimAfterSec` / `screenOffAfterSec`. An old settings file without the new keys reads back
  as the disabled default, which is acceptable for alpha.
- Fail-loud still holds: the pure stage function has no fallback that strands the screen, and the
  input hook unconditionally restores brightness on any input, so continued inactivity is the
  only way to stay dim or off.
- The pure stage predicate is host-tested; the backlight and input wiring remain device-only.
