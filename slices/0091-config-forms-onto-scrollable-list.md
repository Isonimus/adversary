---
id: '0091'
title: "Move three hand-rolled config forms onto ScrollableList"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Ledger item 91 ("unify the hand-rolled config-form ROW idiom") has two layers that the
2026-10-20 survey separated: the scroll/selection **mechanism** (owned by `ui::ScrollableList`,
already under seven other config/data forms) and the per-row **content idiom** (label + value +
selection colour, owned by no component, four divergent conventions). This slice does only the
mechanism layer, and only where it fits cleanly: it moves **three** hand-rolled config forms,
`evil_twin` (`renderConfig`), `karma` (`renderConfig`) and `ble_bad_ble` (`drawIdle`), off their
bespoke `int`-index + manual-loop rendering and onto the shared `ScrollableList` /
`ListScrollModel`, matching the proven `ble_spam` / `beacon_spam` / `probe_flood` pattern.

It deliberately does **not** build the `ConfigRow` content helper and does **not** force one
canonical row look: each form keeps its existing value placement and value colouring verbatim
(ble_bad_ble keeps its `< value >` chevron framing and two-tone value colour). The only visual
change is the one the mechanism owns: the selected-row band moves from each form's hand-drawn
`BG_SECONDARY` fill to the component's `BG_SELECTED` (dimmed to `BG_SECONDARY` on footer focus),
which is how the seven already-migrated forms draw it, so this converges the band, not the row
content.

`handshake` (`drawConfig`), the fourth hand-rolled form, is **excluded by measurement**: it is
not a uniform selectable list and will not become one (see Design), so forcing it onto a
uniform-row scroller is the wrong-abstraction cost. It stays with the `ConfigRow` follow-on.

Item 91 therefore stays open after this slice, reduced to: the `ConfigRow` content helper (chrome
only), the single canonical row look, and handshake. Its ledger line is updated, not deleted.

## Definition of Done

- **Given** the `evil_twin` config form, which overflows its area
- **When** the operator navigates past the last visible row
- **Then** the list scrolls via `ScrollableList`'s proportional thumb, with no hand-drawn
  `^` / `v` glyphs and no `configScrollOffset_` bookkeeping remaining in the screen.

- **Given** any of the three migrated forms (`evil_twin`, `karma`, `ble_bad_ble`)
- **When** a config row is the selected row
- **Then** the component draws its selection band (`BG_SELECTED`, dimmed to `BG_SECONDARY` when
  the footer holds focus), and each row's label and value render in that form's existing colours
  and X positions (ble_bad_ble still shows `< Keyboard >`-style chevron values).

- **Given** any of the three migrated forms
- **When** the operator edits a field (`,` / `<`, `/` / `>`, or a letter hotkey) or activates the
  terminal start row with ENTER
- **Then** the same field changes or the same attack starts as before: selection is read from the
  single `ListScrollModel` (no residual `configSelection_` / `m_menuSelection` int), and the
  terminal `ui::ActionButton` start row is preserved at its existing index.

