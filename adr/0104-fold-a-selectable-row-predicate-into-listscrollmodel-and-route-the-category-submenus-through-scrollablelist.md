---
id: '0104'
title: "Fold a selectable-row predicate into ListScrollModel and route the category submenus through ScrollableList"
type: architecture
status: accepted
date: 2026-10-22
supersedes: []
superseded_by: []
---

## Context

slice-0062/0063 extracted the shared scrolling-selection-list component: `ListScrollModel`
(pure selection + scroll-window state, host-unit-tested) and `ScrollableList` (the device
renderer that derives the viewport from the live rect, paints the selection band itself, and
draws one unified proportional thumb). The selection-list migration then ran to completion:
the LEDGER records "MIGRATION COMPLETE: every selection list is now on the component."

One holdout remains, and it is the most-used screen class: the category submenus
(Wireless/BLE/IR/RFID/HID), drawn by `Menu::render` (`src/ui/components/menu.h`). They still
keep their own `selection_` + `startIdx` + `maxVisible` math and still draw the OLD
bright-`ACCENT()`-band-with-inverted-`BG_PRIMARY`-text selection idiom that slice-0063
replaced everywhere else. Primary navigation is now the only screen class visually
inconsistent with every data list, and the inverted text is the long-standing contrast nit.

Two things have blocked a straight migration, and both are now resolved by a single decision:

1. **Non-selectable rows.** `ListScrollModel`/`ScrollableList` assume `1 row == 1 selectable
   index` and always paint a band. The submenus have rows that must be skipped by the cursor:
   structural `SEPARATOR`s (drawn, never selected) and live `isEnabled()`-off entries
   (slice-0042) that grey themselves and show a reason-bearing `disabledLabel`.
2. **Variable row height.** Menu rows are 18px but separators are 14px; `ScrollableList`'s
   viewport math is `h / rowHeight`, a single fixed row height.

The skip-the-non-selectable-row logic is now hand-rolled in **three** places:

- `Menu::findNextSelectable` / `isSelectable` / `ensureValidSelection` (`menu.h:187-195`), over
  its own `selection_`;
- settings' `isSelectableRow(SettingType)` free function plus a `nextSelectable` lambda driving
  `itemList_` (`settings_screen.cpp:39-40,365-382`);
- the IR main menu's inline skip-loop over `mainSelection_` (`ir_record_screen.cpp:426-427`).

That is the rule of three (global quality bar). The project deliberately *held* this as a
LEDGER item until a third concrete consumer appeared, to get the abstraction right rather than
build it speculatively on the first instance. The three copies differ (structural vs
runtime-disabled), which is exactly the kind of drift the `ListScrollModel` extraction exists
to prevent: slice-0062 cites two opposite scroll bugs that shipped from divergent private
copies of the scroll-window rule.

## Decision

1. **Add an optional selectable predicate to `ListScrollModel`.**
   `void setSelectablePredicate(std::function<bool(int)> pred)`. `moveUp`/`moveDown` skip
   indices the predicate rejects (still wrapping at the ends); `setSelectedIndex` and
   `setItemCount` snap the selection to the nearest selectable index when it would otherwise
   land on a rejected row. `setViewport` is deliberately left out of the snap: it is layout, not
   content, and the existing `Menu` snaps the cursor off a non-selectable row only on navigation
   and on list rebuild (`ensureValidSelection` after `setItems`/`reset`/`enterSubmenu`), never
   per-frame, so a row that greys live while selected stays selected until the next nav, with
   `selectCurrent` fail-loud-refusing to dispatch a disabled item. Folding the snap into the
   per-frame `setViewport` would silently change that; keeping it out preserves the behaviour
   exactly. An **empty predicate is the default and means every index is selectable**, so every
   existing consumer is behaviour-unchanged.

   - **Representation: `std::function<bool(int)>`.** The usual embedded objection is heap
     allocation, but the predicates here capture a single `this` pointer, which fits inside
     libstdc++'s `std::function` small-object buffer (its `_Any_data` union is two
     pointers / member-pointer wide on both the 64-bit host and the 32-bit xtensa target), so
     storing one does not allocate. The model's unit test pins this with a `static_assert` on
     the captured lambda's size against that buffer, so the claim is measured, not assumed.
   - **Rejected alternatives.** A function-pointer + `void* ctx` pair is zero-allocation but
     C-style, loses type safety, and is clumsier to unit-test; a virtual `ISelectable`
     interface is ceremony for a one-method callback. Once the allocation objection is measured
     away, the captured-lambda form is the most readable and is what the call sites already
     express (`[this](int i){ return items_[i].isSelectable(); }`).
   - **Termination is a fail-loud contract.** When *no* index is selectable (e.g. a list of
     only separators), `moveUp`/`moveDown` return the selection unchanged rather than loop
     forever. The model never spins; building an all-non-selectable list is a caller error, and
     the unit test pins the no-move behaviour so a regression cannot reintroduce the spin.

