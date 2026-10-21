---
id: '0093'
title: "Converge config-form keyboard input on the canonical model"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Make every attack/config form answer the keyboard the same way, per the model fixed in
ADR-0092 (the `ble_spam` superset). Today four dialects coexist (measured 2026-10-20,
post slice-0091): arrow-edit forms (`karma`, `evil_twin`, `ble_bad_ble`) leave ENTER inert
on value rows; ENTER-cycle forms (`beacon_spam`, `probe_flood`, `deauth`) have no
left/right edit; `ble_spam` already has both; `handshake` routes its Target row to the
scanner. This split made the operator press ENTER on a migrated form, see nothing happen,
and read it as broken (2026-10-20). This slice closes the LEDGER `[feature]` item "Converge
the config-form keyboard interaction model", including its fold-in: `evil_twin`'s Target
routes to the WiFi scanner on ENTER like `handshake`, as a selectable list row.

## Definition of Done

- **Given** the karma, evil_twin, or ble_bad_ble config form with a value row selected
- **When** the operator presses ENTER
- **Then** that field advances to its next value (identically to pressing `/`), and ENTER
  still activates the terminal START row when that row is selected

- **Given** the beacon_spam, probe_flood, or deauth config form with a value row selected
- **When** the operator presses `,`/`<` or `/`/`>`
- **Then** the field steps to its previous value on `,`/`<` and its next value on `/`/`>`,
  where previous is the reverse of the ring ENTER/`>` advances through

- **Given** the evil_twin config form
- **When** the operator navigates to the Target row and presses ENTER
- **Then** the WiFi scanner opens so a target can be picked, and the chosen network returns
  as the evil_twin target (the existing Scanner -> EvilTwin publish path)

- **Given** the settings screen or the handshake screen
- **When** the operator uses the keyboard
- **Then** their behaviour is unchanged (both are recorded exceptions in ADR-0092)

- **Given** the beacon_spam, probe_flood, or deauth config footer
- **When** the operator reads the ENTER hint
- **Then** it says "Change" (not "Toggle", which was false for the multi-value rings), while
  evil_twin and karma's footer shows a fixed "Mode" label with no appended live value and no
  longer overflows, and evil_twin no longer advertises a left/right "Edit" hint

## Design

Implements ADR-0092; read it for the model. The forms already share `ScrollableList` +
`ListScrollModel` (slice-0091) and `ActionButton` for the terminal row (slice-0089), so
this slice touches only the per-screen `handleInput` dispatch, not rendering or selection.

**What was considered for reuse.** `ble_spam_screen.cpp` is the reference: its ENTER case
is `if (selectedIndex() == COUNT-1) startAttack(); else return handleInput('/');`, and its
`,`/`/` cases carry the per-field prev/next logic. That three-line ENTER fallback is copied
verbatim into `karma`, `evil_twin`, `ble_bad_ble` (each already has the `,`/`/` edit cases
it delegates to). It is **not** extracted into a shared helper yet: the per-field edit
logic still lives in each screen's switch, so a single shared dispatcher would need those
switches hoisted behind a common interface first. That is the deferred `ConfigRow`/
input-dispatch follow-on (LEDGER); doing it here would be speculative generality ahead of
the third concrete caller. This slice converges *behaviour*, the follow-on converges
*code*. ADR-0092 is the spec that follow-on will implement.

**beacon_spam / probe_flood / deauth** have no `,`/`/` cases today; their fields cycle
*forward-only* inside the ENTER handler (`handleSelect`, e.g. deauth packetCount
0->50->100->500->1000->0). Convergence adds `,`/`<` (previous) and `/`/`>` (next) cases.
`>`/ENTER reuse the existing forward cycle; `<` needs the **backward** step written per
field (the ring run in reverse). This is the only non-mechanical code: toggles flip either
way, but each forward-only enum ring gets an explicit predecessor. No magic values: the
step tables already name their thresholds; the backward step mirrors the same table.

**evil_twin Target -> scanner** (operator-approved reshape, 2026-10-20). The Target moves
from a non-selectable header line into the config list as row 0, making
`CONFIG_OPTION_COUNT` 7 -> 8 and shifting the six existing option indices +1 (the terminal
START row stays last). The new row 0 is a **navigation row**: ENTER on it calls
`ScreenManager::getInstance().navigateTo(ScreenId::SCANNER)`, mirroring
`handshake_screen.cpp:220-227`; `,`/`/` on it are inert (nothing to edit in place). The
return path already exists: `scanner_screen` publishes `ATTACK_TARGET_SELECTED` and
`main.cpp launchAttackTarget` routes it back to `EvilTwinScreen::setTarget`. The old header
render block is deleted; the Target becomes a list row that shows the current target SSID
(or "(from scanner)") as its value.

