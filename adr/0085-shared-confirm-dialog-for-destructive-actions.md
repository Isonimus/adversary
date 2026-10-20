---
id: '0085'
title: "A shared ConfirmDialog component for destructive and one-shot confirmations"
type: architecture
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Context

The firmware asks the operator to confirm destructive or one-shot actions (delete a
`.pcap`, delete a captured handshake, delete a saved SubGHz signal or an IR code, remove a
whitelist entry, transmit/replay a stored signal). An audit on 2026-10-20 found that the
same idea (confirm an action on the selected item) is implemented five times, four
different ways, with no shared component:

- `ir_record_screen.cpp:682` `drawConfirm(canvas, question)`: left-aligned question in
  WARNING, then the selected item name.
- `radio_screen.cpp:1193` `drawConfirm(canvas, question, accent)`: byte-for-byte the same
  function as `ir_record`'s, plus a colour parameter. This is a literal copy-paste.
- `captures_screen.cpp:1380` `drawDeleteConfirm`: warning line, then filename and size.
- `handshake_screen.cpp:1487` `drawDeleteConfirm`: centered "!" glyph, SSID, type, and a
  "This cannot be undone!" line in ERROR.
- `whitelist_screen.cpp:230` `drawDeleteConfirm`: two `fillRoundRect` Yes/No buttons with
  an `ACCENT()` fill and a `deleteConfirmYes_` toggle.

Three problems follow from this:

1. **Literal duplication.** `ir_record::drawConfirm` and `radio::drawConfirm` are the same
   code. That alone is a DRY violation, and the other three are bespoke re-implementations
   of the same concept.
2. **Four layouts for one concept.** Five screens render "confirm an action on the selected
   item" four visually different ways, so the most safety-critical dialogs in the firmware
   are the least consistent.
3. **The interaction model itself diverges, which is a safety smell.** Four of the five use
   ENTER = execute / ESC = cancel with **no selectable default**, so a reflexive Enter on
   the confirm screen immediately performs the irreversible action. Only `whitelist` makes
   the operator actively choose Yes or No. The common case defaults toward "do it," which
   is exactly backwards for an irreversible operation.

The repo already has a modal family: `ActionMenu` (slice-0077) draws a centered rounded
popup over a darkened backdrop with an ACCENT border and a `BG_SECONDARY` fill, and since
slice-0063 it selects rows with the house `BG_SELECTED()` band rather than an inverted
ACCENT fill. `ActionMenu` is a "pick one of N actions" primitive; there is no shared
"confirm or cancel" primitive, so each screen rolls its own. The `whitelist` Yes/No buttons
are also the sole remaining non-`BG_SELECTED()` selected-item style in any popup (recorded
as idiom 3 of the Shared action-button ledger item after the 2026-10-20 modal audit).

The BadUSB and Mouse Jiggler "USB HID mode" screens are the **same binary-confirm pattern**
and are in scope: `badusb_screen.cpp:342` and `mouse_jiggler_screen.cpp:175` both proceed on
ENTER (disabling the serial console until the next reboot) and cancel on backtick, with no
selectable default, so a reflexive Enter disables serial exactly as a reflexive Enter deletes
elsewhere. They are not irreversible-data confirms, but neither are the transmit/replay
one-shots already in scope, so irreversibility was never the gate; and their multi-line
explanatory body ("Serial console will be disabled until reboot") is content for the dialog's
detail slots, not a different contract. The one nuance: their `drawConfirm` does double duty,
rendering a "USB HID not available" notice under `#else` for non-Cardputer targets, so the
migration replaces only the Cardputer confirm branch and leaves that notice a plain message.

Implementing the migration surfaced a third binary confirm on `radio` the initial audit had
not enumerated: the jammer arming gate (`JAM_SELECT` to `JAM_CONFIRM` to `JAM_ACTIVE`). It
does not render through the duplicated `drawConfirm` (it lives in `drawJam` beside the mode
picker and the hold-to-jam view) and it is already Enter-safe (only `Y` arms), so neither the
DRY win nor the safe-default win depended on it. It is nonetheless the same binary Yes/No
decision on the same screen, with a two-line legal-use notice that fits the dialog's detail
slots, so for one consistent confirm model across the firmware it is folded in here as a
`Warning` confirm. This keeps `radio`'s `drawConfirm` deletion total and removes the
`JAM_CONFIRM` state, leaving the jammer flow as select to confirm (overlay) to active.

A final sweep on 2026-10-20 for destructive actions lacking **any** confirm found one:
`saved_networks_screen.cpp:182` forgets a WiFi credential (SSID and password) immediately on
an `ActionMenu` "Forget Network" select, unlike every other stored-item delete, which all
gate behind a confirm. That is a missing confirm rather than a divergent one, but it is the
same irreversible-loss concern and this component is exactly the fix, so it is added here with
a `Destructive` confirm. The remaining destructive calls in the UI are transient in-memory
clears (`clearDevices`, `clearResults`, `clearTarget`, `clearTag`), which are re-derivable and
need no confirm.

