---
id: '0030'
title: "Extract the pure key-normalization out of handleInput() into a native-testable keymap"
type: slice
status: accepted
date: 2026-09-16
supersedes: []
superseded_by: []
---

## Goal

The Architecture ledger's remaining "extract `main.cpp` into managers" item names an
`InputRouter` for `handleInput()`/`routeInputToActiveScreen()`. Reading those two functions
(`src/main.cpp`) shows they are **not** one concern: they interleave

1. **reading** raw platform input (`M5Cardputer.Keyboard` / M5Stick `Input()`, behind `#ifdef`),
2. **normalizing** it to a routing `char` — enter→`'\n'`, del→`0x08` on Cardputer; an
   `InputAction → char` switch on M5Stick,
3. **intercepting** the Fn+S screenshot hotkey (`globalCanvas`, `sdManager`, `saveScreenshot`),
4. **routing** — on a screen: `ScreenManager::handleInput` + exit-to-menu; on the menu:
   `mainMenu`/`carouselMenu` + re-render.

Steps 1/3/4 are single-caller orchestration bound to hardware singletons; wrapping them in an
`InputRouter` class would **relocate** that coupling, not make it testable, and there is exactly
one caller — so a class there is pure relocation with no test payoff (rule 3). **Step 2 is the
only pure logic**, and it is exactly the silent-regression kind (swap `LEFT`/`RIGHT`, break
del): a lookup wearing control flow, just like slice-0029's dispatch switch.

This slice extracts **only** that pure normalization into a display-free, native-testable TU and
leaves the orchestration in `main.cpp`. It mirrors slice-0029 (extract the data/pure-logic seam;
keep the composition root in `main.cpp`) rather than building a speculative class.

## Definition of Done

- **Given** the Cardputer key path
- **When** Enter, Del, or a printable key is pressed
- **Then** `normalizeCardputerKey()` returns `'\n'`, `0x08`, and the character respectively —
  identical to the old inline `if (state.enter)…` chain — and `handleInput()` calls it instead
  of that chain

- **Given** the M5Stick gesture path
- **When** an `InputAction` arrives
- **Then** `inputActionToKey()` maps it to the same char the old `switch` produced
  (`UP`/`LEFT`→`';'`, `DOWN`/`RIGHT`→`'.'`, `SELECT`→`'\n'`, `BACK`→`` '`' ``, else `'\0'`), and
  `handleInput()` calls it instead of that switch

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green, including a new `test_input_keymap` suite asserting both mappings
  including the fall-through (`'\0'`) and no-key (`0`) cases — a suite that fails to compile
  before this slice (the symbols do not exist)

- **Given** a full `pio run -e cardputer` build
- **When** it compiles and links
- **Then** it succeeds and the Fn+S hotkey, screen routing, and menu handling in `handleInput()`
  are behaviourally unchanged (only the key-derivation lines now delegate to the keymap)

## Design

### The seam: two pure functions

A new display-free TU `hal/input/input_keymap.{h,cpp}` holds the normalization:

```
namespace adversary {
// Cardputer: derive the routing char from the modifier state + the pressed key.
// enter -> '\n', del -> 0x08 (backspace), otherwise the pressed char (may be 0 = "no key").
char normalizeCardputerKey(bool enter, bool del, char pressedKey);

// M5Stick: map a logical InputAction to the char the router/menus consume.
// '\0' for actions with no char (NONE and any unmapped action) -> caller ignores it.
char inputActionToKey(InputAction action);
}
```

`handleInput()` keeps steps 1/3/4 verbatim; only the char-derivation collapses to a call:

```
// Cardputer branch
char key = adversary::normalizeCardputerKey(state.enter, state.del, pressedKey);
if (key == 0) return;
...
// M5Stick branch
char pressedKey = adversary::inputActionToKey(action);
if (pressedKey == '\0') return;
```

### Home and native reach

`hal/input/` is the cohesive home — beside `input_manager.h`, which already owns the canonical
`InputAction` enum (reused, not redefined — DRY). It compiles natively with a one-line
`+<hal/input/input_keymap.cpp>` filter exception, the same "display-free / HAL-free helper is
native-testable" precedent as `pulse_strip.cpp` (slice-0005) and `menu_routes.cpp` (slice-0029).
`core/` was rejected: putting a HAL-enum consumer there would invert the core←hal layering.

