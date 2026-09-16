---
id: '0026'
title: "Dissolve the active-screen lifecycle downcasts in stopAllAttacks()"
type: slice
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Goal

`main.cpp`'s `stopAllAttacks()` reclaims the WiFi/BLE radio heap before switching screens.
To tear down the outgoing screen it **downcasts to four concrete screen types**:

- `static_cast<ScannerScreen*>(activeScr)->setActive(false)` (main.cpp:126)
- `static_cast<SnifferScreen*>(activeScr)->setActive(false)` (main.cpp:128)
- `static_cast<EvilTwinScreen*>(activeScr)->stop(); ...->forceStopPortal()` (main.cpp:137-139)
- `static_cast<KarmaScreen*>(activeScr)->stop(); ...->forceStopPortal()` (main.cpp:141-143)

These four downcasts force `main.cpp` to `#include` the four concrete screen headers purely
to name their types, and they are the reason slice-0024 deferred `handleMenuAction()` (the
"MenuController" half): extracting it would only relocate the four includes, not resolve
them.

Every *other* screen in `stopAllAttacks()` is already torn down polymorphically through
`IScreen::hide()` with no downcast. Bring the four holdouts onto that same path and delete
the downcasts, the type-specific branching, and the four includes.

This is **not** the "IAttack module taxonomy" the ledger originally framed. There is no
attack hierarchy to model here — `stopAllAttacks()` is screen-lifecycle cleanup, and the fix
is to finish routing it through the lifecycle hook the codebase already has. (See Design →
"Why not an IAttack interface".)

## Definition of Done

- **Given** the firmware builds for `cardputer`
- **When** `main.cpp` is compiled
- **Then** `stopAllAttacks()` contains **zero** `static_cast<...Screen*>` downcasts and no
  per-`ScreenId` cleanup branching — the outgoing-screen teardown is a single
  `if (activeScr) activeScr->hide();` — and `main.cpp` no longer includes
  `scanner_screen.h`, `sniffer_screen.h`, `evil_twin_screen.h`, or `karma_screen.h` on
  `stopAllAttacks()`'s behalf

- **Given** the Scanner, Sniffer, Evil Twin, or Karma screen is active and running
- **When** the operator triggers a screen switch that calls `stopAllAttacks()` (start a new
  module, or pick a per-network/per-packet action)
- **Then** the outgoing screen's radios and heap are released exactly as before — Scanner
  stops scanning + `deinit`s its AP list, Sniffer stops capture + flushes/closes PCAP, Evil
  Twin and Karma stop their portal (including `forceStopPortal()`) — because `hide()` now
  performs the full teardown for all four

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green (the touched code is embedded-only: `stopAllAttacks()` and the
  screen `hide()` overrides pull `WiFi`/`M5Unified`/BLE and are excluded from the native
  build — see Verification)

## Design

### The lifecycle hook already exists and already fires

`ScreenManager::setActiveScreen()` calls `activeScreen_->hide()` on the outgoing screen
before creating the next one (screen_manager.cpp:47, 63, 94), and `returnToMenu()` does the
same (screen_manager.cpp:165). **Every screen transition is already a polymorphic teardown of
the outgoing screen through `IScreen::hide()`** — the pure-virtual hook every screen
implements. `stopAllAttacks()` runs *before* each such transition to reclaim the shared radio
heap in a deterministic order; it is not a competing mechanism, it is a pre-transition
sweep.

### `hide()` is already a superset of the downcast method — for three of the four

Reading the four holdouts' teardown paths:

| Screen | `hide()` does | downcast in `stopAllAttacks()` does | gap |
|---|---|---|---|
| Scanner | `stopScan()` + unsubscribe + `m_scanner.deinit()` | `setActive(false)` → `stopScan()` | none — `setActive(false)` ⊂ `hide()` |
| Sniffer | `m_active=false` + `stopCapture()` + unsubscribe + `WiFi.mode(OFF)` | `setActive(false)` → same, minus unsubscribe/WiFi | none — `setActive(false)` ⊂ `hide()` |
| Evil Twin | unsubscribe + `stop()` (if running) | `stop()` + `forceStopPortal()` | `hide()` is missing `forceStopPortal()` |
| Karma | unsubscribe + `stop()` (if running) | `stop()` + `forceStopPortal()` | `hide()` is missing `forceStopPortal()` |

