---
id: '0086'
title: "Shared ConfirmDialog component and migration of the confirm screens"
type: slice
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Goal

Build the shared `ConfirmDialog` component decided in ADR-0085 and route the confirm flows
across eight screens through it (the destructive/one-shot confirms on `ir_record`, `radio`,
`captures`, `handshake`, and `whitelist`, including `radio`'s jammer arming gate discovered
during the migration; the two USB HID mode-enable confirms in `badusb` and `mouse_jiggler`;
and `saved_networks` "Forget Network", which gains a confirm where it had none), deleting the
bespoke confirm renderers.
The component presents one interaction model everywhere: a selectable Cancel/Confirm pair
drawn with the house `BG_SELECTED()` band, defaulting the highlight to Cancel so a reflexive
Enter cancels rather than executes an irreversible action.

This delivers the `whitelist` Yes/No confirm, which is idiom 3 of the Shared action-button
ledger item; that item keeps idioms 1 and 2, and this slice takes `whitelist`'s confirm so
it is restyled once. It also removes the literal `ir_record` / `radio` `drawConfirm`
duplication and the two bespoke `drawDeleteConfirm` layouts.

## Definition of Done

- **Given** a `ConfirmDialog` configured for a destructive delete and shown
- **When** the operator presses ENTER immediately, without moving the selection
- **Then** `onCancel` is invoked and `onConfirm` is not, because the default highlight is
  Cancel.

- **Given** a shown `ConfirmDialog`
- **When** the operator moves the selection to Confirm and presses ENTER, or presses the
  `Y` shortcut
- **Then** `onConfirm` fires exactly once and the dialog hides; and **when** instead ESC,
  backtick, or `N` is pressed, **then** `onCancel` fires and no action runs.

- **Given** a `ConfirmDialog` with `Destructive` severity
- **When** it renders
- **Then** it shows the ERROR-coloured "This cannot be undone." line; **and given**
  `Warning` severity (a transmit/replay one-shot), **then** that line is absent and the
  question renders in WARNING.

- **Given** each migrated flow (IR code delete and IR transmit in `ir_record`, SubGHz
  signal delete, transmit, and jammer arm in `radio`, file delete in `captures`, handshake
  delete in `handshake`, entry delete in `whitelist`, the USB HID mode-enable in `badusb` and
  `mouse_jiggler`, and "Forget Network" in `saved_networks`)
- **When** its confirm is triggered
- **Then** it renders through `ConfirmDialog` with the correct question, detail lines, and
  severity, and the underlying action (delete, transmit, `jamArm`, `activateHid`, or
  `removeWiFiCredential`) runs only on an explicit Confirm.

- **Given** the native unit test for the component
- **When** `pio test -e native` runs
- **Then** the default-Cancel, navigation, shortcut, severity, and callback cases pass.

## Design

### What exists, and what this reuses

- **The modal shell and input idiom are `ActionMenu`'s** (`src/ui/components/action_menu.*`).
  `ConfirmDialog` is written as its sibling: the same centered rounded box over a darkened
  backdrop with an ACCENT border and `BG_SECONDARY` fill, the same `;`/`.` navigation, ENTER
  to select, ESC/backtick to dismiss, and uppercase shortcut matching (`handleInput` mirrors
  `ActionMenu::handleInput` at `action_menu.cpp:45`). It is a new component, not an extension
  of `ActionMenu`, because the contracts differ: `ActionMenu` is pick-one-of-N and fires a
  per-item callback; `ConfirmDialog` is binary and fires `onConfirm`/`onCancel`.
- **The selected-row band is the house `BG_SELECTED()`** set in slice-0063 and used by
  `ActionMenu` (`action_menu.h:219-221`) and `paintMenuBand` (`ir_record_screen.cpp:540`),
  so the dialog is visually consistent with every migrated list.
