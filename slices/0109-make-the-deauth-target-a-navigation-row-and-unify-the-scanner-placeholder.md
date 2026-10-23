---
id: '0109'
title: "Make the deauth target a navigation row and unify the scanner placeholder"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Let the operator pick a deauth target from inside the deauth screen, and make the empty-target
copy read the same across the forms that route to the scanner.

Today the deauth config can only receive a target from the scanner path (scanner publishes
`ATTACK_TARGET_SELECTED` -> `main.cpp::launchAttackTarget` -> `setParams` -> `setTarget`). Entered
straight from the menu, the screen shows "No target selected" in red and has no way to reach the
scanner: the attack is unusable without first visiting the scanner and selecting there. Evil Twin
(ADR-0092, applied in its config) already solved this: its Target is list row 0, a `ConfigRow`
Navigation row that routes to the scanner on ENTER. Deauth should match.

## Definition of Done

- **Given** the deauth config form entered from the menu with no target
- **When** it is rendered
- **Then** Target is list row 0, a `ConfigRow` whose dim value reads `(select from scanner)` (no
  red, no separate 3-line SSID/BSSID/Channel header, no separator lines), and the Start Attack
  row still reads `(no target)`.

- **Given** the Target row (row 0) is selected
- **When** the operator presses ENTER (or `/` / `>`)
- **Then** the screen routes to the WiFi scanner, with or without an existing target; selecting a
  network there returns via the unchanged `ATTACK_TARGET_SELECTED` -> `setTarget` path and the row
  then shows the target SSID.

- **Given** a target has been selected
- **When** the operator navigates to Start Attack and presses ENTER
- **Then** the attack starts exactly as before (the `hasTarget_` guard and the value rings are
  unchanged).

- **Given** the Evil Twin config form with no target
- **When** its Target row is rendered
- **Then** its placeholder reads `(select from scanner)`, identical to deauth.

- **Given** any deauth state (config, running, results)
- **When** it is rendered
- **Then** the top bar is the shared `ui::StatusBar` (filled bar + battery / SD / WiFi / GPS /
  RFID / cap / heap badges), not a hand-rolled title line, matching Evil Twin and karma.

## Design

Mirror the Evil Twin Target row in deauth, and unify the placeholder phrase.

- **Target becomes list row 0, a Navigation row.** Drop the 3-line hand-drawn Target header block
  (SSID / BSSID / Channel) and both `ACCENT_DARK` separator lines from `drawConfig`; the list
  starts right under the header, like Evil Twin. Row 0 renders through `ConfigRow` with the target
  SSID (truncated at 16 chars) or the shared placeholder as its value.
- **`CONFIG_OPTION_COUNT` 5 -> 6** (Target, Target Type, Packets, Delay, Send Disassoc, Start
  Attack). The row-kind function becomes the Evil Twin three-way: row 0 = Navigation, last row =
  Terminal, the rest = Value. The classifier's `Navigate` arm calls
  `ScreenManager::navigateTo(ScreenId::SCANNER)`. `handleEdit`'s per-row `switch` indices shift by
  +1 (old Target Type 0 -> 1, etc.); row 0 gets no edit arm (a Navigation row has nothing to edit
  in place, ADR-0092).
- **Start Attack keeps its `hasTarget_` guard** on the terminal row; the test-BSSID fallback in
  `startAttack` is left as-is (only reachable if the guard is bypassed; out of scope).
- **Unify the empty-target placeholder to `(select from scanner)`**, dim `TEXT_SECONDARY`. Evil
  Twin's `getSSIDDisplay` returns `"(from scanner)"` today; change it to the same phrase. Record
  the phrase in CLAUDE.md's UI-copy section so future scanner-routed rows reuse it.
- **Port the deauth top bar to the shared `ui::StatusBar`.** deauth hand-rolled its own
  `drawHeader` (bare title + one separator line) since the initial public release (e0225f8); with
  the Target block removed, that divergence from the shared bar became conspicuous next to Evil
  Twin and karma, which already use `StatusBar`. Replace the three `drawHeader` call sites
  (config / running / results) with `ui::StatusBar::render` and delete the method. The broader
  port of the other 12 hand-rolled headers (probe_flood, beacon_spam, et al.) stays a LEDGER item;
  deauth moves now because this slice already reshaped its top.

**Existing code considered.** `classifyConfigKey` (ADR-0107, `config_input.h`) already encodes the
`Navigate`-on-ENTER axis; the Evil Twin Target row (`evil_twin_screen.cpp`) is the exact shape
being copied, including the three-way row-kind function and the scanner route. No new abstraction:
this is wiring deauth onto the shape the other forms already share.

## Verification

- No new unit test: the classifier is already pinned host-side by `test/test_config_input`
  (ADR-0107) and this slice adds no new pure logic, only screen wiring. The deauth `drawConfig` /
  `handleInput` paths are device-only, covered by the compile gate (`pio run -e cardputer` and
  `-e m5stick`, built serially) and on-device review. Full native suite stays green.
- On-device (Cardputer ADV, device in hand): enter deauth from the menu, confirm Target row 0 reads
  `(select from scanner)` dim (no red, no header block); ENTER on it opens the scanner; picking a
  network returns and the row shows the SSID; the value rows and Start Attack behave as before.
  Confirm Evil Twin's empty Target row now reads `(select from scanner)`.

## As built

Shipped as designed. `deauth_screen` gained a Target navigation row at list index 0:
`CONFIG_OPTION_COUNT` 5 -> 6, the three-way row-kind function (0 = Navigation, last = Terminal,
else Value) matching Evil Twin, and a `Navigate` arm that calls
`ScreenManager::navigateTo(ScreenId::SCANNER)` (new `#include "ui/screen_manager.h"`).
`drawConfig` dropped the 3-line SSID/BSSID/Channel header and both `ACCENT_DARK` separators; the
list starts at `HEADER_HEIGHT + 4` and row 0 renders through `ConfigRow` with the SSID (truncated
at 16) or the `(select from scanner)` placeholder. `handleEdit`'s indices shifted +1 with no arm
for row 0. Evil Twin's `getSSIDDisplay` placeholder changed `(from scanner)` -> `(select from
scanner)`; the phrase is recorded in CLAUDE.md's UI-copy section. The deauth top bar moved to the
shared `ui::StatusBar::render` in all three states (config / running / results) and the hand-rolled
`drawHeader` method + its declaration were deleted. On-device review found the Evil Twin Target
value cropped at a 16-char cap (`(select from scanner)` is 21): both forms' Target-row caps were
raised to 26 (an SSID can be 32, and the "Target" label leaves room for ~29 on a 240px row at
FONT_CELL_WIDTH=6), with the value buffers widened to 28, so the placeholder shows in full and real
SSIDs stop over-truncating. Verified: `pio run -e cardputer` and `-e m5stick` both SUCCESS
(serial), `pio test -e native` 902/902, `lint-docs` clean.
