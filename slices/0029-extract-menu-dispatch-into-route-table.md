---
id: '0029'
title: "Extract handleMenuAction()'s dispatch switch into a data-driven menu route table"
type: slice
status: accepted
date: 2026-09-16
supersedes: []
superseded_by: []
---

## Goal

Slice-0028 removed the last concrete-screen downcasts from `main.cpp`, which unblocked the
long-deferred `handleMenuAction()` extraction (LEDGER, Architecture). With the two Scanner/
Sniffer special cases gone, **every** arm of `handleMenuAction()`'s 26-arm `switch` is now the
same shape:

```
case ACTION_X:  Serial.println("...");  navigateToScreen(ScreenId::Y);  [stateMachine.transitionTo(Z);]  break;
```

The only per-arm variation is *which* `ScreenId`, *which* `AppState` (or none), and a
decorative log string. That is not control flow — it is a `{action → screen, state}` lookup
table wearing a `switch`. This slice replaces the switch with that table, co-located with the
menu catalogue (slice-0024 already put the action IDs in `menu_actions.h` and the item
builders in `menu_factory.cpp`), leaving `handleMenuAction()` a lookup + navigate + optional
transition.

Two dead-code items noticed while reading the switch are cleared in the same pass (rule 3,
boyscout):
- Six arms (`BLE_APPLE_ATTACK`, `BLE_BAD_BLE`, `USB_BADUSB`, `MOUSE_JIGGLER`, `BLE_SPOOF`,
  `SERVER`) call `stopAllAttacks()` a **second** time — `handleMenuAction()` already calls it
  unconditionally at the top. Duplicate calls with no effect (the first already left WiFi/BLE
  torn down); they vanish when the table drives a single top-of-function `stopAllAttacks()`.
- The `if (carouselMenu.isAnimating()) { /* empty */ }` block at the top of the handler has an
  empty body and a "just in case" comment — dead.

**Scope boundary.** This is the *menu-dispatch* half only. The *input-routing* half of the
Architecture item — `handleInput()` / `routeInputToActiveScreen()` into an `InputRouter` — is a
distinct concern with distinct coupling (`globalCanvas`, the menus, `sdManager`, the Fn+S
screenshot hotkey) and gets its own decision doc + commit (rule 5, one logical change per
commit). The `ScreenManager` push/pop API named in the LEDGER stays deferred: nothing pushes a
screen expecting to pop back (every exit goes to `MENU` via `returnToMenu()`), so building it
now would be speculative generality (rule 3). Both remain open LEDGER lines.

## Definition of Done

- **Given** the main menu and any selectable action
- **When** the operator picks it
- **Then** the same screen opens and the same `AppState` transition (or none) happens as before
  this slice — with `handleMenuAction()` containing **no** `switch` on the action id, only a
  `findMenuRoute()` lookup

- **Given** an unknown action id reaches `handleMenuAction()`
- **When** it is dispatched
- **Then** `findMenuRoute()` returns `nullptr` and the handler logs "Unknown action" and
  navigates nowhere — identical to the old `default:` arm

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green, including a new `test_menu_routes` regression suite asserting the
  route table maps representative actions to the right `{screen, transition, state}` and unknown
  ids to `nullptr`

- **Given** a full `pio run -e cardputer` build
- **When** it compiles and links
- **Then** it succeeds, `main.cpp` no longer holds the dispatch switch, and the six redundant
  `stopAllAttacks()` calls and the empty `isAnimating()` block are gone

## Design

### The table is data; `handleMenuAction()` becomes a lookup

A new display-free TU `ui/menu_routes.{h,cpp}` holds the mapping:

```
struct MenuRoute { MenuActionId action; ScreenId screen; bool transitions; AppState state; };
const MenuRoute* findMenuRoute(int actionId);   // linear scan, nullptr if unknown
```

`transitions` is explicit because `AppState` has no "no-op" member and inventing one would be a
lie (navigating to Captures/Settings/Radio/… genuinely performs *no* transition — the old
switch simply omitted `transitionTo` for those arms, leaving the state where `stopAllAttacks()`
left it). When `transitions == false`, `state` is unused (filled `IDLE` by convention). The
handler collapses to:

```
void handleMenuAction(int actionId) {
    Serial.printf("[Main] handleMenuAction: %d\n", actionId);
    stopAllAttacks();                                  // once, for every action (was already so)
    const MenuRoute* route = findMenuRoute(actionId);
    if (!route) { Serial.printf("[Main] Unknown action: %d\n", actionId); return; }
    navigateToScreen(route->screen);
    if (route->transitions) stateMachine.transitionTo(route->state);
}
```

`navigateToScreen`, `stopAllAttacks`, and `stateMachine` stay in `main.cpp` — they are the
composition-root's, not the table's. Only the *dispatch data* moves out.

### Why a new TU rather than folding it into `menu_factory.cpp`

