---
id: '0112'
title: "Per-run radio-mode quick-config on the wardriving idle screen"
type: slice
status: accepted
date: 2026-10-24
supersedes: []
superseded_by: []
---

## Goal

Put the wardriving radio mode (WiFi / BLE / Both) within one keypress of the operator, on the
wardriving idle (pre-Start) screen, instead of two levels deep in global Settings. Mode is the
most run-contextual wardriving knob: it is chosen from the RF environment in the moment right
before a walk, yet reaching it today means leaving the wardriving screen, scrolling the Settings
list to the Wardriving section, setting it, backing out, and re-entering.

This is the first application of ADR-0111: the selector is an inline quick-config that reads and
writes the canonical `WirelessSettings::wardrivingRadioMode` field directly through
`SettingsManager`, with no screen-local copy. Settings stays the full editor of the same field;
the manager already reads that field live at `startSession` (ADR-0060), so setting it on the
pre-start idle screen is the natural place.

Scope is radio mode only. Scan interval is a candidate per the same ADR-0111 predicate, but it is
a 1000-10000 ms range (a different interaction from a three-way cycle) and would crowd the idle
screen, so it stays Settings-only for now and is left as a ledger follow-up if demand appears.

## Definition of Done

- **Given** the wardriving idle screen
- **When** it is rendered
- **Then** a `Radio: <WiFi|BLE|Both>` line shows the current mode, and the footer offers a
  `Radio` hint alongside `Start` and `Back`.

- **Given** the idle screen with the mode at WiFi
- **When** the operator presses `m` (or focuses the `Radio` footer hint and selects it, the
  M5Stick path)
- **Then** the mode advances WiFi -> BLE -> Both -> WiFi, the line updates, and the new value is
  written to `wireless.wardrivingRadioMode` and persisted via `SettingsManager::save()`.

- **Given** the mode was changed on the idle screen
- **When** the operator opens global Settings
- **Then** the Wardriving "Radio Mode" row shows the same value (one field, two affordances), and
  a session started from the idle screen brings up the radios for that mode.

- **Given** a corrupt stored value outside [0, RADIO_MODE_COUNT)
- **When** the idle screen reads it, or the operator cycles from it
- **Then** it resolves to WiFi rather than rendering out of bounds or advancing from garbage.

## Design

The cycle/clamp is a pure function `nextRadioMode(RadioMode)` in `wardriving_types.h` next to the
`RadioMode` enum it operates on, keyed off a `RADIO_MODE_COUNT` constant so the cardinality is not
hard-coded. It wraps after the last mode and maps any out-of-range input to `WIFI_ONLY`, so the
selector always recovers to a valid state. This is the only non-trivial logic and it is pure, so
it is native-tested in `test_wardriving`; the rendering and key wiring are device-only.

The screen binds three thin helpers to the canonical field (no cached copy): `currentRadioMode()`
reads and clamps, `cycleRadioMode()` advances via `nextRadioMode` and persists, and the static
`radioModeLabel()` maps the mode to the same three words the Settings editor shows. `save()` is
called on each cycle (a deliberate per-keypress action, not a hold-to-increment), so the value is
durable across a battery pull exactly as a Settings edit would be; this follows the existing UI
convention of a fire-and-forget `save()` (settings_screen).

## Verification

- `pio test -e native -f test_wardriving`: 14/14, including
  `test_next_radio_mode_cycles_and_recovers` (cycle, wrap, and corrupt-value recovery; its
  recovery assertion fails under a naive `(raw+1) % 3`).
- `pio run -e cardputer` and `pio run -e m5stick`: both SUCCESS (built serially).

## As built

As designed. Files: `src/modules/wardriving/wardriving_types.h` (pure helper + count),
`src/ui/screens/wardriving_screen.h` / `.cpp` (idle selector, footer hint, three field-bound
helpers), `test/test_wardriving/test_wardriving_manager.cpp` (regression test).
