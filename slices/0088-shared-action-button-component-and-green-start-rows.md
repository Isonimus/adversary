---
id: '0088'
title: "Shared ActionButton component and migration of the green start rows"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Build the shared `ActionButton` component decided in ADR-0087 and route the three
standalone green "start operation" rows through it: rfid `>> START AUDIT <<`, rfid
`>> START WRITE <<`, and handshake `>> START AUTO HUNT <<`. Each hand-rolls its own
`fillRect(theme::SUCCESS())` band with a local `rowH` and sets the selected label to
`theme::TEXT_PRIMARY()`, which is a per-theme token that collapses to green-on-green in the
green theme (invisible) and low-contrast white-on-green in the default theme. The component
pairs the fixed-green fill with `theme::BG_PRIMARY()` (near-black in every theme), owns the
band geometry (`theme::LIST_ROW_HEIGHT` + `theme::LIST_ROW_TEXT_DY` centring), and owns the
`>> LABEL <<` framing, deleting the three bespoke bands.

This delivers v1 of the Shared action-button ledger item (the green standalone rows, the
only sites with the readability bug and the only true duplication of the buggy green band).
The config-form terminal rows (handshake `>> START CAPTURE <<`, evil_twin, karma, ble_bad_ble,
ble_spam, deauth, probe_flood, beacon_spam, sniffer, five already on `ScrollableList`) are
deliberately out of scope: per ADR-0087 they are unified with the config-form restyle (ledger
item 91), which already owns "settle one reference config look" and "design the two together."

## Definition of Done

- **Given** a bare verb label such as `"START AUDIT"`
- **When** `ActionButton` frames it for display
- **Then** the framed text reads `">> START AUDIT <<"`, so call sites pass the verb and the
  `>> <<` decoration lives in one place.

- **Given** a selected `ActionButton`
- **When** it renders
- **Then** it fills the full-width band with `theme::SUCCESS()` and draws the label in
  `theme::BG_PRIMARY()` (not `theme::TEXT_PRIMARY()`), so the label is high-contrast in every
  theme, including the green theme where the old selected label was green-on-green.

- **Given** an unselected `ActionButton`
- **When** it renders
- **Then** no band is drawn and the label is `theme::SUCCESS()` text, preserving the green
  "start/go" identity.

- **Given** any `ActionButton` row
- **When** it renders
- **Then** the band height is `theme::LIST_ROW_HEIGHT` and the label is vertically centred by
  `theme::LIST_ROW_TEXT_DY`, with no local `rowH` constant at the call site.

- **Given** each migrated row (rfid Start Audit, rfid Start Write, handshake Start Auto Hunt)
- **When** its screen renders the row and the operator selects it
- **Then** the row draws through `ActionButton` with the correct label and selected state, and
  the underlying action (start audit, start write, start auto hunt) still fires on the same
  existing selection, unchanged.

## Design

### What exists, and what this reuses

- **The band-and-centred-label idiom is the migrated lists'** (`ScrollableList`,
  slice-0064): a full-width selected band with the label centred by
  `theme::LIST_ROW_TEXT_DY` in a `theme::LIST_ROW_HEIGHT` row. `ActionButton` reuses those
  same tokens so a standalone start row lines up with the list rows above it and re-centres
  automatically if the tokens change. It is a new component, not a `ScrollableList` call,
  because these three rows are standalone (a prompt/mode row with other content around them),
  not the terminal row of a scrolling list; the config-terminal rows that *are* list rows
  stay with item 91.
- **The header-only, render-as-template shape is the popup siblings'** (`confirm_dialog.h`,
  `text_input_popup.h`): the logic is host-testable by `#include` alone and `render` is a
  template instantiated only against the device canvas, so no `build_src_filter` entry and no
  native render instantiation.
- **The bands being replaced** were measured in the 2026-10-20 sweep: `rfid_screen.cpp:409`
  and `:1398` (`fillRect(..., theme::SUCCESS())` + `TEXT_PRIMARY()`, local `rowH`),
  and `handshake_screen.cpp:1100` (`fillRect(..., theme::SUCCESS())` + `TEXT_PRIMARY()`,
  hardcoded height 14). All three are deleted; the band, framing, and centring move into the
  component.
- **The contrast pairing is measured, not chosen by eye.** `theme::SUCCESS` is a fixed pure
  green `rgb565(0x00, 0xFF, 0x00)`; `theme::BG_PRIMARY()` is near-black in all four themes
  (`0x0000` default and matrix, `0x0A0000` T-800, `0x000A05` green-T), so black-on-green is
  high-contrast everywhere, where the old `TEXT_PRIMARY` was green `0x00FF41` on green in the
  green theme.
- **Deliberately not reused / not in scope:** the config-form terminal rows (Group B/C of the
  sweep, including handshake `>> START CAPTURE <<`) are left to item 91 (ADR-0087 boundary),
  and the accent style those rows use is **not** added to the component now (no present need;
  it would be speculative generality), only designed to be addable without call-site rework.

### The component

