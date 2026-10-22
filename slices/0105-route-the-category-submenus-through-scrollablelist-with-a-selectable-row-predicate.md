---
id: '0105'
title: "Route the category submenus through ScrollableList with a selectable-row predicate"
type: slice
status: accepted
date: 2026-10-22
supersedes: []
superseded_by: []
---

## Goal

Route the Wireless/BLE/IR/RFID/HID category submenus (the `Menu` component, `menu.h` render
template) through the shared `ScrollableList`, the last selection-list holdout after the
migration the LEDGER marks "MIGRATION COMPLETE". Today `Menu::render` hand-rolls its own
`selection_`/`startIdx`/`maxVisible` math and still paints the OLD bright-`ACCENT()`-band +
inverted-text selection idiom that slice-0063 replaced everywhere else, so primary navigation
is the only screen class visually inconsistent with every data list. This also delivers the
mechanism ADR-0104 decides: a selectable-row predicate in `ListScrollModel`, which lets the
component carry the submenus' non-selectable rows (separators + live `isEnabled()`-off
entries). Folding the predicate in retires the three hand-rolled skip-navs (the `Menu`
component, settings' `isSelectableRow`/`nextSelectable`, the IR main menu's inline skip-loop) —
the rule-of-three trigger the project held open as the LEDGER "skip-nav not in the shared model"
item. The hand-rolled action bar in `Menu::render` moves onto the `FooterHints` component, as
its own comment anticipated. The `CarouselMenu` (animated horizontal icon main menu) is a
different widget and stays hand-rolled.

Closes, on on-device verification (ADR-0022 keeps hardware-verify items until confirmed): the
LEDGER "Route the category submenus through `ScrollableList`" line and the "Disabled/
non-selectable row skip-nav is hand-rolled per screen" line.

## Definition of Done

- **Given** a `ListScrollModel` with a selectable predicate that rejects some rows (e.g.
  separators)
- **When** the cursor navigates with `moveUp`/`moveDown` across a rejected row
- **Then** the selection lands on the next *selectable* row, wrapping at the ends, and never
  rests on a rejected row

- **Given** a `ListScrollModel` whose predicate rejects *every* row
- **When** `moveUp`/`moveDown` is called
- **Then** the selection does not change and the call returns (no infinite loop)

- **Given** a `ListScrollModel` with no predicate set (the default)
- **When** it is navigated
- **Then** its behaviour is identical to before this slice (every index selectable)

- **Given** a category submenu on-device (Wireless/BLE/IR/RFID/HID)
- **When** the operator scrolls it
- **Then** the selected row shows the app-wide `BG_SELECTED` band (not the old accent-inverted
  bar), separators and greyed `isEnabled()`-off rows are skipped by the cursor, submenu `>`
  chevrons and reason-bearing disabled labels still render, a proportional scrollbar thumb
  replaces the up/down triangles, and the footer hints render through `FooterHints`

## Design

**Model (ADR-0104).** Add `setSelectablePredicate(std::function<bool(int)>)` to
`ListScrollModel`. A private `isSelectable(i)` returns `pred_ ? pred_(i) : true` (bounds-guarded);
a private `seekSelectable(from, dir)` wraps from `from` up to `itemCount_` steps and returns the
first selectable index or `-1` when none exists. `moveUp`/`moveDown` seek in their direction and
apply the result only when `>= 0` (no-move when none selectable, which is the loop-termination
guarantee). `setSelectedIndex` and `setItemCount` snap via a forward seek so the selection is
never left on a rejected row after a jump or a list rebuild. `setViewport` is untouched
(preserves the existing "snap on nav/rebuild, not per-frame" behaviour — see ADR-0104). Empty
predicate = all selectable, so the eight existing consumers are behaviour-identical.

Representation is `std::function<bool(int)>`: the predicates capture a single pointer, which
fits the libstdc++/libc++ small-object buffer (two pointers wide) on both the 64-bit host and
the 32-bit xtensa target, so no heap allocation. A `static_assert` in the test pins the capture
size. Rejected: a function-pointer + `void* ctx` pair (zero-alloc but C-style, untestable
cleanly) and a virtual interface (ceremony for a one-method callback).

