---
id: '0092'
title: "Canonical config-form keyboard interaction model"
type: architecture
status: accepted
date: 2026-10-20
supersedes: []
superseded_by: []
---

## Context

The attack/config screens let the operator set a handful of options on a vertical
list and then start the attack. After slice-0091 moved the last hand-rolled forms onto
the shared `ScrollableList` + `ListScrollModel`, every config form shares the same
*rendering* and *selection* substrate. The *input* layer never converged, though, and a
measurement on 2026-10-20 (post slice-0091) found four different keyboard dialects for
the same job:

- **arrow-edit** (`karma`, `evil_twin`, `ble_bad_ble`): `;`/`.` navigate (wrap), `,`/`<`
  and `/`/`>` edit the selected field left/right, and ENTER is **inert** on a value row
  (it only fires on the terminal START row).
- **ENTER-cycles** (`beacon_spam`, `probe_flood`, `deauth`): `;`/`.` navigate, ENTER
  cycles the selected field forward, and there is **no** left/right edit at all.
- **superset** (`ble_spam`): has both: left/right edit *and* ENTER advances the selected
  field (via a `return handleInput('/')` fallback) or activates the terminal row.
- **scanner-detour** (`handshake`): ENTER on the Target row routes to the WiFi scanner.

This drift has a concrete cost. On the migrated arrow-edit forms the operator reflexively
pressed ENTER to change a field (the universal "commit/activate" key), nothing happened,
and it read as a broken control (reported 2026-10-20, then self-corrected). The same
muscle memory is wrong or right depending only on which attack screen is open. There is no
recorded reason for the split; it is accreted history, not a decision. `ble_spam` already
demonstrates that one model serves every case, so the forms can converge on it without
losing any capability.

## Decision

All attack/config forms adopt one keyboard interaction model, the `ble_spam` superset:

1. **Navigate**: `;` moves the selection up, `.` moves it down. Both **wrap** at the ends
   (the `ListScrollModel` already does this).
2. **Edit**: `,` / `<` sets the selected field to its **previous** value; `/` / `>` sets
   it to its **next** value. For a two-state toggle, previous and next are the same flip.
   For a forward-only cycle, `,` steps **backward** through the same ring `/` steps
   forward through.
3. **ENTER performs the selected row's primary action**, by row kind:
   - **value row** -> advance to the next value (identical to `/`), so ENTER is never inert;
   - **terminal row** (the green `START X` action button) -> activate it;
   - **popup row** (e.g. evil_twin Fixed SSID) -> open its popup (identical to `/`);
   - **navigation row** (a Target that is picked on another screen) -> route to that
     screen (the WiFi scanner), matching `handshake`.
4. **Letter hotkeys stay** as per-screen shortcuts (e.g. `m`/`M` mode, `p`/`P` portal,
   `s`/`S` SSID). They are additive and do not change.

Row kind is a property of the row, not of the screen: a single screen may hold value
rows, a navigation row, and the terminal row at once, and ENTER dispatches on the kind of
the currently selected row. A navigation Target is a **selectable list row**, not a
header, so the operator reaches it by navigating to it like any other row.

**Exceptions**, recorded here so they are decisions and not silent drift:

- **`settings`** is out of scope. Its rows are heterogeneous typed controls (toggles,
  numeric steppers, sub-screens, actions), not a uniform value list, so the "edit
  left/right / ENTER advances" semantics do not map onto it. It keeps its per-row-type
  ENTER behaviour.
- **`handshake`** keeps its dynamic row count (the row set changes with capture mode) and
  its non-selectable info lines. On the one axis this ADR governs, ENTER semantics, it
  already **conforms**: its Target row is the canonical navigation row (ENTER -> scanner).

## Consequences

- The muscle memory that ENTER changes a field is now correct on every config form; the
  "broken control" confusion cannot recur.
- `karma`, `evil_twin`, `ble_bad_ble` gain the ENTER-advances fallback. `beacon_spam`,
  `probe_flood`, `deauth` gain left/right edit, which requires writing the **backward**
  step for each forward-only field cycle (the only non-mechanical part). `ble_spam` is the
  reference and is already conformant.
- `evil_twin` gains a selectable Target row that routes to the scanner (the capability the
  operator expected from memory). This reshapes its list: the Target moves from a header
  line into the scrollable list as row 0, shifting the existing option indices.
- ENTER now has a kind-dependent meaning. That is intentional and bounded to four kinds;
  it is the price of making ENTER never-inert while keeping a single terminal action and
  a single navigation affordance per form.
- New config forms have one model to follow and `ble_spam` to copy, instead of a coin
  flip between four dialects. A future shared `ConfigRow`/input-dispatch helper (deferred,
  see `LEDGER.md`) can encode this model in one place; this ADR is the spec it implements.