- `src/ui/components/action_button.h` is header-only. A free inline `formatActionLabel(char*
  out, size_t cap, const char* label)` writes `">> LABEL <<"` into a caller buffer (fixed
  `char[]`, no `String`), and is the host-testable unit. `ActionButton::render(Canvas&,
  int16_t x, int16_t y, const char* label, bool selected)` is a template: when `selected` it
  draws `fillRect(0, y, canvas.width(), theme::LIST_ROW_HEIGHT, theme::SUCCESS())` and sets
  the text to `theme::BG_PRIMARY()`; otherwise it sets the text to `theme::SUCCESS()`; then it
  frames the label and prints it at `(x, y + theme::LIST_ROW_TEXT_DY)`.
- No state and no interaction model: selection stays in each screen's existing
  `m_promptSelection` / `m_writePromptSelection` / `configSelection_`. The screens' input and
  action dispatch are untouched; only the render of the row changes.

### Migration

| Screen | Row | Current | After |
| --- | --- | --- | --- |
| `rfid_screen` | Start Audit (`m_promptSelection==1`) | inline `SUCCESS` band, `TEXT_PRIMARY`, `rowH=13` | `ActionButton::render(..., "START AUDIT", selected)` |
| `rfid_screen` | Start Write (`m_writePromptSelection==1`) | inline `SUCCESS` band, `TEXT_PRIMARY`, `rowH=13` | `ActionButton::render(..., "START WRITE", selected)` |
| `handshake_screen` | Start Auto Hunt (`configSelection_==1`) | inline `SUCCESS` band, `TEXT_PRIMARY`, height 14 | `ActionButton::render(..., "START AUTO HUNT", selected)` |

## Verification

- **Unit test** `test/test_action_button/test_action_button.cpp` (native, Unity), covering
  `formatActionLabel`: a bare verb is framed to `">> VERB <<"`; a label at the buffer bound is
  truncated without overflow; an empty label frames to `">>  <<"`. Named in the first
  Definition-of-Done scenario. The `render(Canvas&)` template is device-only and is verified
  by build plus on-device review, as `ActionMenu::render` and `ConfirmDialog::render` are.
- **Builds green:** `pio run -e cardputer` and `pio run -e m5stick` succeed, and the
  new-warnings gate (ADR-0083, `scripts/lint-build-warnings.mjs`) stays clean for our src.
- **On-device:** each migrated row is exercised on hardware in both the default and the green
  theme: selected shows the `SUCCESS` band with a readable near-black label (the contrast fix,
  verified specifically in the green theme where it was previously invisible), unselected shows
  green text with no band, the label is vertically centred, and the start action still fires on
  select. Any row that cannot be verified at merge is recorded in the ledger.
- **Doc gate:** `node scripts/build-index.mjs` and `node scripts/lint-docs.mjs` are green with
  ADR-0087 and this slice added.

## As built

Shipped 2026-10-20 as designed, with two refinements decided during implementation.
`src/ui/components/action_button.h` was added as a header-only render helper (no state, no
interaction model: the selection stays in each screen's existing prompt/config index), and
the three green rows (rfid Start Audit, rfid Start Write, handshake Start Auto Hunt) were
routed through it, deleting their bespoke `fillRect(theme::SUCCESS())` bands and local `rowH`.

Every Definition-of-Done scenario was met:

- `formatActionLabel` frames a bare verb to `">> VERB <<"`, so call sites pass the verb and
  the decoration lives in one place; empty and null verbs frame safely and a short buffer
  truncates without overflow. Covered by `test/test_action_button` (native, Unity):
  **6/6 assertions passed** under `pio test -e native`.
- Selected draws the full-width `theme::SUCCESS()` band with `theme::BG_PRIMARY()` text, and
  unselected draws `theme::SUCCESS()` text on no band. Verified on-device in both the default
  and the green theme: the label is readable in the green theme, where the old per-theme
  `TEXT_PRIMARY` rendered it green-on-green (invisible); the start action still fires on the
  same existing selection.
- The band is `theme::LIST_ROW_HEIGHT` tall with the label centred by `theme::LIST_ROW_TEXT_DY`,
  with no local `rowH` left at any call site.

Two refinements beyond the original design, both operator-driven at review and re-verified
on-device:

- **Horizontal centring.** The label is centred across the full width like a button (the
  symmetric `>> <<` frame made the left-aligned arrows read as adrift), so the `render`
  signature dropped its `x` parameter.
- **`theme::FONT_CELL_WIDTH` token.** Centring needed the glyph-cell width, which was a magic
  `6` duplicated as a local `GLYPH_WIDTH` in two components and ~24 bare `strlen(...) * 6`
  literals. It was extracted to `theme::FONT_CELL_WIDTH = 6` (the horizontal twin of the
  existing `FONT_CELL_HEIGHT`) and consumed here; sweeping the remaining local const and the
  literals onto the token was filed as a separate `[cleanup]` ledger item to keep this commit
  atomic.

Measured verification:

- `pio run -e cardputer` and `pio run -e m5stick` both succeeded; the ADR-0083 new-warnings
  gate (`scripts/lint-build-warnings.mjs`) stayed clean for our src (0 warnings) on both.
- `node scripts/build-index.mjs` and `node scripts/lint-docs.mjs` green with ADR-0087 and this
  slice added (0 errors, 0 warnings).

Scope held to the three green standalone rows. The config-form terminal rows (handshake Start
Capture, evil_twin, karma, ble_bad_ble, and the five already on `ScrollableList`) were left to
the config-form restyle (ledger item 91), which was updated to inherit them and to reuse or
extend `ActionButton`. The Shared action-button ledger item was closed. No device-verify
residual was filed.
