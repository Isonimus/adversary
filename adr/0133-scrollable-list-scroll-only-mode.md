---
id: '0133'
title: "Scroll-only (no-selection) mode for ScrollableList"
type: architecture
status: accepted
date: 2026-10-28
supersedes: []
superseded_by: []
---

## Context

`ScrollableList` + `ListScrollModel` (slice-0062) were built to kill four divergent
hand-rolled scrollbar idioms by routing every scrolling list through one component with a
single proportional thumb. The selection lists migrated, but six read-only views were
deliberately left behind because the component assumes **1 item = 1 selectable row, always
paints a selection band, and wraps at the ends** (slice-0065 scoped them out).

Those six views are, today, two families that still hand-roll their own `scrollOffset_` int
and clamp logic:

- **Multi-line-row logs** (each item spans 2+ text lines; draw the old up/down triangle
  carets; hardcode a `maxVisible` row budget):
  - karma probes: `renderProbes` (`probeScrollPos_`)
  - karma credentials: `renderCredentials` (`credentialScrollPos_`)
  - karma per-client traffic: `renderClientTraffic` (`trafficScrollPos_`)
  - evil-twin credentials: `evil_twin_screen.cpp` (`credentialScrollPos_`)
- **Single-line text blocks** (1 line = 1 row at a uniform `LINE_HEIGHT`; already clamp with
  no wrap; show an "N/total" text readout as a third surviving scrollbar idiom):
  - About (`about_screen.cpp:206`)
  - What's New (`whats_new_screen.cpp:156`, whose header already notes it is "the same shape
    as AboutScreen")

A grep on scroll-state members is authoritative: these are the only hand-rolled scroll views
left. Every other scrolling list already uses `ListScrollModel`.

This is six copies of the same scroll arithmetic, which is the exact drift the component
exists to prevent. The `maxVisible` hardcode is the same bug class `ListScrollModel`'s
docstring cites (commit a2417c8: a hardcoded visible-count that sits below the rendered
rows). And the operator **A/B-confirmed on device (2026-10-18, slice-0067 review)** that
karma's probes view visibly lags the migrated clients list on the same screen: two idioms
side by side, one the conspicuous laggard once the selection lists were done.

The reason none of these could reuse the component unchanged is mechanical: `ListScrollModel`
derives `scrollOffset_` from `selectedIndex_` via `clampOffsetToSelection()`, which runs on
every `setViewport()` (i.e. every frame). A view with no selection would have its window
yanked back toward index 0 each frame. So a read-only log is not just "a selection list that
hides the band": the scroll unit itself has to change from the selection to the window.

## Decision

Add a **scroll-only mode** to the shared component rather than making these views fake
selection lists or keeping them bespoke. A read-only log has no item to act on, so a real
selection cursor would be a lie; a scroll-only mode is the honest contract and keeps all
scroll arithmetic in the one unit-tested place.

`ListScrollModel` gains `setScrollOnly(bool)` / `isScrollOnly()`. When set:

1. `moveUp()` / `moveDown()` move `scrollOffset_` directly by one row, **clamped to
   `[0, itemCount_ - visibleRows_]` with no wrap**. Wrapping a read-only log from bottom
   back to top is disorienting; a log clamps at both ends. (Selection mode keeps its wrap.)
2. `clampOffsetToSelection()` skips the pull-window-to-selection step and only clamps
   `scrollOffset_` into range. This is what makes per-frame `setViewport()` safe: the window
   holds its position instead of snapping to the selection, and a freshly appended item never
   jerks the view.
3. `selectedIndex_` and the selectable predicate are unused; thumb geometry
   (`isScrollable` / `thumbHeightPx` / `thumbTopPx`) is reused verbatim, since it is already
   pure `scrollOffset_` / `itemCount_` / `visibleRows_` math.

`ScrollableList::render` reads `model.isScrollOnly()` and, when true, **paints no selection
band** and passes `isSelected = false` to every row callback. The render signature is
unchanged, and the default (`scrollOnly_ == false`) leaves every existing selection list
byte-for-byte identical.

Both families then route through the component:

- Text blocks (About, What's New): `rowHeight = LINE_HEIGHT`, the callback draws one text
  line. The proportional thumb replaces the "N/total" readout.
- Multi-line logs (karma x3, evil-twin creds): `rowHeight =` the item's full pixel height,
  the callback draws the item's lines inside that row. The thumb replaces the triangle
  carets and the `maxVisible` hardcode is deleted (the renderer derives the row budget from
  the live rect, as it already does for selection lists).

## Consequences

- One scroll idiom across the whole UI: the triangle-caret scrollbar and the "N/total"
  readout are both retired, finishing the slice-0062 unification. Six hand-rolled
  `scrollOffset_` copies and their `maxVisible` hardcodes are deleted.
- The scroll arithmetic for these views is now host-unit-tested (clamp-no-wrap at both ends,
  window held across `setViewport`, thumb geometry) instead of living untested in each
  screen.
- The mode is additive and default-off, so no selection list changes behaviour; the only
  visible change to the migrated read-only views is the scrollbar (thumb instead of
  carets/text) and the exact row budget now following the live layout rect.
- The ledgered BadUSB script viewer gets a ready-made scroll-only text-viewer contract to
  build on instead of a seventh hand-rolled copy.
- Whether this removes the operator-observed probes lag is an on-device check, not something
  provable from the host build; recorded as a hardware-verify follow-up in the ledger. The
  refactor stands on the DRY/consistency grounds above regardless of the lag outcome.
- A scroll-only list intentionally cannot be acted on. The alternative (make probes /
  credentials selectable so the operator picks one to act on) is a feature, not this
  refactor, and is left for a future slice if a per-item action is ever wanted.
