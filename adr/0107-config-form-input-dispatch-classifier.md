---
id: '0107'
title: "Config-form input dispatch as a pure classifier"
type: architecture
status: accepted
date: 2026-10-23
supersedes: []
superseded_by: []
---

## Context

ADR-0092 converged every uniform attack/config form onto one keyboard model (the `ble_spam`
superset): `;`/`.` navigate (wrap), `,`/`<` and `/`/`>` edit the selected field
previous/next, and ENTER performs the selected row's primary action *by row kind* (value row
advances like `/`, terminal row activates, navigation row routes to the WiFi scanner, popup
row opens its popup). That ADR closed by naming the helper it is a spec for: "A future shared
`ConfigRow`/input-dispatch helper can encode this model in one place; this ADR is the spec it
implements." slice-0093 implemented the model, but **per screen**: it left seven copies of the
same key-to-action and ENTER-by-row-kind mapping in `beacon_spam`, `probe_flood`, `ble_spam`,
`deauth`, `evil_twin`, `karma`, and `ble_bad_ble` (each as a `handleUp`/`handleDown`/
`handleSelect`/`handleEdit` quartet with an identical outer structure).

The per-screen copies are uniform *by convention*, not *by construction*. The ENTER-inert bug
ADR-0092 was written to kill (reported 2026-10-20: operator presses ENTER to change a field,
nothing happens, reads as a broken control) lives in exactly this mapping, and nothing stops a
new config form, or an edit to an existing one, from reintroducing it. The rule of three is
long past (seven consumers).

What is genuinely shared across the seven is only the *decision*: given a key and the kind of
the currently selected row, which canonical action fires. What legitimately varies, and must
stay per screen, is the *mechanism*: the `handleEdit(delta)` value rings (each field has its
own enum/number cycle), what "activate" does (`startAttack` vs `startWithConfig`), the
navigation target, the popup, and per-row guards (e.g. `deauth`'s terminal row only fires with
a target). Folding row kind into a declarative per-form option model was considered and
rejected here: it reshapes all seven screens' data models for no behavioural gain and is the
speculative generality the quality bar forbids.

ConfigRow (ADR-0096) already set the precedent for this family: extract the one pure,
host-testable kernel (`configRowValueX`) and leave the varying content at the call site. The
input layer wants the same treatment.

## Decision

The canonical input model is encoded once as a **pure classifier**, not a callback dispatcher
that owns control flow:

```cpp
enum class ConfigRowKind { Value, Terminal, Navigation, Popup };
enum class ConfigKeyAction { None, NavUp, NavDown, EditPrev, EditNext,
                             Activate, Navigate, Popup };

// Pure: no state, no I/O, no canvas, no model. Maps one keypress plus the selected
// row's kind to the canonical action. Host-testable by include alone.
ConfigKeyAction classifyConfigKey(char key, ConfigRowKind selectedKind);
```

The mapping it owns (and the only place the mapping lives) is exactly ADR-0092's model:

- `;` -> `NavUp`; `.` -> `NavDown` (the caller's `ListScrollModel` wraps).
- `,` / `<` -> `EditPrev`; `/` / `>` -> `EditNext`.
- ENTER (`\n` / `\r`) dispatches on `selectedKind`:
  - `Value` -> `EditNext` (ENTER is never inert: it advances the field, identical to `/`);
  - `Terminal` -> `Activate`;
  - `Navigation` -> `Navigate`;
  - `Popup` -> `Popup`.
- Any other key -> `None` (not consumed; the screen's own switch handles letter hotkeys,
  space, back/ESC, and non-config states).

Each config form, in its CONFIG state, computes the selected row's `ConfigRowKind` (for most
forms a one-liner: terminal on the last row, value otherwise) and performs the returned action
in a small switch. The mechanical arms (`NavUp`/`NavDown` call `setItemCount` + `moveUp`/
`moveDown`; `EditPrev`/`EditNext` call the screen's existing `handleEdit`) are uniform; the
semantic arms (`Activate`/`Navigate`/`Popup`) hold the per-screen bodies the screen already
has. `None` returns false so the key falls through to the screen's remaining handling.

Control flow stays inside each screen: the classifier decides, the screen acts. There is no
`std::function`, no captured callback struct built per keypress, and nothing hidden from a
reader of `handleInput`. The classifier is a pure function of two scalars, so its entire
contract (every key, every row kind, including the ENTER-by-kind table) is pinned by host unit
tests with no canvas and no mock, the way `configRowValueX` and the ListScrollModel predicate
are.

Scope matches ADR-0092 exactly. `settings` stays out (heterogeneous typed controls, not a
uniform value list; ADR-0096 counter-example). `handshake` stays out as a consumer but already
conforms on the ENTER axis (its Target row is the canonical `Navigation` row). The classifier
governs the CONFIG-state uniform value list only; letter hotkeys, space, and back remain
per-screen and additive.

## Consequences

- The ADR-0092 key-to-action and ENTER-by-row-kind mapping exists in exactly one place and is
  covered by host tests. The ENTER-inert regression cannot reappear in a migrated form, and a
  new config form adopts the model by calling one function instead of copying a quartet.
- Each migrated screen drops its hand-rolled `;`/`.`/`,`/`/`/ENTER mapping and its
  `handleSelect` row-kind branch, keeping only `handleEdit` (the value rings) and the activate/
  navigate/popup bodies. The per-screen perform-switch is short and uniform but is retained
  deliberately: it keeps the mechanism (which varies) and the control flow visible at the call
  site, which the pure-classifier shape was chosen for over a callback dispatcher.
- The classifier is pure and total: it returns `ConfigKeyAction::None` for any key it does not
  own, so a screen can layer its hotkeys around it without the helper swallowing input. It
  never touches the model or the canvas, so it cannot drift the "snap on nav, not per-frame"
  selection contract (ADR-0104) or the band-only render contract (ADR-0096).
- Row kind is computed by the screen each keypress from its current selection, so a form with
  a navigation row (`evil_twin` row 0) or a popup row (`ble_bad_ble` Name) expresses that as a
  one-line kind function rather than a special-cased ENTER branch.
- This does not fold row kind into a declarative option model; that heavier refactor is
  explicitly out of scope and not deferred (it is rejected, not postponed).