`input_manager.h` was not includable natively: its `#else` arm (the "some other M5 target" case,
which the native build also falls into) unconditionally `#include <M5Unified.h>`. The class's own
M5 members are already `#ifdef ESP32`-guarded, so that include is the **only** native blocker.
Guarding it `#elif !defined(NATIVE_BUILD)` lets the platform-agnostic enum be included off-device
while on-device generic-M5 builds still get `M5Unified.h`. Boyscout (rule 3): the header was
labelled "platform-agnostic" but could not be included off the target.

### Behaviour preservation

Both mappings are copied 1:1 from the inline code; `handleInput()`'s read/Fn+S/route logic is
untouched. The Cardputer `if (key == 0) return` and M5Stick `if (pressedKey == '\0') return`
guards stay in `handleInput()` (they are control flow, not mapping), so `normalizeCardputerKey`
returning `0` for "no printable key" and `inputActionToKey` returning `'\0'` for unmapped actions
preserve the exact early-return behaviour.

### Why not the full InputRouter class

Deliberately out of scope (operator decision, this slice): steps 1/3/4 have no second caller and
no pure logic, so a class would relocate hardware coupling without buying a test. The ledger's
`InputRouter` line is closed as "no testable payoff — pure relocation"; the `ScreenManager`
push/pop line stays deferred (still no consumer). If a real second input driver or an input-replay
need appears later, the orchestration extraction can be revisited with an actual consumer.

## Verification

- Native: `pio test -e native` — full suite green, including the new `test_input_keymap` suite
  (fails to compile before this slice: `normalizeCardputerKey`/`inputActionToKey` do not exist).
- Build: `pio run -e cardputer` — compiles and links a valid image; report RAM/Flash vs the
  slice-0029 baseline (Flash 2,466,559 B).
- Seam audit: `handleInput()` in `src/main.cpp` no longer contains the inline
  `if (state.enter)…else if (state.del)…` chain nor the `switch (action)` char mapping; both are
  single calls into `hal/input/input_keymap`.
- On-device (deferred to the LEDGER, as with slice-0026/0028/0029): a Cardputer key, Enter, Del,
  and Fn+S each behave as before; M5Stick gestures navigate as before.

## As-built

Shipped as designed. `hal/input/input_keymap.{h,cpp}` holds the two pure functions;
`normalizeCardputerKey()` is enter→`'\n'` / del→`0x08` / else the pressed char, and
`inputActionToKey()` is the M5Stick `switch` copied 1:1 (`'\0'` default). `handleInput()`'s two
inline derivations collapsed to single calls, both early-return guards (`key == 0`,
`pressedKey == '\0'`) left in place. `input_manager.h`'s generic-M5 `<M5Unified.h>` include is now
guarded `#elif !defined(NATIVE_BUILD)` so the enum is includable natively (its class members were
already `#ifdef ESP32`-guarded). `+<hal/input/input_keymap.cpp>` added to the native filter beside
the `pulse_strip.cpp`/`menu_routes.cpp` precedents.

The full `InputRouter` class was **not** built (operator decision): steps 1/3/4 of `handleInput()`
have one caller and no pure logic, so a class would relocate hardware coupling without a test
payoff. The ledger's `InputRouter` line is closed on that basis; `ScreenManager` push/pop stays
deferred (still no consumer).

Verification:
- Native: `pio test -e native` **767/767** (was 759; +8 = the new `test/test_input_keymap/` suite,
  confirmed by a filtered `-f test_input_keymap` run — 8/8 PASSED; it does not compile before this
  slice as the symbols did not exist).
- Build: `pio run -e cardputer` **SUCCESS** (187 s). RAM 26.4% (86,632 B, flat vs 0029); Flash
  73.8% (**2,466,579 B**, **+20 B** vs the 0029 baseline of 2,466,559 B — the out-of-line functions
  vs the inlined derivations; negligible).
- Seam audit: `grep -c "if (state.enter)" src/main.cpp` → **0**; `grep -c "case InputAction::UP:"
  src/main.cpp` → **0**; both derivations are single `normalizeCardputerKey()`/`inputActionToKey()`
  calls.
- Doc-kit: `lint-docs` ok (30 docs); `adr/INDEX.md` regenerated (`build-index` clean).
- On-device Cardputer/M5Stick input spot-check is deferred to the LEDGER (input is hardware-bound,
  as with slice-0026/0028/0029).
