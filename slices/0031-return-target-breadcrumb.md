---
id: '0031'
title: "Return to the originating list after an attack drill-down, instead of dumping to the root menu"
type: slice
status: accepted
date: 2026-09-16
supersedes: []
superseded_by: []
---

## Goal

Launching an attack from a **list** — Scanner or Sniffer → pick a target → Deauth / Handshake /
Evil Twin / Probe Flood / Karma — and then pressing ESC drops the operator at the **root menu**,
not back at the list they drilled in from. Every list→attack path behaves this way, because they
all funnel through one handler and one exit rule:

1. The list publishes `ATTACK_TARGET_SELECTED`; [`launchAttackTarget()`](../src/main.cpp) runs
   `stopAllAttacks()` (which `hide()`s the list) then `navigateWithParams(<attack>, …)`, and
   `ScreenManager::setActiveScreen()` **`delete`s the list** before the attack inits.
2. The attack's ESC sets its exit flag; `ScreenManager::returnToMenu()` unconditionally goes to
   `MENU`. The list instance is already gone, so there is nothing to return to.

This is the consumer that reverses the deferral recorded against the Architecture ledger's
`ScreenManager` push/pop line. That deferral's stated premise was *"every exit goes to `MENU`;
nothing pushes a screen expecting to pop back."* The list→attack drill-down **is** a push and the
operator **does** expect to pop back — the premise is false, so the line is reconsidered here.

Menu→attack launches (e.g. Deauth straight from the menu) exiting to `MENU` are **correct** — the
menu *is* where you came from — so this slice changes only the drill-down origin, not that path.

### Scope: a return-target breadcrumb, not an object stack

The deferred API was a true push/pop **object stack** that keeps parent screen *instances* alive.
That is rejected here on the No-PSRAM invariant (CLAUDE.md): the firmware deliberately holds **one**
screen resident at a time — the list is `delete`d before the attack inits — because the ~205 KB
internal SRAM is the whole budget. Keeping a 50-entry Scanner list resident *underneath* Evil Twin
or Karma (an AP + captive portal + web server — the most memory-hungry state, the reason
`SystemManager::prepareForMemoryIntensiveTask()` exists) risks OOM on exactly the attacks people
launch from a list. Navigation depth is also only ever 1 (menu → list → attack; nothing drills
deeper), so a general stack is speculative generality (rule 3).

Instead: remember only **where to return** — the originating `ScreenId` plus the `AppState` that
was interrupted — and rebuild that screen via its existing factory on exit. One screen stays
resident; the invariant holds. The `AppState` is part of the breadcrumb because it is load-bearing
(10+ modules read it): returning to the Scanner while the state machine is stuck in `ATTACKING`
would be a latent bug, so the exit restores the exact state the drill-down interrupted (`SCANNING`)
rather than hardcoding it or leaving it wrong.

## Definition of Done

- **Given** the operator drilled into an attack from the Scanner or Sniffer
- **When** they press ESC to exit the attack
- **Then** they land back on the originating list screen (freshly rebuilt via its factory) with the
  interrupted `AppState` restored (`SCANNING`), not on the root menu in `IDLE`

- **Given** the operator launched an attack straight from the menu (no list drill-down)
- **When** they press ESC to exit the attack
- **Then** they land on the root menu, exactly as today (the breadcrumb defaults to `MENU` when
  none was set)

- **Given** the operator returned to the list and then exits the list itself with ESC
- **When** the list's exit is handled
- **Then** they go to the root menu — the breadcrumb is one-shot, consumed on the first exit and
  not replayed on the next

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** a new `test_return_target` suite asserts the one-shot slot's set / consume-once /
  default-to-`{MENU, IDLE}` / clear semantics on the `{ScreenId, AppState}` pair — failing to
  compile before this slice (the type does not exist) and green after

## Design

### The breadcrumb: a pure one-shot slot

The silent-regression-prone part is not "remember an id" but the **consume-once** rule: forget to
clear the breadcrumb and the *next* normal exit wrongly bounces back into a list. That is a small
stateful decision worth testing in isolation, so it lives in a display-free, native-testable value
type rather than as a bare field buried in `ScreenManager`. The breadcrumb is a `{ScreenId,
AppState}` pair (see the state rationale above); "unset" is encoded as the default target `{MENU,
IDLE}`, so no separate armed-flag is needed:

```
namespace adversary {
struct ReturnTarget { ScreenId screen; AppState state; };

// One-shot "where to return on exit" breadcrumb. The default {MENU, IDLE} means "no drill-down":
// set() records an interrupted screen+state, consume() returns it exactly once then reverts to the
// default. No HAL dependency — native-testable.
class ReturnTargetSlot {
public:
    void set(ScreenId screen, AppState state) { target_ = {screen, state}; }
    ReturnTarget consume() { ReturnTarget t = target_; target_ = NO_TARGET; return t; }
    void clear() { target_ = NO_TARGET; }
private:
    // Named NO_TARGET, not DEFAULT: Arduino.h #defines DEFAULT as a macro (only bites on-device).
    static constexpr ReturnTarget NO_TARGET{ScreenId::MENU, AppState::IDLE};
    ReturnTarget target_ = NO_TARGET;
};
}
```

