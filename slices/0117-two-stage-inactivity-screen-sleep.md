---
id: '0117'
title: "Two-stage inactivity screen sleep (readable dim, then off)"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Evolve the single-stage inactivity sleep (slice-0115) into the two-stage model decided in
ADR-0116: dim to a readable 15% after one delay, then to full black after a second, longer delay.
Keeps the ADR-0114 backlight-only invariant and the central input hook; refines the wake so the
input is swallowed only when the screen is fully off.

## Definition of Done

- **Given** `Dim After` is Off (default)
- **When** the device sits idle
- **Then** the screen never changes brightness (feature disabled, as before).

- **Given** `Dim After` 30 s and `Screen Off` 1 m
- **When** the device is idle
- **Then** at 30 s the backlight drops to 15% (screen still legible, a wardriving session keeps
  counting), and at 1 m it goes to 0, with `loop()` running throughout.

- **Given** the screen is at the 15% dim stage
- **When** the operator presses any key
- **Then** the backlight restores to full and the key also routes to the active screen (the
  operator could see it).

- **Given** the screen is fully off
- **When** the operator presses any key
- **Then** the backlight restores to full and that key is swallowed (no accidental action on a
  blind screen).

- **Given** `Screen Off` is set below `Dim After`
- **When** settings are saved
- **Then** `Screen Off` is clamped up to `Dim After` so the readable stage is never skipped.

## Design

Per ADR-0116. The stage is a pure function `inactivityStage(now, lastActivity, dimAfterSec,
offAfterSec) -> {ACTIVE, DIMMED, OFF}` in `src/modules/system/inactivity.h` (dim-after 0 = off,
off-after 0 = dim-only, unsigned subtraction for millis() wraparound), host-tested in
`test_inactivity`. `SystemManager` tracks the applied `currentStage_`: `updateScreenSleep()`
applies the stage brightness on each change (full / 15% / 0), and `noteActivityAndWake()` restores
full brightness on any input and returns true (swallow) only when the previous stage was OFF.

Settings: the single `screenTimeoutSec` field is replaced by `screenDimAfterSec` /
`screenOffAfterSec`, surfaced as `Display > Dim After` and `Screen Off` rows (0 = Off, label
Off / Ns / Nm), with the off-clamp applied on save.

## Verification

- `pio test -e native -f test_inactivity`: 5/5 (disabled, three-stage progression, dim-only,
  off-below-dim degenerate, wraparound).
- `pio test -e native -f test_settings_manager`: 11/11 (the replaced display fields serialize).
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (serial; -j 2 avoids the
  host OOM at default parallelism).
- On-device: the single-stage predecessor (slice-0115) was operator-confirmed working on the
  Cardputer ADV. The two-stage dim/off + the stage-aware wake still need an on-device pass; a
  ledger verify line tracks it across the three input models.

## As built

As designed. Files: `adr/0116-...` (supersedes 0114, which is marked superseded),
`src/modules/system/inactivity.h` (stage enum + pure predicate + dim-level constant),
`src/modules/system/system_manager.{h,cpp}` (stage state + two-stage apply + stage-aware wake),
`src/modules/storage/settings_manager.h` (two fields + JSON), `src/ui/screens/settings_screen.{h,cpp}`
(two rows + off-clamp + label), `test/test_inactivity/test_inactivity.cpp` (rewritten).
