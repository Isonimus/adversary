---
id: '0110'
title: "Migrate Handshake Manual config to the shared list, rows, and classifier"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Bring the Handshake Manual config form onto the same three shared pieces every other attack form
now uses: the `ListScrollModel` + `ScrollableList` selection/scroll, the `ConfigRow` chrome, and
the `classifyConfigKey` input classifier (ADR-0092 / 0096 / 0107). Manual is the last uniform
config form still hand-rolling its own selection int, its own selection band, and its own ENTER
`switch`.

Doing so lets the Target row drop its hand-drawn `fillRect` band and its WARNING-red empty state
for the shared dim `ConfigRow` look, and lets the config screen drop the conditional `CH:BSSID`
detail line. That identity line is not deleted outright: it moves to the **top of the Manual
CAPTURING screen**, where the operator actually needs to confirm which AP is being attacked, so no
information is lost.

Auto Hunt (ADR-0094's peer screen, same class) has no Target and a single START row; it is folded
onto the same `configList_` + classifier path with its one row classified as a Terminal, so the two
modes stop diverging in the input handler.

## Definition of Done

- **Given** the Handshake Manual config form entered from the menu with no target
- **When** it is rendered
- **Then** Target is `ScrollableList` row 0, a `ConfigRow` whose dim value reads
  `(select from scanner)` (no WARNING red, no hand-drawn band, no `CH:BSSID` detail line), followed
  by the Auto Deauth / Deauth Count / Timeout value rows and a terminal START CAPTURE button.

- **Given** the Target row (row 0) is selected
- **When** the operator presses ENTER (or `/` / `>`)
- **Then** the screen routes to the WiFi scanner; selecting a network there returns via the
  unchanged `ATTACK_TARGET_SELECTED` -> `setTarget` path and the row then shows the target SSID
  (capped at 26 chars, matching deauth and evil_twin).

- **Given** a target has been selected and Manual capture is running
- **When** the CAPTURING screen is rendered
- **Then** its top shows the target SSID on one line and `CH:<n>  <BSSID>` on the next, above the
  progress ring and stats, and the ring/stats remain fully on screen (clear of the footer).

- **Given** the Manual value rows (Auto Deauth / Deauth Count / Timeout)
- **When** the operator presses `,` / `<` / `/` / `>` or ENTER on one
- **Then** the value steps exactly as before (toggle, 1..20 ring, 30/60/120/180 ring), and `;` / `.`
  navigate the list with wrap.

- **Given** the Auto Hunt config form
- **When** the operator presses ENTER
- **Then** the hunt starts exactly as before; `;` / `.` and the edit keys are inert (its single
  START row is classified Terminal, and there is no value row for them to act on).

## Design

Mirror the evil_twin Manual-form shape (`evil_twin_screen.cpp`), which is the exact pattern this
slice copies, and move the identity line to the capture view.

- **Selection model.** Replace the plain `int configSelection_` with the shared `ListScrollModel
  configList_` (already `#include`d via the header). Both modes use it: `configItemCount()` stays
  `isAutoHunt() ? 1 : CONFIG_ITEMS` (5), and `show()` resets it to the top like evil_twin.
- **Input via the classifier.** The CONFIG case computes the selected row's kind, then switches on
  `classifyConfigKey(key, kind)`:
  - Auto Hunt: the single row is a **Terminal** (START). ENTER -> Activate -> `startCapture()`;
    nav keys move within a 1-item list (no-op); edit keys resolve to `handleConfigEdit`, which has
    no case for row 0, so they stay inert exactly as today.
  - Manual: row 0 = **Navigation** (Target -> scanner), last row = **Terminal** (START CAPTURE),
    the rest = **Value**. EditPrev/EditNext call `handleConfigEdit(-+1)`; Activate starts capture
    under the `hasTarget_` guard; Navigate routes to `ScreenId::SCANNER`.
  - Back (`` ` ``) and any key the classifier does not own fall through as before.
  `handleConfigEdit` switches on `configList_.selectedIndex()` instead of `configSelection_`; its
  per-row cases are unchanged (1 = Auto Deauth, 2 = Deauth Count, 3 = Timeout).
- **Manual render via `ScrollableList`.** `drawConfig`'s Manual branch sets
  `configList_.setItemCount(CONFIG_ITEMS)` and renders through `ScrollableList::render` with a
  per-row callback: the last index is the shared `ActionButton` ("START CAPTURE"); row 0 is a
  `ConfigRow` whose value is the target SSID (26-char cap, 28-byte buffer) or the shared
  `(select from scanner)` placeholder; rows 1-3 are `ConfigRow`s with the existing value strings.
  The hand-drawn selection `fillRect`, the ACCENT/WARNING target colouring, and the `CH:BSSID`
  detail line are deleted. Auto Hunt's branch is unchanged except for reading selection from
  `configList_`.
- **Target identity moves to CAPTURING.** `drawCapturing` (Manual only; Auto Hunt uses
  `drawAutoHuntCapturing`) gains a two-line header at the top: the SSID, then `CH:<n>  <BSSID>`
  (via `utils::formatMacBytes`). To keep the progress ring and the three stat lines on screen under
  the added header, the ring is centred in the space *below* the header and its radius drops 20 ->
  16; the stat lines are anchored just above the footer. Exact pixel spacing is confirmed on
  device.

**Existing code considered.** `classifyConfigKey` (ADR-0107) and `ConfigRow` (ADR-0096) already
encode the model; evil_twin is the already-shipped twin of this exact form. No new abstraction is
introduced: this removes handshake's three hand-rolled copies (selection int, selection band, ENTER
`switch`) in favour of the shared pieces. ADR-0094 (one class, two modes) is unchanged; the mode
still only selects the layout and the row-kind branch.

Not in scope: Auto Hunt's capture view, the handshake list view, and the save/success flow are
untouched. Handshake already renders the shared `ui::StatusBar` (via `drawHeader`), so no header
port is part of this slice.

## Verification

- No new unit test: the classifier is already pinned host-side by `test/test_config_input`
  (ADR-0107) and this slice adds no new pure logic, only screen wiring. The `drawConfig` /
  `drawCapturing` / `handleInput` paths are device-only, covered by the compile gate (`pio run -e
  cardputer` and `-e m5stick`, built serially) and on-device review. Full native suite stays green.
- On-device (Cardputer ADV, device in hand): enter Handshake Manual from the menu, confirm Target
  row 0 reads `(select from scanner)` dim (no red, no band, no CH/BSSID line); ENTER on it opens the
  scanner; picking a network returns and the row shows the SSID; the value rows step as before and
  `;`/`.` wrap; START CAPTURE begins capture. On the CAPTURING screen, confirm the SSID and
  `CH:<n>  <BSSID>` show at the top and the ring + stats are fully visible above the footer. Confirm
  Auto Hunt still starts on ENTER and behaves as before.

## As built

Shipped as designed. `HandshakeScreen` replaced the plain `int configSelection_` with the shared
`ListScrollModel configList_`; `show()` resets it to the top. The CONFIG input handler now routes
both modes through `classifyConfigKey` (ADR-0107): Auto Hunt's single row is classified `Terminal`
(ENTER -> `startCapture`, nav/edit keys inert because `handleConfigEdit` has no case for row 0),
Manual is Target (0) `Navigation` / value rows (1-3) / START (last) `Terminal`; the Navigate arm
calls `ScreenManager::navigateTo(ScreenId::SCANNER)`, Activate guards on `hasTarget_`, and back
(`` ` ``) plus unhandled keys fall through. `handleConfigEdit` switches on
`configList_.selectedIndex()`. Manual `drawConfig` renders on `ScrollableList` with a per-row
callback (last row = `ActionButton` "START CAPTURE", row 0 = `ConfigRow` with the SSID or the
`(select from scanner)` placeholder capped at 26, rows 1-3 = `ConfigRow` value rows); the hand-drawn
selection band, the ACCENT/WARNING target colouring, and the `CH:BSSID` detail line are gone. Auto
Hunt's branch is unchanged except its ActionButton is always focused. `drawCapturing` (Manual)
gained a two-line target header (SSID, then `CH:<n>  <BSSID>` via `utils::formatMacBytes`); the
progress ring shrank 20 -> 16 and centres below the header, and the three stat lines anchor above
the footer, so everything stays on the 135px screen. Added `#include
"ui/components/config_input.h"`.

On-device (Cardputer ADV): confirmed the Target placeholder, dim rows, wrap navigation, scanner
round-trip, value stepping, and START; the CAPTURING header shows SSID + CH/BSSID with the ring and
all three stat lines fully visible above the footer; Auto Hunt starts unchanged. Verified: `pio run
-e cardputer` and `-e m5stick` both SUCCESS (serial), `pio test -e native` 902/902, `lint-docs`
clean. Corrected a stale LEDGER note in passing: handshake was never a hand-rolled header (its
`drawHeader` already wraps `ui::StatusBar::render`), so the header-unification backlog dropped from
13 screens to 11.