Home: `ui/return_target_slot.h` (header-only — a handful of trivial members need no TU, so the
native build reaches it through the test's `#include` with no `build_src_filter` entry). `ScreenId`
comes from `screens/screen_interface.h` and `AppState` from `core/state_machine.h`; both are already
native-includable (`menu_routes`, native-compiled since slice-0029, includes both), so no
include-guard surgery is needed this time — unlike slice-0030's `input_manager.h`.

### Wiring: capture at the drill-down, consult at the exit

- **Capture** — in `launchAttackTarget()`, before `stopAllAttacks()` tears the list down and before
  the `transitionTo(ATTACKING)`, record the current screen *and* current state as the return target:
  `screenMgr.setReturnTarget(screenMgr.getActiveScreenId(), stateMachine.getState());` (the active
  screen there is the Scanner or Sniffer in `SCANNING` — exactly what to restore).
- **Consult** — the exit path calls `consume()` and navigates to whatever it returns (`{MENU, IDLE}`
  by default, the `{list, SCANNING}` when a breadcrumb was set), restoring the state in both cases.
  Returning to a list re-creates it via its factory (fresh screen), which respects the
  single-resident-screen rule (the attack screen is `hide()`+`delete`d before the list is allocated).

The slot instance lives in `ScreenManager` (a `ReturnTargetSlot returnSlot_;` member with
`setReturnTarget(ScreenId)` and an exit that consults it), since `ScreenManager` already owns the
exit path and this **is** the reconsidered `ScreenManager` navigation API — just the breadcrumb
form, not the object-stack form.

### Boyscout: de-duplicate the exit dance (DRY)

The exit-to-menu sequence is copy-pasted at two sites in `main.cpp` — the loop's
`shouldReturnToMenu()` block and `routeInputToActiveScreen()` — each doing the identical four steps
(`returnToMenu()` + `navigateToScreen(MENU)` + `transitionTo(IDLE)` + `showMenu()`). The breadcrumb
must be honored at **both**, so this slice collapses them into one `exitActiveScreen()` helper that
consults the slot and routes to `MENU` *or* the recorded list. Two call sites, one rule — no third
place for the two to drift apart (rule of three is already exceeded at two identical copies + the
new behavior).

### Accepted tradeoff: the rebuilt list re-scans

The Scanner keeps no cached results (confirmed: no persistent store), so returning to it triggers a
fresh scan and loses scroll position / selection. That is accepted: landing back in the right screen
is a strict improvement over being dumped at the root menu, and it costs no resident memory.
Persisting the last scan to restore the list in place is a **follow-up** (logged to the LEDGER), not
part of this slice — it is a separate concern (result caching) with its own memory cost to weigh.

### Rejected alternative: object stack

Keeping parent screen instances alive (true push/pop) preserves list state instantly but holds N
screens resident — rejected on the No-PSRAM invariant above. The ledger's object-stack line is
closed on that basis; if a future need ever has a memory-cheap parent and a genuine multi-level
drill-down, it can be revisited with that consumer in hand.

## Verification

- Native: `pio test -e native` — full suite green, including a new `test_return_target` suite for
  the slot's set / consume-once / default-`MENU` semantics (fails to compile before this slice: the
  `ReturnTargetSlot` type does not exist).
- Build: `pio run -e cardputer` — compiles and links; report RAM/Flash vs the slice-0030 baseline
  (Flash 2,466,579 B).
- Seam audit: the exit-to-menu four-step sequence appears **once** in `main.cpp` (the new
  `exitActiveScreen()` helper), not duplicated across the loop and `routeInputToActiveScreen()`.
- On-device (deferred to the LEDGER, as with slice-0026/0028/0029/0030): from the Scanner, drill into
  Deauth/Handshake/Evil Twin/Probe Flood and ESC → back on the Scanner; same from the Sniffer; a
  menu-launched attack ESC → root menu; exiting the returned-to list ESC → root menu (breadcrumb not
  replayed).

## As-built

Shipped as designed. `ui/return_target_slot.h` (header-only) holds `ReturnTarget {ScreenId,
AppState}` and the one-shot `ReturnTargetSlot` (`set`/`consume`/`clear`, default `{MENU, IDLE}`).
`ScreenManager` owns a `returnSlot_` with inline `setReturnTarget()`/`consumeReturnTarget()`.
`launchAttackTarget()` arms it before `stopAllAttacks()`; the two duplicated exit sites in
`main.cpp` (the loop and `routeInputToActiveScreen()`) collapsed into one `exitActiveScreen()` that
consumes the breadcrumb and routes to `{MENU, IDLE}` (rebuild the menu, unchanged) or `{list,
SCANNING}` (rebuild the list via its factory, state restored).

The default constant is named `NO_TARGET`, not `DEFAULT`: `Arduino.h` `#define`s `DEFAULT` as a
macro, which broke the cardputer build (`could not convert '1' … to ReturnTarget`) though the native
build was clean — caught precisely because both targets are built. No `build_src_filter` entry was
needed: the type is header-only, reached through the test's `#include`.

Verification:
- Native: `pio test -e native` **772/772** (was 767; +5 = the new `test/test_return_target/` suite,
  confirmed by a filtered run — 5/5 PASSED: default, set→consume, consume-once, clear, overwrite; it
  does not compile before this slice as `ReturnTargetSlot` did not exist).
- Build: `pio run -e cardputer` **SUCCESS**. RAM 26.4% (86,640 B, +8 B vs 0030); Flash 73.8%
  (**2,466,663 B**, **+84 B** vs the 0030 baseline of 2,466,579 B — the helper + slot; negligible).
- Seam audit: `returnToMenu()` appears exactly **once** in `src/main.cpp` (inside
  `exitActiveScreen()`); both former exit sites are single `exitActiveScreen()` calls — the loop and
  `routeInputToActiveScreen()` can no longer drift.
- Doc-kit: `lint-docs` ok; `adr/INDEX.md` regenerated.
- On-device drill-down return spot-check is deferred to the LEDGER (input is hardware-bound, as with
  slice-0026/0028/0029/0030).