The chosen home is "the menu catalogue", but `menu_factory.cpp` is display-coupled (it includes
`status_bar.h`, `generated_icons.h`, builds `MenuItem`/`CarouselItem` icons) and is therefore
**excluded from the native build** (`build_src_filter` has `-<ui/>`). Burying the route table
there would make it un-unit-testable. `ScreenId` (via `screen_interface.h` → `canvas_types.h`)
has a native stub, and `AppState` lives in native-compiled `core/`, so a *display-free*
`menu_routes.cpp` compiles natively. It is added to the native filter with a one-line
`+<ui/menu_routes.cpp>` exception exactly as `pulse_strip.cpp` already is (slice-0005) — the
same "display-free UI helper, so native-testable" precedent. This keeps the table beside the
catalogue **and** honours rule 3's regression-test mandate.

### Behaviour preservation

- **Navigation + transition** per action are copied 1:1 from the switch (see the table); the
  full enumeration is in As-built.
- **The single `stopAllAttacks()`** already ran for every action at the top of the old handler;
  the six arms that called it *again* were redundant (WiFi/BLE were already down). Removing the
  duplicates changes nothing observable.
- **Per-arm log strings** ("Starting Deauth Attack…") become one generic
  `[Main] handleMenuAction: <id>` line. Debug-serial cosmetics only; no functional change. The
  operator-facing on-device behaviour (screen + state) is unchanged.

### Testing

`findMenuRoute()` is the new native-testable seam. `test/test_menu_routes/` asserts: a scan
action → `{SCANNER, transition, SCANNING}`; an attack action → `{DEAUTH, transition,
ATTACKING}`; a browser action (`CAPTURES`) → navigates with **no** transition; a former
redundant-`stopAllAttacks` arm (`BLE_BAD_BLE`, `SERVER`) → navigates with no transition (guards
the boyscout removal not perturbing routing); and unknown/negative ids → `nullptr`. It fails
before this slice (the symbol does not exist) and passes after. The end-to-end menu→screen path
is hardware-bound (screens are M5/WiFi and not native-instantiable, as with 0026/0028) and is
verified on-device (deferred to the LEDGER).

## Verification

- Native: `pio test -e native` — full suite green, including the new `test_menu_routes` suite.
- Build: `pio run -e cardputer` — compiles and links a valid image; report RAM/Flash and the
  delta vs the 0028 baseline (Flash 2,467,819 B).
- Switch audit: `handleMenuAction()` in `src/main.cpp` contains no `switch` and no
  `case ACTION_`; `grep -c "stopAllAttacks()" src/main.cpp` drops by six from the removed
  duplicates.
- On-device (deferred to the LEDGER, as with slice-0026/0028): spot-check one action per state
  class — a scan (→ SCANNING), an attack (→ ATTACKING), and a browser/tool (Captures/Settings →
  no transition) — each opens the right screen; an unknown id is impossible from the menu but the
  `default` log path is unchanged.

## As-built

Shipped as designed. The route table (`ui/menu_routes.{h,cpp}`) holds all 26 actions copied
1:1 from the old switch; `findMenuRoute()` is a linear scan returning `nullptr` for unknown ids.
`handleMenuAction()` in `main.cpp` is now `stopAllAttacks()` → `findMenuRoute()` → navigate +
(if `route->transitions`) `transitionTo()`, replacing the 26-arm switch. `main.cpp` dropped
from 996 to 844 lines.

Boyscout, both cleared in the same commit:
- The six duplicate `stopAllAttacks()` calls (`BLE_APPLE_ATTACK`, `BLE_BAD_BLE`, `USB_BADUSB`,
  `MOUSE_JIGGLER`, `BLE_SPOOF`, `SERVER`) are gone — the single top-of-handler call already
  covered them.
- The empty `if (carouselMenu.isAnimating()) {}` block was removed.

`menu_routes.cpp` is added to the native `build_src_filter` (`+<ui/menu_routes.cpp>`) beside the
`pulse_strip.cpp` precedent, so the mapping is unit-tested natively even though the display-
coupled `menu_factory.cpp` cannot be.

Deferred as scoped (both remain open LEDGER lines, unchanged): the `InputRouter` extraction of
`handleInput()`/`routeInputToActiveScreen()` (own concern, own commit) and the `ScreenManager`
push/pop API (no consumer — speculative until one exists).

Verification:
- Native: `pio test -e native` **759/759** (was 753; +6 = the new `test/test_menu_routes/`
  suite, which fails to compile before this slice as `findMenuRoute` did not exist).
- Build: `pio run -e cardputer` **SUCCESS** (88 s). RAM 26.4% (86,632 B, flat vs 0028); Flash
  73.8% (**2,466,559 B**, **−1,260 B** vs the 0028 baseline of 2,467,819 B — the deleted switch
  boilerplate, net of the new table).
- Switch audit: `grep -c "case ACTION_\|switch (actionId)" src/main.cpp` → **0**;
  `handleMenuAction()` holds no `switch`. The dead `isAnimating()` block and the six redundant
  `stopAllAttacks()` calls are gone.
- Doc-kit: `lint-docs` ok (29 docs); `adr/INDEX.md` regenerated (`build-index --check` clean).
- On-device menu→screen spot-check is deferred to the LEDGER (screens are hardware-bound, as
  with slice-0026/0028).

A first background build attempt failed on `as: unrecognized option '--longcalls'` — the host
assembler leaking onto the WiFi/AsyncUDP *library* objects (the slice-0027 toolchain-env class),
unrelated to this diff; a clean re-run linked a valid image.