One related flow is deliberately **out of scope**: the rfid `.nfc` write/restore is not a
binary confirm. `rfid_screen.cpp` `WRITE_SUMMARY` is a full write-plan review (UID, data
blocks, skipped trailers) ending in a `>> START WRITE <<` action row, which is correctly
heavier than a yes/no and belongs with the action-button idiom.

## Decision

Add a shared **`ConfirmDialog`** UI component and route the confirm flows across eight screens
through it: the destructive/one-shot confirms on `ir_record` (delete, transmit), `radio`
(delete, transmit, and the jammer arming gate), `captures`, `handshake`, and `whitelist`; the
two USB HID mode-enable confirms (`badusb`, `mouse_jiggler`); and `saved_networks` "Forget
Network" (a new `Destructive` confirm where there was none). The `draw*Confirm` helpers, the
`whitelist` Yes/No buttons, and `radio`'s `JAM_CONFIRM` state are deleted.

- **One interaction model: explicit Yes/No, default Cancel.** The dialog always presents a
  selectable Cancel/Confirm pair drawn with the house `BG_SELECTED()` band, and `show()`
  sets the highlight to **Cancel**. A reflexive Enter therefore cancels; executing a
  destructive action requires the operator to move the selection to Confirm (or press the
  `Y` shortcut). ESC and backtick cancel. This is the safe-by-default behaviour chosen by
  the operator on 2026-10-20, and it makes every confirm in the firmware behave the same
  way.
- **Reuse `ActionMenu`'s modal shell**, not a second look: the centered rounded box, the
  darkened backdrop, the ACCENT border, the `BG_SECONDARY` fill, `;`/`.` navigation, ENTER
  to select, ESC/backtick to dismiss, and uppercase shortcut matching. `ConfirmDialog` is
  the sibling primitive to `ActionMenu`: pick-one-of-N versus confirm-or-cancel.
- **Content slots** cover every migrated screen: a question line, up to three free-text
  detail lines (an item identity like name/size/type, or an explanatory note), and a
  severity. A `Destructive` severity renders in ERROR and adds a "This cannot be undone."
  line (handshake, file, record, signal, code deletes); a `Warning` severity renders in
  WARNING with no undo line (the transmit/replay one-shots and the HID mode-enable confirms,
  which are consequential but reboot-reversible).
- **Call sites shrink** to "configure question/details/severity, `show()`, handle a
  `onConfirm` / `onCancel` callback." The per-screen selection state (`deleteConfirmYes_`
  and the `CONFIRM`/`DELETE_CONFIRM` input branches) moves into the component.

This also resolves the `whitelist` confirm (idiom 3 of the Shared action-button ledger
item) here rather than there, so the two component efforts are sequenced and `whitelist` is
restyled exactly once.

Why single-mode rather than a mode flag: the operator chose one consistent model, and a
single explicit-choice path is the simplest thing that is also the safest (KISS, global
quality bar). The transmit/replay one-shots are not irreversible, but defaulting them to
Cancel costs nothing and keeps one code path.

## Consequences

- **Every confirm behaves identically and safely.** The accidental-Enter-executes risk is
  removed across every migrated confirm (deletes, transmit one-shots, the jammer arming gate,
  the HID mode-enable that disables serial, and the previously-unguarded Forget Network), and
  the dialog is visually consistent with the existing `ActionMenu` popup family.
- **DRY.** The duplicated `ir_record`/`radio` confirm, the two `drawDeleteConfirm`
  re-implementations, the `whitelist` button pair, and `radio`'s bespoke `JAM_CONFIRM` body
  collapse into one component with one test. New confirm sites reuse it instead of copying a
  neighbour.
- **A third modal primitive.** The UI popup set becomes `ActionMenu` (choose an action),
  `ConfirmDialog` (confirm or cancel), and the inline action-button row (start an
  operation). The boundary between them is clear, so future screens pick the right one.
- **Sequencing with the Shared action-button item.** That item keeps idioms 1 and 2 (the
  `>> START <<` rows); its idiom 3 (whitelist Yes/No) is delivered by this component's
  slice, so the action-button slice must not touch `whitelist`'s confirm.
- **Testable logic, device-only render.** Selection default, navigation, and shortcut
  handling are host-testable pure logic (native unit test), exactly as the rest of the UI
  logic is; the `render(Canvas&)` is a template drawn only on the device sprite, like
  `ActionMenu::render`, so it is covered by build + on-device review rather than a host
  assert.
- **Per-screen migration carries behaviour risk that needs on-device verification.** Moving
  each screen's confirm input into the component changes its input branch, so each migrated
  flow (the deletes, the two transmits, the jammer arming gate, the two HID mode-enables, and
  Forget Network) is verified on hardware before the slice's As built is frozen; any residual
  device-verify is tracked in the ledger.
- **Not in scope, by design.** The rfid Write Plan stays a bespoke review screen (a
  write-plan review, not a binary confirm); it could adopt `ConfirmDialog` later only if it
  were ever reduced to a yes/no, without reopening this decision.