- **The confirm renderers being replaced** were measured in the 2026-10-20 audit:
  `ir_record_screen.cpp:682` and `radio_screen.cpp:1193` (identical bar a colour param),
  `captures_screen.cpp:1380`, `handshake_screen.cpp:1487`, the `whitelist_screen.cpp:230`
  Yes/No buttons with the `deleteConfirmYes_` toggle (`whitelist_screen.cpp:267-280`), and
  the two USB HID mode-enable confirms (`badusb_screen.cpp:195` / input at `:342`,
  `mouse_jiggler_screen.cpp:93` / input at `:175`), which proceed on ENTER and cancel on
  backtick with no default, the same binary pattern. All are deleted or reduced; their
  per-screen confirm state and input branches move into the component. On the HID screens
  the migration touches only the Cardputer confirm branch; the `#else` "USB HID not
  available" notice stays a plain message.
- **`radio`'s jammer arming gate (`JAM_CONFIRM`) is folded in too.** Discovered during the
  migration, it is a third binary confirm on `radio`, rendered inside `drawJam` (not the
  shared `drawConfirm`) and already Enter-safe. Per ADR-0085 it becomes a `Warning` confirm
  (its legal-use notice fills two detail slots); the `JAM_CONFIRM` state is removed and the
  jammer flow becomes select to confirm (overlay) to active.
- **`saved_networks` gains a confirm it never had.** "Forget Network"
  (`saved_networks_screen.cpp:182`) today calls `removeWiFiCredential` directly from the
  `ActionMenu` select, with no confirmation. The migration inserts a `Destructive`
  `ConfirmDialog` between the select and the removal, so forgetting a credential requires an
  explicit Confirm. This is the one added confirm (new behaviour), not a replaced renderer; a
  2026-10-20 sweep confirmed it is the only persisted destructive action in the UI with no
  confirm (the other direct destructive calls are transient in-memory clears).
- **Deliberately not reused / not in scope:** the rfid `WRITE_SUMMARY` write-plan review
  (`rfid_screen.cpp:1342`) stays a bespoke screen (ADR-0085): it is a full plan review, not
  a binary confirm.

### The component

- `src/ui/components/confirm_dialog.h` is header-only: all methods inline, with
  `render(Canvas&)` a template over the device sprite, matching the sibling popups
  `text_input_popup.h` and `character_selector.h`. Header-only keeps the logic host-testable
  by `#include` alone (the render template is never instantiated on native, so the component
  needs no `build_src_filter` entry), where `ActionMenu`'s `.cpp` is not in the native build
  at all.
- API: `setQuestion(const char*)`, `addDetail(const char*)` (up to three lines, copied into
  fixed `char[]` buffers to avoid `String` heap churn per CLAUDE.md), `setSeverity(Severity)`
  with `enum class Severity { Warning, Destructive }`, `setOnConfirm` / `setOnCancel`,
  `show()`, `hide()`, `isVisible()`, `handleInput(char)`, and `render(Canvas&)`.
- Two options are laid out as a Cancel/Confirm pair. `selectedIndex_` starts at the Cancel
  index in `show()`; ENTER fires the highlighted option's callback and hides; `Y`/`N` are
  shortcuts to Confirm/Cancel; ESC/backtick fire `onCancel`.
- `Destructive` renders the question and the "This cannot be undone." line in ERROR;
  `Warning` renders the question in WARNING and omits the undo line.

### Migration

Each screen replaces its confirm renderer and confirm-state input branch with a
`ConfirmDialog` member, configured on entry to the confirm and dispatched by its callbacks.
Confirm copy follows the destructive-verb invariant this pass introduced to `CLAUDE.md`:
`whitelist`'s "Remove" becomes "Delete", `saved_networks` keeps "Forget" (the WiFi idiom), and
the file/record/code/signal deletes already read "Delete".

