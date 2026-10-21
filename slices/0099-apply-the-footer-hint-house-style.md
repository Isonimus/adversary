---
id: '0099'
title: "Apply the footer-hint house style"
type: slice
status: accepted
date: 2026-10-21
supersedes: []
superseded_by: []
---

## Goal

Apply the footer-hint house style decided in ADR-0098 across every screen, closing the
`LEDGER.md` audit "Unify footer-hint copy across every screen". That item named three drifts:
(a) the Cardputer arrow keys `;`/`.`/`,`/`/` printing as literal punctuation (`;:Nav`), (b)
duplicated-vs-missing hints, and (c) more than one verb for the same action. This slice makes
`getKeyLabel` render the arrow keys as the CP437 directional triangles ADR-0098 measured,
collapses the redundant up+down hint pairs to one representative-key hint, and folds the ESC
and ENTER synonym drift onto the one-verb-per-action vocabulary, keeping the domain verbs the
ADR lists.

## Definition of Done

- **Given** any screen whose footer offered list navigation or value adjust on the Cardputer
  arrow keys
- **When** its footer is rendered on the Cardputer
- **Then** the hint shows a filled directional triangle (up-down pair for `;` nav, left-right
  pair for `,` adjust) instead of the literal `;`/`.`/`,`/`/` character, and no screen shows a
  redundant second hint for the opposite direction.

- **Given** the ESC and ENTER hints across all screens
- **When** each is rendered
- **Then** ESC reads `Back` (leave), `Stop` (cancel a running op) or `Discard` (unsaved data)
  with no `Exit`/`Abort`, and ENTER reads a single generic verb (`Select`/`Change`/`Start`)
  for its generic action, with domain verbs (`Inject`, `Blast`, `Capture`, `Read`, `Run`)
  kept only where ADR-0098 lists them.

- **Given** the key character passed to `footerKeyLabel`
- **When** it is one of the arrow keys, Enter, Esc, Space, Tab or a letter
- **Then** it returns the mapped label (`;`->up-down triangles, `,`->left-right triangles,
  `.`->down triangle, `/`->right triangle, `\n`->`Enter`, `` ` ``->`Esc`, letters uppercased).

## Design

ADR-0098 is the house style; this slice is its application. Two code shapes:

**Component.** The private `FooterHints::getKeyLabel` static becomes a free function
`footerKeyLabel(char)` in `adversary::ui` (the pure-helper + device-template split reused from
`ActionButton`/`ConfigRow`, so the mapping is host-testable) and gains the four arrow-key
cases returning the CP437 triangle byte strings (`\x1E` up, `\x1F` down, `\x11` left, `\x10`
right; `;` and `,` return the two-glyph pair). `getHintWidth` already measures
`strlen(footerKeyLabel(key))`, so the 2-glyph labels size with no further change. The
M5StickC path prints the label alone and is untouched.

**Call sites.** Each `setHints` site using the arrow keys drops its redundant partner hint
(e.g. ble_spam's `{';',"Nav"},{'.',"Nav"}` and rfid's `{';',"^"},{'.',"v"}` become one
`{';', "Nav"}` / `{';', "Scroll"}`), and the ESC/ENTER labels are rewritten to the ADR
vocabulary (`Exit`->`Back`, `Abort`->`Stop`, `Toggle`->`Change`, generic
`Open`/`View`/`Go`/`Action`/`Details`->`Select`). Each fold is judged per site so a domain
verb is never flattened.

**The Menu component's footer.** The category submenus are drawn by the `Menu` component
(`menu.h`), whose hand-rolled 20px action bar printed the raw key chars (`` `:Back ``,
`;/.:Nav`) and so sat outside the `setHints` sweep. It now composes its bar from
`footerKeyLabel` as well (root `<up-down triangles>:Nav  Enter:Select`, submenu
`Esc:Back  Enter:Select`), with the M5StickC label-only split mirrored from `FooterHints`.
This is the payoff of extracting `footerKeyLabel` as a free function: a hand-rolled bar can
share the one glyph source without adopting the whole component. Moving the menu fully onto
`FooterHints` is left to the `Menu`->`ScrollableList` rework (the open ledger item), which
reworks this rendering anyway.

