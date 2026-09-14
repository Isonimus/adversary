---
id: '0023'
title: "Extract the screen->factory table out of main.cpp into a screen registry"
type: slice
status: accepted
date: 2026-09-14
supersedes: []
superseded_by: []
---

## Goal

`main.cpp` `#include`d ~30 concrete screen headers for one reason only: it built the
screen->factory table inline in `setup()`, so it named every screen class. That coupling is
the first, most-isolable slice of the standing "extract main.cpp into managers" architecture
item — the entry point should orchestrate, not enumerate the screen catalogue.

Move the whole `registerFactory(...)` table into a dedicated translation unit,
`screen_registry.cpp`, exposed through a one-function header. After this, `main.cpp` includes
only the handful of screens it genuinely constructs or downcasts itself, and calls
`registerAllScreens()` once.

Scope is deliberately narrow: **only** the registration table moves. The remaining
`main.cpp`->screen coupling (the concrete-type downcasts in `stopAllAttacks()` and the
scanner/sniffer action wiring) is a *different* concern — it needs an `IAttack`/polymorphic
lifecycle, which is its own open ledger item — and is left untouched here.

## Definition of Done

- **Given** the firmware builds for `cardputer`
- **When** `main.cpp` is compiled
- **Then** it `#include`s no screen header except the five it uses directly
  (`scanner_screen`, `sniffer_screen`, `evil_twin_screen`, `karma_screen`, `splash_screen`),
  and contains **zero** `registerFactory` calls

- **Given** the screen registry
- **When** `registerAllScreens()` runs at boot
- **Then** it registers exactly the same 28 `ScreenId` factories that the old inline block
  did — a set-equality check against `HEAD`, no target added or dropped

- **Given** any cross-screen navigation (menu action, or the Scanner/Sniffer sideways jumps
  to Deauth/Handshake/Evil-Twin/Probe-Flood via `navigateWithParams`)
- **When** the operator triggers it
- **Then** it resolves through the factory table exactly as before — those paths key off
  `ScreenId` + the base `IScreen::ScreenParams`, never a concrete screen type, so relocating
  the factories cannot break them

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green and the registry TU is **not** compiled (it lives under
  `ui/`, which the native `build_src_filter` excludes)

## Design

### Explicit registry, not per-screen self-registration

The tempting "pure" form is per-screen self-registration: a `REGISTER_SCREEN` macro drops a
file-scope static registrar into each screen `.cpp`, so adding a screen touches only that
screen and `main.cpp` includes nothing. It is rejected here.

The ESP32 Arduino build links with `-ffunction-sections -fdata-sections -Wl,--gc-sections`.
A screen `.cpp` whose only outward reference is a self-registering static object is reachable
from nobody, so the linker is entitled to garbage-collect it — and the screen then **silently
never registers**, surfacing on device as a dead menu entry. Defending against that
(`KEEP`/whole-archive, or a forced-reference table) reintroduces exactly the central list
self-registration was meant to remove. It also adds static-initialisation-order fragility on
a no-PSRAM target. A silently-dropped screen also violates the project's fail-loud bar: the
failure is invisible until someone opens the menu on hardware.

The explicit registry keeps the one benefit that matters (the entry point is decoupled from
the catalogue) while a missing registration stays a **compile-time** error in one obvious
file. Adding a screen edits one dedicated file whose sole job is the table — not `main.cpp`.

### What moves and what stays

`main.cpp` keeps five screen headers because it uses those types directly, and this slice
deliberately does not change that:

- `stopAllAttacks()` downcasts the active screen to `ScannerScreen`/`SnifferScreen`
  (`setActive(false)`) and to `EvilTwinScreen`/`KarmaScreen` (`stop()` + `forceStopPortal()`)
  — concrete methods not on `IScreen`.
- The Scanner/Sniffer action-callback wiring in `handleMenuAction()` casts the active screen
  to set its `setOnNetworkAction`/`setOnPacketAction` callback.
- `splashScreen` is a file-scope `SplashScreen` instance.

Removing those five needs the polymorphic-lifecycle refactor tracked separately (the
`IAttack` base-interface item), so it is out of scope. Everything else — 28 factories across
~25 headers — moves to `screen_registry.cpp`.

### The one behavioural subtlety: the Settings callback

The Settings factory wires `setOnSavedNetworksRequested` to jump to Saved Networks. Inline in
`main.cpp` it called the `navigateToScreen()` free function (which sets the manager's active
screen **and** writes `main.cpp`'s local `currentScreen` mirror). The registry TU cannot see
that free function. It calls `ScreenManager::getInstance().setActiveScreen(SAVED_NETWORKS)`
directly instead — which is behaviour-identical here: `loop()` handles input first, then
re-syncs `currentScreen = sm.getActiveScreenId()` before the update/render of the same
iteration, so the mirror is refreshed regardless. No new coupling, no behaviour change.

## Verification

- Native: `pio test -e native` full suite green (no new pure logic — this is a relocation;
  the factory lambdas are unchanged). Registry TU excluded from the native build by
  `-<ui/>`.
- Build: `pio run -e cardputer` compiles and links (the real arbiter — the registry TU is
  compiled only for the embedded targets).
- Set-equality: the registered `ScreenId` set diffed against `HEAD` is identical (28 == 28).
- On-device (operator): open Scanner, act on a network -> Deauth/Handshake/Evil-Twin/
  Probe-Flood all still launch; open a spread of menu screens; confirm Settings -> Saved
  Networks still jumps. A screen that failed to register would be a dead menu entry.

## As-built

Shipped as designed. New `src/ui/screen_registry.{h,cpp}`; `registerAllScreens(ScreenManager&)`
holds the 28-factory table verbatim from the old `setup()` block, with the Settings callback
switched from `navigateToScreen()` to `setActiveScreen()` as described. `main.cpp` dropped 25
screen `#include`s (kept the 5 it downcasts), replaced the inline block with one
`adversary::registerAllScreens(screenMgr)` call, and had a stale "22 screens" comment
corrected. The residual `main.cpp`->screen coupling is left for the `IAttack` lifecycle item.

One transitive dependency surfaced and was fixed at root cause: `stopAllAttacks()` calls
`BLESpanner::forceRelease()` but had been getting the `BLESpanner` declaration only through
the (now-removed) BLE screen headers. `main.cpp` now `#include`s `modules/ble/ble_spanner.h`
directly — it includes what it uses. An audit of every other module symbol `main.cpp`
references confirmed the rest resolve through headers it still includes directly
(`screen_manager.h` for `IScreen`/`ScreenId`/`ScreenManager`, `theme.h` for `ThemeManager`).

Verification:
- `pio run -e cardputer` SUCCESS — compiles, links, and packages a valid ESP32S3 image.
  RAM 26.4% (86,632 B), Flash 73.9% (2,468,791 B).
- `pio test -e native` 752/752 (registry TU excluded from the native build by `-<ui/>`; no
  native source changed).
- Registered `ScreenId` set diffed against `HEAD`: identical (28 == 28).
- On-device (Cardputer, 2026-09-14): confirmed by the operator — Scanner → act on a network
  → Deauth/Handshake/Evil-Twin/Probe-Flood all launch; Settings → Saved Networks jumps; a
  spread of screens (BadUSB, RFID, IR, Radio, Server, Captures, …) open with no dead-ends.
  No screen failed to register.