2. **Render the submenus through `ScrollableList`** with the shared `BG_SELECTED` band,
   retiring the `ACCENT`-band + inverted-text idiom. The per-row `drawRow` callback carries the
   Menu-specific content only: the `disabledLabel`-vs-`label` text, `TEXT_DISABLED` greying for
   gated-off rows, the submenu `">"` chevron, and the separator divider line. The band and the
   proportional thumb are the component's; the up/down triangle scroll glyphs are retired in
   favour of the unified thumb, exactly as every migrated data list already did.

3. **Menu rows become uniform height.** The separator becomes a full-height row whose callback
   draws a centered divider line. Variable-height rows are explicitly **not** added to
   `ScrollableList`: no other consumer needs them, and the fixed `rowHeight` is load-bearing
   for the `h / rowHeight` viewport math. Adding variable heights would be speculative
   generality on behalf of a single caller.

4. **Collapse the three hand-rolled skip-navs onto the predicate.** `Menu`, `SettingsScreen`,
   and the IR main menu each drive a `ListScrollModel` with a selectable predicate; their
   private `isSelectable`/`findNextSelectable`/`ensureValidSelection`/`nextSelectable`/inline
   skip-loop code is deleted. This closes the LEDGER "skip-nav not in the shared model" item.

5. **Move the Menu action bar onto `FooterHints`.** The hand-rolled bottom bar in
   `Menu::render` is already a `FooterHints` lookalike, and its own comment anticipates this
   move "rides with the Menu->ScrollableList rework." The lookalike is deleted; `FooterHints`
   expresses the root "Nav" vs submenu "Back" context and the keyboardless M5Stick variant.

## Consequences

- **One selection idiom app-wide.** Primary navigation now matches every data list, and the
  menus inherit `ListScrollModel`'s unit-tested thumb math and viewport-from-rect, so they are
  protected from the two scroll bugs slice-0062 cited. The predicate is a general capability
  any future non-uniform list can use.
- **One skip-nav implementation instead of three divergent copies**, which is the rule-of-three
  payoff and the point of holding the item until now.
- **Widest blast radius of the `ScrollableList` migrations.** This touches PRIMARY navigation
  across the five category submenus plus the settings screen and the IR menu, so **on-device
  verification across all of those is mandatory before the implementing slice closes** (a green
  native build is necessary but not sufficient). Per ADR-0022 the two LEDGER lines this consumes
  stay open until that hardware check passes.
- **A visible change to the most-used screens.** The submenu selection goes from
  accent-inverted to the subtle app-wide band. This is intended (that is the unification), but
  it will read as a change to anyone used to the old look. The uniform-height separator is a
  minor cosmetic change (slightly more whitespace around the divider line).
- **`ListScrollModel` gains a `<functional>` dependency and a `std::function` member.** It
  stays header + `.cpp`, stays host-unit-tested, and keeps zero Arduino/Canvas coupling. The
  predicate does not own the item list: the screen owns both model and items and must keep the
  predicate valid for the model's lifetime, which the setter documents.
- **The `CarouselMenu`** (the animated horizontal icon main menu) is a different widget and
  stays hand-rolled; it is out of scope.
- **No new deferral.** This decision consumes two existing LEDGER items (the submenu routing and
  the skip-nav-not-in-the-model item) rather than creating one; both are deleted when the
  implementing slice ships and verifies on-device.
