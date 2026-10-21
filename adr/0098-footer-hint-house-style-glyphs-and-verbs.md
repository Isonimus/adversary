---
id: '0098'
title: "Footer-hint house style: arrow-key glyphs and one verb per action"
type: architecture
status: superseded
date: 2026-10-21
supersedes: []
superseded_by: ['0100']
---

## Context

The `FooterHints` component (`src/ui/components/footer_hints.h`) is the one action-hint bar
every screen draws, but the ~148 `setHints(...)` call sites have drifted on two axes, and the
ledger carried this as the "unify footer-hint copy" audit (design-first before the mass edit).

**Key glyphs.** `getKeyLabel()` only names `\n`->`Enter`, `` ` ``->`Esc`, space->`Space`,
tab->`Tab`; every other key prints as its raw uppercase char. The Cardputer's four arrow keys
are the punctuation `;` (up), `.` (down), `,` (left), `/` (right), so 29 `{';', "Nav"}` hints
render as the literal `;:Nav`, plus `.:Nav`, `,:Edit`, and rfid's `;:^ .:v`. The direction the
key means is invisible, and the stray punctuation reads as noise.

Whether a real arrow glyph is available was a measurable question, so it was measured, not
estimated. The canvas sets no font, so it uses M5GFX's built-in default (6px cell =
`FONT_CELL_WIDTH`), which is the classic CP437 GLCD font (`.pio/.../M5GFX/.../glcdfont.h`,
1280 bytes = 256 glyphs x 5 columns). Decoding that table confirms filled directional
triangles exist as single bytes: `0x1E` = up-triangle, `0x1F` = down-triangle, `0x11` =
left-triangle, `0x10` = right-triangle (the `0x18`-`0x1B` line-arrows exist too but read worse
at 5px). None of these are the `\n`/`\r` codes the `print()` layer intercepts, so they draw as
ordinary glyphs. The existing ASCII `^`/`v` carets (`menu.h`, `evil_twin`, `karma`, rfid
footer) were habit, not a font limit.

**Verbs.** The shared keys carry synonym drift. ESC (`` ` ``): `Back` x57, `Stop` x10,
`Exit` x6, `Abort` x3, `Discard` x2 - `Back`/`Exit` are one action, `Stop`/`Abort` are one
action. ENTER (`\n`): 25 distinct labels; `Select` x12 / `Open` x3 / `View` x2 / `Go` x2 /
`Details` / `Action` all mean "activate the highlighted row," and a stray `Toggle` survived
slice-0093's standardization on `Change` x13. This is the same disease the destructive-verb
table in `CLAUDE.md` already cured for Delete/Clear/Discard/Forget; this ADR extends that
treatment to the footer's navigation and activation verbs.

## Decision

**1. Arrow keys render as filled triangles pointing the way the physical key points.**
`getKeyLabel()` maps the Cardputer arrow cluster to the CP437 triangle glyphs:

| Key | Physical label | Glyph | Byte |
|-----|----------------|-------|------|
| `;`  | up    | up-triangle then down-triangle (the bidirectional pair) | `0x1E 0x1F` |
| `,`  | left  | left-triangle then right-triangle (the bidirectional pair) | `0x11 0x10` |
| `.`  | down  | down-triangle | `0x1F` |
| `/`  | right | right-triangle | `0x10` |

`;` and `,` map to the *pair* because in this UI they are the handles for bidirectional
movement: `;` for vertical list nav/scroll, `,` for horizontal value adjust. A single
affordance is therefore one hint on the representative key, not a redundant two-hint pair:
`{';', "Nav"}` renders `up-down-triangles Nav`, `{',', "Edit"}` renders
`left-right-triangles Edit`. `.` and `/` keep single-direction glyphs for the rare explicit
one-way case. This mapping only affects the Cardputer render path; M5StickC (no keyboard)
already prints the label alone, so it is untouched.

**2. One verb per action on the shared keys.** ESC and ENTER draw from a fixed vocabulary;
a domain verb is kept only where it carries meaning the generic verb would lose.

ESC (`` ` ``):

| Verb | Action | Folds in |
|------|--------|----------|
| `Back` | leave the screen / return to the menu (nothing is running) | `Exit` |
| `Stop` | cancel the operation currently running (scan, attack, capture) | `Abort` |
| `Discard` | abandon unsaved in-progress data (per the `CLAUDE.md` destructive table) | - |

ENTER (`\n`), generic verbs:

| Verb | Action | Folds in |
|------|--------|----------|
| `Select` | activate the highlighted list item | `Open`, `View`, `Go`, `Action`, `Details` (when generic) |
| `Change` | cycle the selected config value (slice-0093) | `Toggle` |
| `Start` | begin an operation that then runs | `Go` (when it starts an op) |

ENTER domain verbs are kept, because the generic verb would erase what the action does:
`Inject` (HID keystrokes), `Blast` (spam burst), `Capture` (handshake), `Read` (RFID tag),
`Run` (execute an SD script: the DuckyScript idiom), and transient states (`Wait...`,
`Pause`/`Resume` on a pausable op). A fold is applied only where the label is a true synonym
of the generic action; a verb that names a distinct operation stays.

## Consequences

- `getKeyLabel()` grows the four arrow-key cases; `getHintWidth()` already measures
  `strlen(getKeyLabel(key))`, so the 2-glyph pair labels size correctly with no other change.
- The ~30 hint sites using `;`/`.`/`,`/`/` collapse to a single representative-key hint and
  lose their literal-punctuation look; redundant up+down hint pairs (ble_spam, rfid scroll)
  become one hint each.
- ESC `Exit`->`Back` and `Abort`->`Stop`; ENTER `Toggle`->`Change` and the generic
  activation synonyms -> `Select`, applied with per-site judgement so domain verbs survive.
- New screens have a table to copy instead of a guess, which is the point: the footer stops
  being a place drift re-enters. The glyph set is CP437-only and must be confirmed on-device
  (the triangles are the first non-ASCII glyphs the UI prints deliberately).
- Not in scope: the footer's focus/navigation model, `FooterHint`'s structure, and screens
  whose ENTER already uses a correct domain verb. This is a copy-and-glyph pass, not a
  behaviour change.
