---
id: '0106'
title: "Route the attack-config forms through ConfigRow"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Finish the config-row convergence. slice-0097 built the chrome-only `ConfigRow` (ADR-0096) and
routed the four *setup* forms (evil_twin, karma, ble_bad_ble, handshake Manual) through it, but
the four *attack-config* forms it did not reach still hand-roll the divergent idiom that
slice-0097 removed everywhere else: the whole selected row reddens to `ACCENT` and the value is
drawn non-dim in a fixed column (beacon_spam / probe_flood at x=100, ble_spam at x=80, deauth
right-aligned at margin 4). These forms got `ScrollableList` from slice-0091 but never the
content layer. Route their uniform `label ... value` rows through `ui::ConfigRow::render` so the
house look (label x=8, value right-aligned 8px margin, dim `TEXT_SECONDARY`, selection by band
only, no per-row recolour) is the one look across every config form. No new decision: this
extends ADR-0096 to the forms slice-0097 left, so it cites that ADR rather than adding one.

## Definition of Done

- **Given** the beacon_spam, probe_flood, ble_spam, and deauth config forms
- **When** each is rendered
- **Then** every uniform value row draws its label at x=8 and its value right-aligned with an
  8px margin in dim `TEXT_SECONDARY`, via `ConfigRow::render`, with no fixed-column value and no
  whole-row `ACCENT` recolour.

- **Given** a config row drawn by `ConfigRow`
- **When** its row is the selected one
- **Then** the selection is shown only by the `ScrollableList` band (dimmed to `BG_SECONDARY`
  when the footer holds focus), and the row's label and value colours are unchanged by selection.

- **Given** the terminal START row of beacon_spam / probe_flood / ble_spam
- **When** it is rendered
- **Then** it still renders through the shared `ActionButton` unchanged (only the config value
  rows move to `ConfigRow`).

## Design

Each form's `ScrollableList` row callback drops its local `setTextColor(selected ? ACCENT :
TEXT_PRIMARY)` + label/value cursor pair and calls `ui::ConfigRow::render(c, rowY, label,
value)`, keeping its value-string formatting (the call-site formatter, as ADR-0096 fixes: a row
component earned by shape, not value type). The terminal ActionButton branch in beacon_spam /
probe_flood / ble_spam is untouched. deauth has no separate ActionButton row: all five of its
rows are label/value today (including `Start Attack` whose value is `>` / `(no target)`), so all
five route through `ConfigRow`, keeping deauth internally uniform; the deauth start-affordance
redesign (the separate LEDGER "Start affordances that don't fit ActionButton" item) is a future
change this does not touch or resolve. Now-unused `valueX` locals are removed; `probe_flood`
keeps `labelX` (its Targeted-mode target line still uses it).

**Existing code considered.** `ui::ConfigRow` (slice-0097, `config_row.h`) is the exact shape
these rows are, so it is reused wholesale; the value strings stay at the call site. Deliberately
**not** migrated: `settings` (data-driven `SettingType` union, ADR-0096's counter-example),
`ble_apple_attack` (single-column label-only picker, no value), the `rfid` card-detail rows
(read-only, semantic per-row colour), and `modules` (status list) are different shapes that
`ConfigRow` does not fit.

## Verification

- No new unit test: `ConfigRow`'s one pure helper `configRowValueX` is already covered host-side
  by `test/test_config_row/test_config_row.cpp` (slice-0097); `ConfigRow::render` is a device-only
  canvas template, covered by the compile gate (`pio run -e cardputer` and `-e m5stick`, built
  serially) and on-device review, exactly as slice-0097 verified the first four forms. Full
  native suite stays green.
- On-device (Cardputer ADV, device in hand): confirm beacon_spam, probe_flood, ble_spam, and
  deauth each render label-left / value-right in the house look, value dim, selection band-only
  (no red active row), the START rows unchanged, and the deauth Target line unchanged.

## As built

Shipped as designed. `beacon_spam`, `probe_flood`, `ble_spam`, and `deauth` route their config
value rows through `ui::ConfigRow::render`, dropping the whole-row `ACCENT`-on-select recolour
and the fixed value columns (beacon/probe at x=100, ble_spam at x=80) and deauth's hand-computed
right-align. Values now draw dim `TEXT_SECONDARY` right-aligned at the 8px house margin, with
selection shown by the `ScrollableList` band alone. deauth routes all five rows including
`Start Attack` (value `>` / `(no target)`); the terminal START `ActionButton` rows on the other
three are untouched; `probe_flood` kept its `labelX` local for the Targeted-mode target line. The
now-unused `valueX` locals were removed.

- **DoD 1 (house look via ConfigRow):** met across all four forms.
- **DoD 2 (band-only selection):** met — the row colours no longer change on select.
- **DoD 3 (START rows unchanged):** met — beacon/probe/ble_spam keep the `ActionButton`.
- **On-device:** verified on the Cardputer ADV 2026-10-23 — all four forms read label-left /
  value-right dim, band-only selection, the START rows and deauth Target line unchanged, and
  in-place editing behaves exactly as before.

Verification: native `pio test -e native` 892/892; both device builds (`cardputer`, `m5stick`)
compiled clean with no new warnings; doc lint clean. No new unit test (the one pure helper
`configRowValueX` is already covered by `test/test_config_row`; `ConfigRow::render` is a
device-only template on the compile + on-device gate).