**Footer copy** is tidied in the same pass, since the new left/right edit made the existing
hints inconsistent. The config-form ENTER verb is unified to "Change" on the three screens
that carry a separate `s`=Start key (beacon/probe/deauth), where ENTER only ever changes the
selected option, so "Toggle" was wrong for the multi-value rings; ble_spam and evil_twin keep
their activation verb ("Go"/conditional "Start") because ENTER there also starts the attack
(no separate start key) and changing it would lose that meaning. evil_twin's dead `/`="Edit"
hint is dropped (it was never shown in CONFIG and left/right stays unadvertised everywhere, by
operator decision). evil_twin and karma's `"Mode - <value>"` footer label becomes a fixed
`"Mode"`: the live mode already renders on the Mode row, and appending it overflowed the
footer. The full cross-screen footer sweep (literal `;`/`.` key glyphs, missing/duplicated
hints, per-state verbs) is out of scope and logged as an `[audit]` ledger item.

Native build excludes `src/ui/` (`platformio.ini` `build_src_filter` `-<ui/>`), so these
screen `.cpp` changes compile only under the device envs; the real compile gate is
`pio run -e cardputer` / `-e m5stick`.

## Verification

No native unit test covers screen `handleInput` (UI is excluded from the native env and has
no host harness), so this slice is verified by device build + on-device operation, the same
bar slice-0091 used:

- `pio run -e native` stays green (858/858; unaffected, proves nothing regressed in the
  shared models the forms sit on).
- `pio run -e cardputer` and `pio run -e m5stick` compile clean, built serially (never
  concurrently, per CLAUDE.md), and pass the ADR-0083 warnings gate
  (`node scripts/lint-build-warnings.mjs <build.log>`).
- `node scripts/lint-docs.mjs` clean (this slice + ADR-0092 indexed).
- **On-device** (operator): on each of the six forms, ENTER advances the selected field and
  activates START on the last row; on beacon/probe/deauth `,`/`/` step the field back/
  forward; on evil_twin, ENTER on the Target row opens the scanner and the picked network
  comes back as the target; settings and handshake behave as before.

## As built

All six config forms now answer the keyboard identically per ADR-0092; ble_spam was the
reference and was left untouched. Operator confirmed every scenario on-device 2026-10-21.

- **karma, ble_bad_ble, evil_twin** gained the ENTER-advances fallback: ENTER on a value
  row now advances the field (`return handleInput('/')`), and still activates the terminal
  START row. ble_bad_ble also started accepting `\r` alongside `\n` for ENTER.
- **beacon_spam, probe_flood, deauth** gained `,`/`<` (previous) and `/`/`>` (next). Each
  got one `handleEdit(int delta)` as the single source of per-field stepping; the forward
  branch is the pre-existing cycle verbatim and the backward branch is its exact mirror,
  and ENTER's value-row path now routes through `handleEdit(+1)` so ENTER and `/` are
  identical. The forward-only rings (deauth packet-count/delay, beacon/probe interval,
  channels, modes) step both directions.
- **evil_twin Target → scanner** (operator-approved reshape): Target moved from a
  non-selectable header into the config list as selectable row 0, so `CONFIG_OPTION_COUNT`
  went 7 → 8 and the six option indices shifted +1. ENTER on the Target row routes to the
  WiFi scanner (`ScreenManager::navigateTo(ScreenId::SCANNER)`); the picked network returns
  as the target over the existing `ATTACK_TARGET_SELECTED` → `setTarget` path, with no
  scanner or main.cpp change. `,`/`/` on the Target row are inert (nothing to edit in
  place). The default selection on entry is now the Target row, which reads as the natural
  first step.
- **settings and handshake** were left unchanged, as the recorded ADR-0092 exceptions.
- **Footer copy** was tidied in the same pass: the config ENTER verb unified to "Change" on
  beacon/probe/deauth (they carry a separate `s`=Start, so ENTER only changes the option and
  "Toggle" was wrong for the rings); ble_spam/evil_twin kept their activation verb because
  ENTER also starts there; evil_twin's dead `/`="Edit" hint was removed; and evil_twin +
  karma's `"Mode - <value>"` label became a fixed `"Mode"` (the value already shows on the
  row and was overflowing the footer). The cross-screen footer sweep was logged as an
  `[audit]` ledger item, not done here.

Verification ran green: `pio test -e native` 858/858; `pio run -e cardputer` and
`-e m5stick` both compiled clean with the ADR-0083 warnings gate clean on each;
`node scripts/lint-docs.mjs` clean. On-device the operator confirmed ENTER advances fields
and starts on the terminal row, `,`/`/` step both directions, evil_twin's Target opens the
scanner and the picked network returns as the target, the footer reads "Change"/"Mode"
without overflow, and settings/handshake are unchanged.
