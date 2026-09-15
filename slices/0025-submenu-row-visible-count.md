---
id: '0025'
title: "Draw every submenu row that fits — recover the dropped 5th row"
type: slice
status: accepted
date: 2026-09-14
supersedes: []
superseded_by: []
---

## Goal

Submenus render only **4 rows** and leave a ~15px dead band above the action bar, so a
longer list (Wireless, BLE) looks like it holds four entries with no hint that more exist.
A 5th row fits on screen but is never drawn.

Two off-by-one errors in `Menu::render` (menu.h) conspire:

1. `maxVisible = contentAreaHeight / itemHeight = 87 / 18 = 4` — integer truncation caps the
   loop one row short.
2. The overflow guard `yPos + itemHeight > SCREEN_HEIGHT - 20` breaks at `yPos = 100`
   (`100 + 18 = 118 > 115`), even though the row's *selection band* there is `[95, 113]` and
   `113 < 115` — it clears the action bar. The guard models a row as `itemHeight` tall
   *below* `yPos`, but the band actually starts `highlightInsetAbove` (5px) *above* `yPos`
   and is `itemHeight` tall, so it ends at `yPos + 13`, not `yPos + 18`.

Draw the row that fits.

## Definition of Done

- **Given** a submenu with more than four entries (e.g. Wireless)
- **When** it renders at the top of the list
- **Then** five full rows are drawn — the fifth's selection band ends at y=113, clearing the
  y=115 action-bar top — and the residual gap above the action bar is ~2px, not ~15px

- **Given** a submenu longer than the visible window
- **When** it renders
- **Then** the `v` scroll indicator still marks "more below", and navigating down still keeps
  the selected row on screen (`startIdx` tracks `selection_` against the corrected
  `maxVisible`)

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green (menu.h is header-only geometry; no behavioural test exists for
  the pixel layout — see Verification)

## Design

### The band model, not a content-height quotient

`maxVisible` is now the count of selection bands that clear the action bar, computed from the
same geometry the guard uses:

```
maxVisible = (actionBarTop - (yPos - highlightInsetAbove)) / itemHeight
           = (115 - (28 - 5)) / 18 = 92 / 18 = 5
```

and the guard breaks only when a band's bottom would cross `actionBarTop`:

```
(yPos - highlightInsetAbove) + itemHeight > actionBarTop
```

Both now agree on where a row's pixels actually land. `highlightInsetAbove` and
`actionBarTop` are named so the two call sites can't drift apart again.

### Why five *full* rows, not a partial "peek"

The initial intent was a half-cut sliver of the next item as the "more below" cue. The 18px
grid rules it out: rows land at y = 28, 46, 64, 82, 100, 118. The fifth (100) sits fully
inside the content area; the sixth (118) sits fully *under* the action bar (its text baseline
is below y=115). No grid position straddles the 115 boundary, so un-dropping the row yields a
fifth **full** row, not a sliver. A genuine sliver would need row spacing grown to ~20px (4
full rows + a clipped fifth) — that spaces out every submenu, including the short ones, for a
cue the existing `v` arrow already carries. On-device the five-full-rows result read
correctly, so the extra spacing was not taken (operator, 2026-09-14).

### Not native-testable, and why that is fine

`Menu::render` is a `template<typename Canvas>` header that draws to an M5 canvas; the layout
is pixel geometry against `config::SCREEN_HEIGHT`, with no pure function to assert. The proof
is the arithmetic above plus on-device confirmation, not a unit test — the same posture as
the surrounding render code.

## Verification

- Build: `pio run -e cardputer` SUCCESS — RAM 26.4% (86,632 B, unchanged), Flash 73.9%
  (2,468,995 B, unchanged vs. HEAD; the edit is arithmetic on existing locals, no size cost).
- Native: `pio test -e native` 752/752 (menu.h compiles via its dependents; no behaviour
  changed for the native build).
- On-device (operator, 2026-09-14): flashed; Wireless and the other submenus now show five
  full rows with a ~2px bottom gap; the `v` more-below arrow and down-navigation still work.

## As-built

Shipped as designed. `Menu::render` (menu.h) replaced the `contentAreaHeight / itemHeight`
`maxVisible` with the band-count formula and introduced named `highlightInsetAbove` (5) and
`actionBarTop` (`SCREEN_HEIGHT - 20`) locals; the overflow guard now tests the band bottom
(`(yPos - highlightInsetAbove) + itemHeight > actionBarTop`) instead of `yPos + itemHeight`.
No other file changed. The partial-peek idea was evaluated and rejected on geometry grounds
(see Design); five full rows plus the pre-existing `v` arrow was confirmed on-device.