So Scanner and Sniffer need **no code change** — their downcast is pure redundancy with the
`hide()` the transition already calls. Evil Twin and Karma need one line each: fold
`forceStopPortal()` into `hide()` (right after the existing `stop()`), making `hide()` the
complete teardown. `forceStopPortal()` is idempotent (it delegates to
`evilTwin_/karma_.forceStopPortal()`, which no-ops when no portal is up), so calling it on a
normal back-to-menu exit is safe.

### The change

1. `EvilTwinScreen::hide()` and `KarmaScreen::hide()` gain a `forceStopPortal()` call after
   their `stop()`, so `hide()` is the full teardown for those screens too.
2. `stopAllAttacks()` replaces the whole per-`ScreenId` block (main.cpp:124-171 — the
   Scanner/Sniffer downcasts, the deauth/handshake/beacon/probe `hide()` branch, the Evil
   Twin/Karma downcasts, the IR/RFID/BLE/HID `hide()` branch, and the server/wardriving
   `hide()` branch) with a single `if (activeScr) activeScr->hide();`. The genuinely global
   steps stay verbatim and in the same relative order: BLE `forceRelease()` + `deinit()`,
   then the global WiFi reset, then the `delay(100)`.
3. `main.cpp` drops the four screen-header includes it kept only to name those types, and the
   `ble_spanner.h` "for stopAllAttacks()" comment loses its downcast justification (the
   include stays — `stopAllAttacks()` still calls `BLESpanner::getInstance()`).

### Ordering is preserved, and double-hide is already the norm

The current order is *per-screen teardown → global radio reset*, all before the transition.
The single polymorphic `hide()` keeps exactly that order: it runs first, the BLE/WiFi reset
runs after. Nothing is reordered.

The subsequent transition then calls `hide()` a second time on the same outgoing screen (via
`setActiveScreen`). That double-hide is **not new**: today `stopAllAttacks()` already calls
`hide()` for ~13 screens (deauth, handshake, beacon, probe, IR, RFID, the BLE peripherals,
server, wardriving) that the transition then hides again. Every `hide()` is written to be
idempotent — unsubscribes are guarded by `handlerId != 0`, `stop()` is guarded by
`isRunning()`, and `WiFi.mode(OFF)`/`deinit()` are safe to repeat. This slice brings the four
holdouts onto that same already-proven idempotent path; it does not invent a new one.

### Out of scope: the callback-wiring downcasts

`main.cpp` also downcasts to `ScannerScreen*`/`SnifferScreen*` at main.cpp:786, 845, and 851
to call `setOnNetworkAction()` / `setOnPacketAction()` / `show()`. Those are **domain
callback wiring**, not lifecycle — they attach Scanner→attack and Sniffer→attack handoff
behaviour that is specific to those two screens and has no polymorphic equivalent on
`IScreen`. No lifecycle interface removes them, so they are deliberately left alone; this
slice is scoped to the *lifecycle* downcasts only. (Removing those would need a different
mechanism — e.g. the screens publishing an EventBus action — and is not this slice.)

### Why not an IAttack interface

The ledger originally framed this as "add an `IAttack` base interface" over the modules in
`modules/attack/`. That framing does not fit the problem:

- `stopAllAttacks()` is not about attacks. It tears down whatever screen is active —
  scanner, sniffer, IR, RFID, BLE peripherals, server, wardriving — to reclaim radio heap.
  Most of those are not attacks.
- The teardown contract it needs (`hide()`) already exists on `IScreen` and every screen
  already implements it. A parallel `IAttack` hierarchy would be a second lifecycle interface
  competing with the one that already works — speculative generality the quality bar forbids.
