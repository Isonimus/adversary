---
id: '0100'
title: "Draw footer and scroll arrow glyphs as filled-triangle geometry"
type: architecture
status: accepted
date: 2026-10-21
supersedes: ['0098']
superseded_by: []
---

## Context

ADR-0098 decided the Cardputer footer arrow keys render as CP437 directional triangles
(`0x1E` up, `0x1F` down, `0x11` left, `0x10` right), on the strength of decoding
`glcdfont.h` and finding those bitmaps in the table. slice-0099 shipped it. On-device
(2026-10-21) the hints rendered as **nothing**: `;:Nav` became `:Nav`, the pairs blank, and
the scroll carets slice-0099 had switched to the same bytes went invisible.

The measurement was of the font bitmap table, not of the text render path, and the render
path is where it fails. Read from the active library (`M5GFX`,
`lgfx/v1/LGFXBase.cpp:2133-2138`), the text loop discards every code point below `0x20`
before it ever reaches `drawChar`:

```cpp
do { ... uniCode = decodeUTF8(*string); } while (uniCode < 0x20 && *(++string));
if (uniCode < 0x20) break;
```

So although `GLCDfont` (the default `Font0`) does carry triangle bitmaps at
`0x10`/`0x11`/`0x1E`/`0x1F`, `print()`/`draw_string` never emits them: the four triangle
code points are exactly the control range the text layer filters out (the `0x18`-`0x1B` line
arrows ADR-0098 also noted sit in the same filtered range). The CP437 triangles are
unreachable through the text API on this stack, full stop.

This is the measurement error ADR-0098 should have caught: "the font table contains glyph X"
is not "`print()` draws glyph X." The render path has the final say, and it was never
measured. Hence this correction.

## Decision

Draw the footer arrow affordances as filled-triangle **geometry** with `canvas.fillTriangle`
(`LGFXBase`), not as font glyphs. Geometry does not pass through the text layer, so it is
immune to the control-code filter and renders identically on both targets regardless of the
active font.

A device-only helper in `adversary::ui` draws one triangle of a given direction within a
`FONT_CELL_WIDTH` (6px) cell at the text baseline, in the caller's current colour. The
direction and pairing semantics from ADR-0098 are **unchanged**: `;` (vertical nav) draws the
up+down pair, `,` (horizontal adjust) the left+right pair, `.` a single down, `/` a single
right, one 6px cell per triangle so the hint's width model (one glyph cell == one char cell)
is preserved and the layout from slice-0099 does not move. The scroll-position carets in the
`Menu` component, `evil_twin` and `karma` draw through the same helper.

**The verb half of ADR-0098 carries forward unchanged and in force**: ESC = `Back`/`Stop`/
`Discard`, ENTER = `Select`/`Change`/`Start` plus the kept domain verbs (`Inject`, `Blast`,
`Capture`, `Read`, `Run`, `Pause`/`Resume`). This ADR supersedes ADR-0098 only to correct the
glyph-rendering mechanism; it re-affirms the one-verb-per-action vocabulary verbatim.
`footerKeyLabel` keeps its control-key words (`Enter`/`Esc`/`Space`/`Tab`) and letter
fallback; only its arrow-key cases are removed, because arrows are now geometry, not labels.

## Consequences

- `footer_hints.h` gains a device-only `drawTriGlyph(canvas, x, yTop, dir, colour)` template
  plus a `footerKeyGlyphCells(key)` classifier; `getHintWidth` sizes arrow keys from the cell
  count and text keys from `footerKeyLabel` as before, so widths are identical to slice-0099.
- `footerKeyLabel` loses its `0x1E`/`0x1F`/`0x11`/`0x10` cases; the host test shifts from
  asserting those byte strings to asserting `footerKeyGlyphCells` (2 for `;`/`,`, 1 for
  `.`/`/`, 0 for text keys) plus the unchanged control/letter mapping.
- The `Menu` component's hand-rolled bar and the three scroll-caret sites call the shared
  helper instead of printing a byte: no more invisible glyphs.
- Triangle rendering is device-only (a canvas template), covered by the compile gate +
  on-device review like every other render template; the classifier and width stay
  host-tested.
- The lesson is recorded for the next glyph question: verify the render path, not the font
  table. A code point present in the font is not drawable if the text layer filters it.
- Not in scope: the verb vocabulary (settled by ADR-0098, re-affirmed here) and the
  `Menu`->`ScrollableList` rework (open ledger item).
