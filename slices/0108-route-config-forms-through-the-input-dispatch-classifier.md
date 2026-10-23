---
id: '0108'
title: "Route the config forms through the input-dispatch classifier"
type: slice
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Goal

Build the pure config-form input classifier from ADR-0107 and route the seven uniform config
forms through it, so ADR-0092's keyboard model lives in one host-tested place instead of seven
hand-rolled copies. The classifier maps `(key, selectedRowKind)` to a `ConfigKeyAction`; each
screen computes its selected row's kind and performs the returned action, keeping only its
`handleEdit` value rings and its activate/navigate/popup bodies. No behaviour change on any
screen: this is a refactor that makes the existing model uniform by construction (ADR-0107),
not a change to what any key does.

## Definition of Done

- **Given** `ui::classifyConfigKey(key, kind)`
- **When** called with each canonical key and each `ConfigRowKind`
- **Then** it returns the ADR-0092 action: `;`->NavUp, `.`->NavDown, `,`/`<`->EditPrev,
  `/`/`>`->EditNext, ENTER->(Value:EditNext, Terminal:Activate, Navigation:Navigate,
  Popup:Popup), and any other key->None. Proven by host unit tests (no canvas, no mock).

- **Given** the beacon_spam, probe_flood, ble_spam, deauth, evil_twin, karma, and ble_bad_ble
  config forms
- **When** the operator navigates, edits, and presses ENTER in the CONFIG state
- **Then** every key behaves exactly as before (nav wraps, `,`/`/` edit, ENTER advances a
  value row / activates the terminal row / routes evil_twin's Target to the scanner / opens
  ble_bad_ble's Name popup), with the mapping now sourced from `classifyConfigKey`.

- **Given** a key the classifier does not own (letter hotkeys, space, back/ESC) or a non-CONFIG
  screen state
- **When** it is pressed
- **Then** the classifier returns None and the screen's own handling runs unchanged.

## Design

New `src/ui/components/config_input.h` (+ `.cpp` if needed; the function is pure and small, so
header-only like ConfigRow is preferred) declares `ConfigRowKind`, `ConfigKeyAction`, and
`ConfigKeyAction classifyConfigKey(char key, ConfigRowKind selectedKind)`. The body is a flat
switch on `key`, with the ENTER case switching on `selectedKind`. No includes beyond what the
enums need; no `theme.h`, no model, no Arduino.

Host test `test/test_config_input/test_config_input.cpp` (new native test env entry) asserts
the full table: each nav/edit key, both aliases (`,`/`<`, `/`/`>`), ENTER against all four
kinds, and a sample of unowned keys returning None.

Each screen's CONFIG-state input path is rewritten to:

```cpp
const ui::ConfigRowKind kind = /* one-liner per screen */;
switch (ui::classifyConfigKey(key, kind)) {
    case ui::ConfigKeyAction::NavUp:    configList_.setItemCount(N); configList_.moveUp();   break;
    case ui::ConfigKeyAction::NavDown:  configList_.setItemCount(N); configList_.moveDown(); break;
    case ui::ConfigKeyAction::EditPrev: handleEdit(-1); break;
    case ui::ConfigKeyAction::EditNext: handleEdit(+1); break;
    case ui::ConfigKeyAction::Activate: /* startAttack() etc, with the screen's guard */ break;
    case ui::ConfigKeyAction::Navigate: /* ScreenManager navigateTo(SCANNER) */ break;
    case ui::ConfigKeyAction::Popup:    /* open the screen's popup */ break;
    case ui::ConfigKeyAction::None:     return false;  // fall through to hotkeys/back/other states
}
needsRedraw_ = true; return true;
```

Row-kind one-liners by screen:
- **beacon_spam, probe_flood, ble_spam, deauth**: `idx == LAST ? Terminal : Value`. (deauth's
  Activate arm keeps its `hasTarget_` guard; its last row stays label/value per slice-0106.)
- **karma**: `idx == LAST ? Terminal : Value`.
- **evil_twin**: `idx == 0 ? Navigation : idx == LAST ? Terminal : Value`. The Fixed-SSID popup
  stays inside `handleEdit(+1)` (its `/` case already opens the popup), so that row is Value.
- **ble_bad_ble**: `idx == 0 ? Popup : idx == LAST ? Terminal : Value`. The Name row's ENTER
  opens the name popup; its `handleEdit` has no row-0 case, so routing ENTER through Popup (not
  EditNext) preserves today's behaviour.

The existing `handleEdit(delta)` methods are unchanged. `handleUp`/`handleDown`/`handleSelect`
collapse into the switch above and are removed where they existed only to serve CONFIG input.
The classifier is only consulted in the CONFIG state; every other state keeps its current
handling verbatim. Letter hotkeys (`m`/`p`/`s`/...), space, and back/ESC stay in the screen's
switch, reached via the `None` fall-through.

**Existing code considered.** This is the input twin of `ConfigRow` (ADR-0096): same "extract
the pure kernel, leave the varying bodies at the call site" shape. `ListScrollModel` keeps
ownership of selection/scroll and the snap contract (ADR-0104); the classifier never touches
it. `settings` and `handshake` are not migrated (ADR-0092 / ADR-0107 scope).

## Verification

- New host unit test `test/test_config_input` proves the full classification table; full native
  suite stays green (`pio test -e native`).
- Both device builds compile clean, built serially (`pio run -e cardputer`, then `-e m5stick`).
- Doc lint + index check clean (`node scripts/lint-docs.mjs`, `node scripts/build-index.mjs`).
- On-device (Cardputer ADV, device in hand): for each of the seven forms, confirm nav wraps,
  `,`/`/` edit each field as before, ENTER advances a value row, ENTER on the START row
  activates (deauth only with a target), evil_twin's Target ENTER opens the scanner, and
  ble_bad_ble's Name ENTER opens the name popup. No key behaves differently from before.

## As built

(to fill after implementation)