- An `IAttack` interface would not even remove the callback-wiring downcasts above; those are
  Scanner/Sniffer-specific, not attack-generic.

So the item is reframed from "introduce IAttack" to "finish routing lifecycle teardown
through the `IScreen::hide()` hook that already exists", and closed by deletion of the
downcasts rather than by adding an abstraction.

### Not native-testable, and why that is fine

`stopAllAttacks()` and the four `hide()` overrides touch `WiFi`, `esp_wifi_*`, `M5Unified`,
and the BLE singletons; they compile only for the embedded targets and the native
`build_src_filter` excludes them. There is no pure function introduced — the change removes a
type-switch in favour of an existing polymorphic call. As with slice-0023 and slice-0024, the
proof is build + diff review of the removed branches + on-device confirmation that the four
screens still tear down correctly, not a unit test.

## Verification

- Native: `pio test -e native` — full suite green (no native source changed; `main.cpp` and
  the screen TUs are embedded-only).
- Build: `pio run -e cardputer` — compiles and links a valid ESP32-S3 image; report RAM/Flash
  vs. HEAD (expected flat to slightly smaller — a type-switch and four includes removed).
- Downcast audit: `grep -n 'static_cast<.*Screen\*>' src/main.cpp` shows the three
  callback-wiring sites (786/845/851) and **none** inside `stopAllAttacks()`.
- On-device (operator): for each of Scanner, Sniffer, Evil Twin, Karma — start the screen,
  begin its activity (scan / capture / portal), then switch away (start another module, or
  pick a per-network/per-packet action). Confirm the radio is released (next screen starts
  cleanly, no heap-exhaustion), Sniffer's PCAP is flushed/closed, and Evil Twin/Karma's portal
  actually stops. Confirm a normal back-to-menu from Evil Twin/Karma still exits cleanly now
  that `hide()` also calls `forceStopPortal()`.

## As-built

Shipped as designed.

- `EvilTwinScreen::hide()` (evil_twin_screen.cpp) and `KarmaScreen::hide()`
  (karma_screen.cpp) each gained a `forceStopPortal()` call after their existing `stop()`,
  making `hide()` the complete, idempotent teardown for those screens.
- `stopAllAttacks()` (main.cpp): the entire per-`ScreenId` block (four concrete downcasts +
  three `hide()` branches, ~48 lines) collapsed to
  `adversary::IScreen* activeScr = …getActiveScreen(); if (activeScr) activeScr->hide();`.
  The global BLE `forceRelease()`/`deinit()` and WiFi reset that follow are unchanged and in
  the same order.
- `main.cpp` dropped the `evil_twin_screen.h` and `karma_screen.h` includes (their types are
  no longer named anywhere in the TU). `scanner_screen.h`/`sniffer_screen.h` stay — still
  used by the out-of-scope Scanner/Sniffer action-callback wiring — with corrected comments.

Verification:
- Downcast audit: `grep 'static_cast<.*Screen\*>' src/main.cpp` → only the three
  callback-wiring sites (main.cpp:747/806/812); **zero** in `stopAllAttacks()`.
- Build: `pio run -e cardputer` **SUCCESS** — RAM 26.4% (86,632 B, unchanged), Flash 73.9%
  (2,468,807 B; **−188 B** vs. the 0025 baseline of 2,468,995 B — the removed type-switch and
  two includes). `main.cpp`/`evil_twin_screen.cpp`/`karma_screen.cpp` objects confirmed
  recompiled (mtimes post-edit), so the image reflects the change. (Build required slice-0027,
  which unblocked the `weaken_deauth` pre-hook.)
- Native: `pio test -e native` 752/752 (main.cpp + screen TUs are embedded-only, excluded
  from the native build).
- On-device: pending operator confirmation — for each of Scanner/Sniffer/Evil Twin/Karma,
  start + run its activity, switch away, confirm the radio/heap is released and Evil
  Twin/Karma's portal stops (including a plain back-to-menu now that `hide()` calls
  `forceStopPortal()`).