- **Given** the selection is on the first row
- **When** the operator presses up
- **Then** it wraps to the last row (the shared model's behaviour), matching the forms already on
  `ScrollableList`, in place of the old clamp-at-ends.

## Design

**Measured basis.** The 2026-10-20 survey (ledger item 91) catalogued eleven config/option
screens: seven already render through `ScrollableList`, four are hand-rolled. Reading the four
hand-rolled forms' render + input + header this session established their exact shape:

- `evil_twin` (`CONFIG_OPTION_COUNT = 7`): a `Target:` header line above a uniform 7-row
  selectable list, with **manual** scroll (`configScrollOffset_`, `maxVisible`, hand-drawn `^`/`v`
  at `evil_twin_screen.cpp` ~502-522/574-579). The one form that actually overflows and scrolls.
- `karma` (`CONFIG_OPTION_COUNT = 4`): four uniform rows, no scroll, value at x=75.
- `ble_bad_ble` (four fixed rows, magic `4`/`3`): four uniform rows, value at x=80 with `< value >`
  chevron framing and a two-tone value colour (selected value `TEXT_PRIMARY`, else `TEXT_SECONDARY`).
- `handshake` (`CONFIG_ITEMS = 5`): a bespoke `Target:` row (three colour states + a WARNING
  variant), an optional non-selectable CH/BSSID line, and a **mode branch** of 2 selectable items
  (auto-hunt: Target + Start) or 5 (manual: Target + 3 config + Start), plus a four-line
  non-selectable info paragraph. Never scrolls (<=5 rows fit).

**What was reused.** `ui::ScrollableList::render(canvas, model, x, y, w, h, LIST_ROW_HEIGHT,
rowCallback, !footerHints_.hasFocus())` with a `ListScrollModel`, exactly as `ble_spam` uses it.
The component already owns the viewport math, selection-on-screen, the proportional thumb and the
selection band (`BG_SELECTED` / dimmed `BG_SECONDARY`, drawn once in `render`, never in the
callback). Each migrated screen gains one `ListScrollModel` member, its nav handlers become
`setItemCount(N); moveUp()/moveDown()`, and its edit-`switch`es and ENTER read
`selectedIndex()` so the model is the **single source of truth** for selection (the old `int`
index is removed, not kept in sync). The terminal start row stays
`if (idx == COUNT - 1) { ui::ActionButton::render(c, rowY, "START X", selected); return; }` inside
the callback (slice-0089). evil_twin's `Target:` header line is drawn before the list rect, which
starts below it; its manual scroll code is deleted.

**What was deliberately NOT reused / NOT done.**
- **No `ConfigRow`.** The per-row content idiom (value X, chevron framing, value colour) is left
  verbatim in each callback. Extracting a shared content helper is item 91's remaining half, and
  the design discussion this session settled its shape: a **chrome-only** `ConfigRow` (label +
  value + selection colour), with value formatting staying at the call site; a new row component
  earns its place only when a genuinely different in-row **shape** (a stepper, a gauge) clears the
  rule of three, not for each value **type** (toggle/range/switch are formatters of the same
  shape, not new shapes). Building it now, bundled with a mechanism swap, is premature and
  oversizes the slice.
- **No canonical row look forced.** Only the band converges (it is the mechanism's, not the row's).
  Value X positions and ble_bad_ble's chevrons stay divergent until `ConfigRow` lands.
- **handshake excluded.** It is not a uniform selectable list: its item count changes with mode
  (2 vs 5), it interleaves non-selectable info lines, and its first row is bespoke with a WARNING
  state. Routing it through `ScrollableList` would require modelling the info lines as
  non-selectable pseudo-rows, i.e. the `SettingType{HEADER,SEPARATOR}` union `settings_screen`
  uses, which earns its place in a 50-row data-driven screen but not a 5-row one. handshake will
  not become a `ScrollableList` screen even after `ConfigRow` (its three uniform middle rows are a
  `ConfigRow` target; the screen stays a bespoke layout), so excluding it now is correct, not a
  deferral of the same work.

**Behaviour change, intentional.** Navigation wraps at the ends after conversion (the shared
model wraps), where the three forms clamped. This matches the seven forms already on the
component; it is a consistency gain, called out in the Definition of Done.

**No new pure logic.** This is call-site reuse of an already-unit-tested component and model
(`ListScrollModel` math is covered; `ScrollableList::render` is the device-only template). The
proof is the existing suite staying green plus the on-device render/interaction check.

## Verification

- **Native suite stays green** (`pio test -e native`): `test_list_scroll_model` pins the
  viewport/selection/wrap/thumb math this slice leans on; no new pure logic is added, so its
  continued pass is the proof the reused contract is intact. Target: 858/858.
- **Both device builds SUCCESS**, built serially (never concurrently): `pio run -e cardputer`,
  then `pio run -e m5stick`.
- **ADR-0083 warnings gate** clean on both build logs:
  `node scripts/lint-build-warnings.mjs <build.log>`.
- **Docs index + lint green**: `node scripts/build-index.mjs` then `node scripts/lint-docs.mjs`.
- **On-device visual + interaction check** (the real proof for a render/interaction change), per
  migrated screen: rows render at `LIST_ROW_HEIGHT` pitch with the correct labels/values and the
  terminal `ActionButton`; the selected band is `BG_SELECTED` (dimmed on footer focus); every
  field still edits and ENTER still starts the attack; up on row 0 wraps to the last row. For
  `evil_twin` specifically: the list scrolls with the proportional thumb and the old `^`/`v`
  glyphs are gone; the `Target:` header line still sits above the list.

## As built

Shipped as scoped. Three hand-rolled config forms now render through `ui::ScrollableList` /
`ListScrollModel`: `evil_twin` (`renderConfig`), `karma` (`renderConfig`) and `ble_bad_ble`
(`drawIdle`). Each dropped its bespoke `int` selection index (`configSelection_` /
`m_menuSelection`) for a `ListScrollModel` member as the single source of truth; nav handlers
became `setItemCount(N); moveUp()/moveDown()`; edit `switch`es and ENTER read `selectedIndex()`;
the render loops became `ScrollableList::render(...)` callbacks that keep each form's value X and
value colouring verbatim (ble_bad_ble still shows `< value >` chevrons and its two-tone value
colour). evil_twin's manual `configScrollOffset_` + `^`/`v` glyphs were deleted in favour of the
component's proportional thumb, and its `Target:` header line stays above the list. ble_bad_ble's
magic `4`/`3` became a named `IDLE_OPTION_COUNT`. `handshake` was excluded as planned (not a
uniform list; see Design) and stays with the `ConfigRow` follow-on in ledger item 91.

Two intentional, user-visible side effects of adopting the shared model, both matching the forms
already on it: row navigation now **wraps** at the ends (was clamp-at-ends), and the selected band
is the component's `BG_SELECTED` (dimmed `BG_SECONDARY` on footer focus) instead of each form's
hand-drawn `BG_SECONDARY` fill. No interaction was dropped: a full diff against `main` confirmed
every key case, letter hotkey (karma/evil_twin `m`/`p`, evil_twin `s`), popup wiring and
`return` is preserved; only the selection backing changed.

Verification met every Definition-of-Done scenario: native suite 858/858 (incl.
`test_list_scroll_model`); cardputer + m5stick both SUCCESS, built serially; ADR-0083 warnings
gate clean on both logs; docs index + lint green. On-device the operator confirmed the forms
render and the fields edit (via the `,`/`/` side arrows, the house edit convention), closing the
migration.

A post-migration review surfaced a **pre-existing** inconsistency unrelated to this swap: the
config forms use four different keyboard interaction models (arrow-edit vs ENTER-cycles vs
handshake's scanner hop vs settings' typed rows). slice-0091 faithfully preserved each screen's
model; converging them onto one house interaction model is spun out as a dedicated follow-on
(own ADR + slice), not folded here, since it is a behaviour change across ~6 screens, not a
mechanism swap.
