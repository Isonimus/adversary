---
id: '0097'
title: "Route config-form rows through a shared ConfigRow"
type: slice
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Goal

Close the config-form convergence arc. slice-0089 put the terminal `START` rows on the shared
`ActionButton`; slice-0091 moved three of the four hand-rolled forms' scroll/selection onto
`ScrollableList`; this slice unifies the remaining **content** layer. It builds the
chrome-only `ConfigRow` decided in ADR-0096 and routes the uniform `label ... value` rows of
all four forms (`evil_twin`, `karma`, `ble_bad_ble`, and `handshake` Manual) through it, so
the house look (label x=8, value right-aligned, dim value, selection by band, no chevrons)
is defined once. This picks up and closes `LEDGER.md` item "Unify the hand-rolled config-form
ROW idiom to one house look" (the `ConfigRow` remainder), whose design framing it obeys: a
row component is earned by shape, not by value type, so value strings stay at the call site.

## Definition of Done

- **Given** the `evil_twin`, `karma`, `ble_bad_ble`, and `handshake` Manual config forms
- **When** each is rendered
- **Then** every uniform value row draws its label at x=8 and its value right-aligned with an
  8px margin, the value in dim `TEXT_SECONDARY`, with no `< value >` chevrons, via the single
  `ConfigRow::render` helper.

- **Given** a config row drawn by `ConfigRow`
- **When** its row is the selected one
- **Then** the selection is shown only by the row band (the `ScrollableList` band for the
  three migrated forms, `handshake`'s own `BG_SELECTED` band), and the row's label and value
  text colours are unchanged by selection.

- **Given** a value string whose length and the canvas width are known
- **When** `configRowValueX` computes its x
- **Then** the value's right edge sits exactly `CONFIG_ROW_VALUE_MARGIN` (8px) from the canvas
  right edge, and a null/empty value yields the margin position without dereferencing.

## Design

`ConfigRow` is a header-only component (`src/ui/components/config_row.h`) mirroring
`ActionButton`: a `render(Canvas&)` template that is device-only, plus one pure,
host-testable free function `configRowValueX(screenWidth, value)` for the right-aligned
value x. `render` draws the label at `CONFIG_ROW_LABEL_X` (8) in `TEXT_PRIMARY` and the value
at `configRowValueX(...)` in `TEXT_SECONDARY`, both offset by `theme::LIST_ROW_TEXT_DY`; it
takes **no `selected` flag**, because ADR-0096 fixes selection as a band the caller draws.

**Existing code considered.** The look is taken wholesale from `settings_screen`'s row
drawing (`ROW_TEXT_X=8`, `VALUE_RIGHT_MARGIN=8`, value `TEXT_SECONDARY`, selection = band):
it is the canonical ~50-row list and `handshake` already matched its value alignment, so the
two biggest lists set the house look and the three migrated forms converge onto it. The band
is **deliberately not reused into `ConfigRow`**: the three migrated forms already get it from
`ScrollableList` before the row callback, and drawing a second band in the component would
double it; `handshake` paints its own band because ADR-0094 / the ledger keep it off
`ScrollableList` (mode-free fixed layout, interleaved info lines, bespoke Target row). The
pure-helper + device-template split and the `*_MAX` / framing-helper shape are reused from
`ActionButton` (slice-0088) so the test story matches. `settings_screen` is **not** migrated
onto `ConfigRow`: its `SettingType`-union, editing-state, header/separator rows are a
different (data-driven) shape, the counter-example ADR-0096 names.

Per-form wiring: each form's row callback (or `handshake`'s hand-stepped loop) drops its
local label/value cursor + colour code and calls `ConfigRow::render(canvas, rowTop, label,
valueStr)`, keeping its `valueStr` switch (the call-site formatter). `ble_bad_ble` drops the
`< >` framing and the two-tone colour; `evil_twin`/`karma` drop the ACCENT-on-select label
colour; `handshake` Manual's three middle rows (Auto Deauth / Deauth Count / Timeout) route
through the helper and its selected band flips `ACCENT()` -> `BG_SELECTED()` to match, while
its Target row and `START` row are untouched.

## Verification

- Host unit test `test/test_config_row/test_config_row.cpp` covering `configRowValueX`: the
  right edge lands `CONFIG_ROW_VALUE_MARGIN` from the canvas width for several value lengths
  and widths, a single-char value, a full-width value, and a null/empty value (margin
  position, no deref). Run under `pio test -e native`.
- `ConfigRow::render` is a canvas template and device-only, covered by the compile gate
  (`pio run -e cardputer` and `-e m5stick`, built serially) and on-device review, exactly as
  `ActionButton::render` / `ActionMenu::render` are.
- On-device: confirm all four forms render label-left / value-right in the house look with a
  dim value, no chevrons, and band-only selection that stays legible, on the Cardputer (and
  the M5StickC narrow width for the right-aligned value next to the scrollbar).

## As built

Shipped as designed. `src/ui/components/config_row.h` is the header-only component:
`configRowValueX(screenWidth, value)` (pure, host-tested) plus a device-only
`ConfigRow::render(canvas, rowTop, label, value)` template that draws the label at
`CONFIG_ROW_LABEL_X` (8) in `TEXT_PRIMARY` and the value right-aligned with
`CONFIG_ROW_VALUE_MARGIN` (8) in `TEXT_SECONDARY`, both dropped by `theme::LIST_ROW_TEXT_DY`.
No `selected` flag: selection stays the caller's band.

All four forms route their uniform rows through it. `evil_twin` and `karma` dropped the
ACCENT-on-select label colour; `ble_bad_ble` dropped the `< value >` chevrons and the
two-tone value colour; `handshake` Manual's three middle rows (Auto Deauth / Deauth Count /
Timeout) pass `y - theme::LIST_ROW_TEXT_DY` so the house baseline lands on its hand-stepped
spacing, and its selected band flipped from the bright `ACCENT()` to the house
`BG_SELECTED()` (Target and START rows untouched). `settings_screen` was left on its own
`SettingType` row drawing, as ADR-0096's counter-example.

Verification: `test/test_config_row/test_config_row.cpp` (6 cases on `configRowValueX`)
passed under `pio test -e native` (864/864). Both device builds (`cardputer`, `m5stick`)
compiled clean with no new warnings in our `src/`. Doc lint clean. On-device on the
Cardputer (2026-10-21), the operator confirmed all four forms read label-left / value-right
with a dim value, no chevrons, and band-only selection that stays legible, including
handshake's now-softer band.