**Existing code considered.** This extends the destructive-verb table already in `CLAUDE.md`
and slice-0093's `Change` standardization to the navigation/activation verbs; no new component
is introduced. The triangle glyphs reuse the font the UI already renders (ADR-0098's
measurement of `glcdfont.h`), not a bundled font. The focus/navigation model of `FooterHints`
and the `FooterHint` struct are deliberately left alone: this is copy and glyphs, not
behaviour.

## Verification

- Host unit test `test/test_footer_hints/test_footer_hints.cpp` on `footerKeyLabel`: each
  arrow key returns its triangle byte string (`;`->`"\x1E\x1F"`, `.`->`"\x1F"`,
  `,`->`"\x11\x10"`, `/`->`"\x10"`), and `\n`->`"Enter"`, `` ` ``->`"Esc"`, ` `->`"Space"`,
  `\t`->`"Tab"`, a lowercase letter uppercases, an uppercase letter is unchanged. Run under
  `pio test -e native`.
- The render path is a canvas template and device-only, covered by the compile gate
  (`pio run -e cardputer` and `-e m5stick`, built serially).
- On-device: confirm the triangles draw correctly and point the right way on the Cardputer,
  the verb copy reads consistently, and no footer overflows at the narrow M5StickC width
  (ADR-0098 flags these as the first deliberate non-ASCII glyphs the UI prints).

## As built

Shipped as designed, plus two drift fixes found during the sweep. `getKeyLabel` became the
free `footerKeyLabel(char)` in `adversary::ui` (header-only in `footer_hints.h`, mirroring
`configRowValueX`), with the four CP437 triangle cases: `;`->`"\x1E\x1F"` (up+down pair),
`,`->`"\x11\x10"` (left+right pair), `.`->`"\x1F"`, `/`->`"\x10"`. `getHintWidth` already
measured `strlen(footerKeyLabel(key))`, so the two-glyph pairs sized correctly with no other
change.

Across the drifting sites the redundant up+down hint pairs collapsed to one
representative-key hint (ble_spam config, rfid scroll views), and the ESC/ENTER vocabulary
settled to ESC = `Back`/`Stop`/`Discard` (folding `Exit`->`Back`, `Abort`->`Stop`) and ENTER =
`Select`/`Change`/`Start` for the generic actions (folding `Open`/`View`/`Go`/`Action`/`OK`/
`Toggle`/`Details`), with the domain verbs (`Inject`, `Blast`, `Capture`, `Read`, `Run`,
`Pause`/`Resume`, `Continue`) kept. Final corpus: ESC = Back x65 / Stop x13 / Discard x2.

Two fixes beyond pure relabeling, both the "duplicated vs missing hints" class the audit
named: (1) ble_spam's post-stop config footer advertised `Prev`/`Next`/`Toggle` and a dead
`d`=Start key the handler never implemented; it now shows the honest `Nav`/`Edit`/`Start`/
`Back` set matching the config-entry footer. (2) karma's config footer showed only the
`M`/`P` shortcuts despite having the same nav+edit+ENTER model as its sibling config forms;
it now shows the house `Focus`/`Nav`/`Change` set (the `M`/`P` shortcuts stay live in
handleInput, just unadvertised, since the narrow M5StickC footer cannot hold both).

The house style also reached two non-`setHints` surfaces: the `Menu` component's hand-rolled
action bar (`menu.h`) now composes from `footerKeyLabel` (`Esc:Back  Enter:Select`, the
triangle pair for Nav, label-only on M5StickC), and the bespoke `^`/`v` scroll carets in
`menu.h`, `evil_twin`, and `karma` became the triangles `0x1E`/`0x1F` so the scroll glyph
matches the footer glyph. Routing the `Menu` component fully onto `FooterHints` is left to the
`Menu`->`ScrollableList` rework (open ledger item), which reworks that rendering anyway.

Verification: `test/test_footer_hints/test_footer_hints.cpp` (8 cases on `footerKeyLabel`:
each triangle byte string, the two-vs-one-glyph widths, the control keys, letter uppercasing)
passed under `pio test -e native` (872/872). Both device builds (`cardputer`, `m5stick`)
compiled clean, built serially. Doc lint clean. On-device on the Cardputer (2026-10-21) the
operator confirmed the triangles render and point correctly, the verb copy reads
consistently, the menu footer shows `Esc:Back`, the scroll carets are triangles, and no
footer overflows at the M5StickC width.