| Screen | Flow(s) | Severity |
| --- | --- | --- |
| `ir_record_screen` | delete code, transmit code | Destructive, Warning |
| `radio_screen` | delete signal, transmit signal, jammer arm | Destructive, Warning, Warning |
| `captures_screen` | delete file (name + size details) | Destructive |
| `handshake_screen` | delete handshake (SSID + type details) | Destructive |
| `whitelist_screen` | remove entry (SSID/BSSID detail) | Destructive |
| `badusb_screen` | enable USB HID (serial-disabled note) | Warning |
| `mouse_jiggler_screen` | enable USB HID (serial-disabled note) | Warning |
| `saved_networks_screen` | forget network (added confirm; SSID detail) | Destructive |

## Verification

- **Unit test** `test/test_confirm_dialog.cpp` (native, Unity), covering: the default
  highlight is Cancel and an immediate ENTER invokes `onCancel`; navigating to Confirm +
  ENTER and the `Y` shortcut both invoke `onConfirm` once; `N`, ESC, and backtick invoke
  `onCancel`; severity selection drives whether the undo line is produced. Named in each
  Definition-of-Done scenario above. The `render(Canvas&)` template is device-only and is
  verified by build + on-device review, as `ActionMenu::render` is.
- **Builds green:** `pio run -e cardputer` and `pio run -e m5stick` succeed, and the
  new-warnings gate (ADR-0083, `scripts/lint-build-warnings.mjs`) stays clean for our src.
- **On-device:** each migrated confirm (the IR and SubGHz deletes, the two transmit
  one-shots, the jammer arm, the file and handshake deletes, the whitelist delete, the two USB
  HID mode-enables, and Forget Network) is exercised on hardware: the default lands on Cancel,
  Enter cancels, an explicit Confirm performs the
  action, and the action never fires without it. Any flow that cannot be verified at merge is
  recorded in the ledger.
- **Doc gate:** `node scripts/build-index.mjs` and `node scripts/lint-docs.mjs` are green
  with ADR-0085 and this slice added.

## As built

Shipped 2026-10-20 as designed. `src/ui/components/confirm_dialog.h` was added as a
header-only component with the safe-by-default Cancel highlight, and every confirm flow
across the eight screens was routed through it, deleting the five bespoke `draw*Confirm`
renderers, the `whitelist` Yes/No buttons, and the per-screen confirm states
(`DELETE_CONFIRM`, `REPLAY_CONFIRM`, `JAM_CONFIRM`, `CONFIRM_DELETE`, the HID `CONFIRM`
branch).

Every Definition-of-Done scenario was met:

- The default highlight is Cancel so an immediate ENTER invokes `onCancel`; `;`/`.`
  navigation to Confirm + ENTER and the `Y` shortcut each invoke `onConfirm` once; `N`,
  ESC, and backtick invoke `onCancel`; and `Destructive` vs `Warning` severity drives
  whether the "This cannot be undone." line is produced. All covered by
  `test/test_confirm_dialog` (native, Unity): **8/8 assertions passed** under
  `pio test -e native`.
- Each migrated flow (IR code delete and transmit; SubGHz signal delete, transmit, and
  jammer arm; file delete; handshake delete; whitelist entry delete; the two USB HID
  mode-enables; and Forget Network) renders through `ConfirmDialog` with the correct
  question, detail lines, and severity, and its action (delete, transmit, `jamArm`,
  `activateHid`, `removeWiFiCredential`) runs only on an explicit Confirm. Verified
  on-device: the highlight landed on Cancel, a reflexive Enter cancelled, and the action
  never fired without an explicit Confirm.

Measured verification:

- `pio run -e cardputer` and `pio run -e m5stick` both succeeded; the ADR-0083 new-warnings
  gate (`scripts/lint-build-warnings.mjs`) stayed clean for our src (0 warnings).
- `node scripts/build-index.mjs` and `node scripts/lint-docs.mjs` green with ADR-0085 and
  this slice added (0 errors, 0 warnings).

One deviation from the original eight-flow audit: `radio`'s `JAM_CONFIRM` jammer-arming gate
was discovered during implementation and folded in as a `Warning` confirm on the operator's
decision; ADR-0085 and this slice were updated to record it, keeping `radio`'s `drawConfirm`
deletion total. No device-verify residual was filed to the ledger.
