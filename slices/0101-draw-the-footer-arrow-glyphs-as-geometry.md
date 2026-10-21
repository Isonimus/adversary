---
id: '0101'
title: "Draw the footer arrow glyphs as geometry"
type: slice
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Goal

slice-0099 shipped the footer house style, but its arrow glyphs used the CP437 control-range
bytes ADR-0098 specified, and on-device those render as nothing: the M5GFX text layer
discards every code point below `0x20` before drawing (`LGFXBase.cpp:2133-2138`), so the
Cardputer footer showed `:Nav`/`:Edit` with no triangle and the scroll carets went invisible.
ADR-0100 corrects the mechanism to filled-triangle geometry. This slice applies it: a shared
triangle-drawing helper, the footer render and width updated to use it, the three scroll-caret
sites moved onto it, and `footerKeyLabel`'s now-dead arrow cases removed.

## Definition of Done

- **Given** the Cardputer footer on any screen offering arrow-key nav or adjust
- **When** it renders
- **Then** the hint shows a filled triangle drawn as geometry (up+down pair for `;`,
  left+right for `,`, a single down/right for `.`/`/`), pointing the correct way, with no
  blank glyph and no literal punctuation.

- **Given** a `Menu` submenu, or a karma/evil_twin scroll view, with content scrolled off
- **When** it renders on the Cardputer
- **Then** the scroll indicator is a filled up/down triangle, not a blank and not a `^`/`v`
  caret.

- **Given** the key character passed to `footerKeyGlyphCells`
- **When** it is an arrow key or a text key
- **Then** it returns 2 for `;`/`,`, 1 for `.`/`/`, and 0 for the text keys, and
  `footerKeyLabel` still returns `Enter`/`Esc`/`Space`/`Tab` and the uppercased letter for
  those non-arrow keys.

## Design

ADR-0100 is the decision; this slice is its application. Geometry replaces the font byte.

**Helper.** `footer_hints.h` gains, in `adversary::ui`, a `TriGlyph` direction enum, a
device-only `drawTriGlyph(canvas, x, yTop, dir, colour)` template that emits one
`fillTriangle` sized to a `FONT_CELL_WIDTH` (6px) cell at the text top `yTop`, and a pure
`footerKeyGlyphCells(char)` classifier (2 for the bidirectional `;`/`,`, 1 for `.`/`/`, 0 for
text keys). `footerKeyLabel` drops its four arrow cases and keeps the control-key words and
the letter fallback, since arrow keys never reach it now (the render checks
`footerKeyGlyphCells` first).

**Footer render.** `FooterHints::render` draws, for an arrow key, the triangle(s) with
`drawTriGlyph` in the row's current text colour (so the enabled/selected/disabled colour and
the accent-badge contrast all carry through), advances the cursor by `cells *
FONT_CELL_WIDTH`, then prints `:label`; for a text key it prints `footerKeyLabel(key):label`
as before. `getHintWidth` sizes an arrow key as `(cells + 1 + strlen(label)) *
FONT_CELL_WIDTH`, identical to the width slice-0099 reserved, so nothing shifts. The
M5StickC label-only path is untouched (no keyboard, no key glyph).

**Menu bar + scroll carets.** The `Menu` component's hand-rolled bar draws the `;` pair with
`drawTriGlyph` then prints `:Nav`, and keeps `Esc`/`Enter` as `footerKeyLabel` text. The
three scroll-caret sites (`menu.h`, `evil_twin`, `karma`) draw an up or down `drawTriGlyph` in
`theme::ACCENT()` where they printed `\x1E`/`\x1F`.

**Existing code considered.** `fillTriangle` is already provided by the canvas base
(`LGFXBase`); no new primitive is introduced. The helper mirrors the device-template +
host-tested-classifier split of `footerKeyLabel`/`configRowValueX`, so the test story is the
same. The triangle vertex sets are hand-chosen to sit inside the 6x8 text cell; they are
render detail, confirmed on-device, not host-asserted.

## Verification

- Host unit test `test/test_footer_hints/test_footer_hints.cpp` updated: `footerKeyGlyphCells`
  returns 2 for `;`/`,`, 1 for `.`/`/`, 0 for `\n`/`` ` ``/letters; `footerKeyLabel` still
  returns `Enter`/`Esc`/`Space`/`Tab` and uppercases a letter. The triangle byte-string
  assertions are removed, because the bytes are no longer the mechanism. Run under
  `pio test -e native`.
- `drawTriGlyph` and the footer/menu/scroll render are canvas templates, device-only, covered
  by the compile gate (`pio run -e cardputer` and `-e m5stick`, built serially).
- On-device: the footer arrow hints and the scroll carets draw filled triangles pointing the
  right way on the Cardputer, the hint widths are unchanged, nothing overflows at the narrow
  M5StickC width, and the M5StickC still shows action words alone.

## As built

Shipped as designed. `footer_hints.h` gained the `TriGlyph` enum, a device-only
`drawTriGlyph(canvas, x, yTop, dir, colour)` that emits one `fillTriangle` in a 6px cell, a
pure `footerKeyGlyphCells(char)` classifier (2 for `;`/`,`, 1 for `.`/`/`, 0 for text keys),
and `drawFooterKeyGlyphs` for the per-key pair/single dispatch. `footerKeyLabel` dropped its
four arrow cases and keeps the control-key words and the letter fallback.

`FooterHints::render` draws arrow keys with `drawFooterKeyGlyphs` in the row's current text
colour (so the enabled/selected/disabled contrast carries through) and advances the cursor by
`cells * FONT_CELL_WIDTH` before printing `:label`; text keys print `footerKeyLabel:label` as
before. `getHintWidth` sizes an arrow key from the cell count, so the widths are identical to
slice-0099 and no hint moved. The `Menu` component's bar draws the `;` pair with the helper,
and the three scroll-caret sites (`menu.h`, `evil_twin`, `karma`) draw a single Up/Down
triangle in `theme::ACCENT()` where they had the unreachable `0x1E`/`0x1F` bytes.

Verification: `test/test_footer_hints/test_footer_hints.cpp` rewritten to the classifier (5
cases on `footerKeyGlyphCells` plus the control/letter mapping; the byte-string asserts
removed) passed under `pio test -e native`. Both device builds (`cardputer`, `m5stick`)
compiled clean, built serially. Doc lint clean (100 docs; the ADR-0098 -> ADR-0100
supersession link validated). On-device on the Cardputer (2026-10-21) the operator confirmed
the footer arrow hints and the scroll carets now render as filled triangles pointing the
right way, with widths unchanged and no overflow at the M5StickC width.