**Considered + reused.** `ScrollableList::render` already takes a per-row `drawRow(canvas,
itemIndex, rowTopY, isSelected)` callback that closes over the screen's item list, so the
submenus' per-row features (chevron, greying, `disabledLabel`, separator line) move into that
callback unchanged — no new component surface. The band and the unified thumb are already the
component's. `FooterHints` (`footer_hints.h`, already included by `menu.h` for
`ui::footerKeyLabel`) replaces the hand-rolled bar. **Not reused / changed:** `Menu`'s own
`selection_`/`startIdx`/`maxVisible` and `findNextSelectable`/`ensureValidSelection` are deleted
in favour of a `ListScrollModel` member; settings' `isSelectableRow`/`nextSelectable` and the IR
main menu's inline skip-loop are deleted in favour of the model predicate. Rows go uniform-height
(separator becomes a full-height row with a centered divider) because `ScrollableList`'s
`h / rowHeight` viewport math is single-height by design; adding variable heights would be
speculative generality for one caller (ADR-0104).

## Verification

- **Unit tests** (`test/test_list_scroll_model/test_list_scroll_model.cpp`, native, extends the
  slice-0062 suite):
  - `test_predicate_skips_rejected_rows_on_nav` — moveDown/moveUp step over a rejected row to
    the next selectable (first DoD scenario).
  - `test_predicate_wraps_over_rejected_rows_at_ends` — wrap at the ends skips rejected rows.
  - `test_predicate_all_rejected_does_not_move_or_loop` — all-rejected list: selection
    unchanged, call returns (second DoD scenario, loop-termination guard).
  - `test_set_selected_index_snaps_off_rejected_row` — a jump onto a rejected row snaps to the
    nearest selectable.
  - `test_set_item_count_snaps_selection_off_rejected_row` — a rebuild that leaves the selection
    on a rejected row snaps it.
  - `test_no_predicate_is_all_selectable` — default (unset) predicate reproduces pre-slice
    behaviour (third DoD scenario).
  - `test_predicate_capture_fits_std_function_small_buffer` — `static_assert` the capture fits
    the small-object buffer (no-alloc claim).
- **On-device** (the fourth DoD scenario cannot be unit-tested — it is rendering + input on the
  five category submenus plus the settings and IR screens whose skip-navs this retires): verify
  on the Cardputer ADV that each category submenu shows the `BG_SELECTED` band, skips separators
  and greyed rows, renders chevrons/disabled-labels/thumb, and that the settings list and IR
  main menu still navigate correctly. Primary-navigation blast radius, so this gate is mandatory
  before the two LEDGER lines are deleted (ADR-0022). Recorded in `## As built`.

## As built

Shipped as designed. `ListScrollModel` gained `setSelectablePredicate(std::function<bool(int)>)`
with a private `isSelectable`/`seekSelectable`/`snapSelectionToSelectable`: `moveUp`/`moveDown`
skip rejected rows (wrapping, no-move when none selectable), `setSelectedIndex`/`setItemCount`
snap off a rejected row, `setViewport` stays untouched. The seven new unit tests went red then
green (one build showed the five behavioural tests failing on the un-wired model, all green after
wiring); the full native suite is 892/892. The `static_assert` confirmed the single-pointer
capture fits the `std::function` small buffer (no heap alloc).

`Menu::render` now draws through `ScrollableList` with the shared `BG_SELECTED` band (the
accent-inverted idiom retired), a uniform `theme::LIST_ROW_HEIGHT` (14px, the shared row height,
not a bespoke 18 — more consistent with every data list than the ADR's "uniform-18" wording), a
separator drawn as a centered divider line, the proportional thumb replacing the up/down
triangles, and the footer moved onto the `FooterHints` component. `Menu`'s `selection_`/`startIdx`
/`maxVisible`/`findNextSelectable`/`ensureValidSelection` were deleted for a `ListScrollModel`
member driven by `syncModelToLevel`. Settings' `isSelectableRow`+`nextSelectable`+
`firstSelectableIndex` and the IR main menu's inline skip-loop+`firstEnabledMain` were both
retired onto the predicate.

- **DoD 1 (nav skips rejected rows):** met — `test_predicate_skips_rejected_rows_on_nav`.
- **DoD 2 (all-rejected: no move, no loop):** met — `test_predicate_all_rejected_does_not_move_or_loop`.
- **DoD 3 (no predicate = unchanged):** met — `test_no_predicate_is_all_selectable`, plus the
  eight pre-existing consumers behave identically.
- **DoD 4 (on-device):** met. Verified on the Cardputer ADV 2026-10-22 across all five category
  submenus plus the settings and IR screens: shared `BG_SELECTED` band (not accent-inverted),
  separators and greyed `isEnabled()`-off rows skipped and showing their reason labels, submenu
  chevrons, proportional scrollbar thumb on a long submenu, `FooterHints` footer (Nav/Select at
  root, Back/Select in a submenu), back restoring the parent selection, and the IR Learn row
  greyed+skipped with no RX module. Builds: native 892/892, cardputer SUCCESS (Flash 75.1%), m5stick SUCCESS.
