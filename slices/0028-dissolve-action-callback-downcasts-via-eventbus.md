---
id: '0028'
title: "Dissolve the Scanner/Sniffer action-callback downcasts via an attack-target EventBus event"
type: slice
status: accepted
date: 2026-09-15
supersedes: []
superseded_by: []
---

## Goal

Slice-0026 removed the *lifecycle* downcasts from `stopAllAttacks()` by routing teardown
through `IScreen::hide()`. Two concrete-screen downcasts survived, both in
`handleMenuAction()` — **callback wiring**, not lifecycle:

- `ACTION_SCAN_NETWORKS` (`main.cpp:747`): `static_cast<ScannerScreen*>(...)` to call
  `setOnNetworkAction(...)` (+ `setActive(true)`).
- `ACTION_PACKET_SNIFFER` (`main.cpp:806/812`): `static_cast<SnifferScreen*>(...)` to call
  `show()` + `setOnPacketAction(...)`, and a second cast inside the packet callback to call
  `setActive(false)`.

Each set a **capture-less** lambda (`[]`) — pure navigation glue: "the operator chose an
action on a scanned network / sniffed packet → tear down, navigate to the matching attack
screen with the target's identity, go to ATTACKING". Because the lambdas capture nothing,
there is no per-invocation state and no reason they are re-wired on every menu entry; they
live in `main.cpp` only because that is where the wiring was written, and the concrete type
is not on `IScreen`, which forces the downcast.

This is the coupling the LEDGER flagged as blocking the `handleMenuAction()` extraction:
extracting the dispatcher as-is would relocate the two `static_cast`s into the new TU — a
lateral move. Dissolve them **first** (this slice) so the extraction (a later slice) is a
clean, uniform move.

Dissolution: the two screens **publish** the operator's choice on the existing EventBus
instead of invoking a main-supplied callback, and a single generic subscriber owns the
navigation. `handleMenuAction()`'s two special cases then collapse to the same
`navigate + transition` shape as the other 24, and both concrete-screen includes leave
`main.cpp`. This matches CLAUDE.md §3 (event-driven, decoupled screen↔navigation).

## Definition of Done

- **Given** the WiFi Scanner is open and the operator picks Deauth / Handshake / Evil Twin /
  Probe Flood on a selected network
- **When** the action is chosen
- **Then** the matching attack screen launches targeting that network (BSSID/SSID/channel),
  the app is in `ATTACKING`, and the Scanner's radio is released first — with **no**
  `static_cast<ScannerScreen*>` anywhere in `main.cpp`

- **Given** the Packet Sniffer is open and the operator picks Handshake Capture / Deauth /
  Evil Twin / Karma on a captured packet
- **When** the action is chosen
- **Then** the matching attack screen launches targeting that packet's source, the app is in
  `ATTACKING`, and the Sniffer's capture is stopped first — with **no**
  `static_cast<SnifferScreen*>` anywhere in `main.cpp`

- **Given** the native test env
- **When** `pio test -e native` runs
- **Then** the suite is green, including a new regression test that a published
  `ATTACK_TARGET_SELECTED` event delivers its `{targetScreen, bssid, ssid, channel}` payload
  intact to a subscriber

- **Given** a full `pio run -e cardputer` build
- **When** it compiles and links
- **Then** it succeeds, and `main.cpp` no longer includes `scanner_screen.h` /
  `sniffer_screen.h` (their only reason to be included — the two downcasts — is gone)

## Design

### One event serves both producers

Scanner and Sniffer select the *same kind of thing*: a target (a WiFi endpoint identified by
BSSID/SSID/channel) plus an attack to launch on it. So one event, not two:

- `EventType::ATTACK_TARGET_SELECTED` (Attacks range, `206`) — sits between selection and
  `ATTACK_STARTED (200)`.
- Payload `AttackTargetEventData { int16_t targetScreen; uint8_t bssid[6]; char ssid[33];
  uint8_t channel; }`. `targetScreen` holds a `ScreenId` value stored as `int16_t` so
  `event_data.h` (core) stays free of any UI header.

Two structurally-identical producers → one shared event/payload/subscriber. This clears the
rule-of-three bar (it is not speculative), and it is DRY: the navigation glue exists once.

### Each screen maps its own action → target ScreenId

The screens already own their on-screen action menus. Mapping a chosen action to the target
`ScreenId` is therefore co-located with the code that defines it, at the publish site:

- Scanner: the action menu is keyed by char (`D`/`H`/`T`/`P`/`I`); each attack key calls a
  local `publishAttackTarget(target, m_selectedNetwork)` helper — `D→DEAUTH`, `H→HANDSHAKE`,
  `T→EVIL_TWIN`, `P→PROBE_FLOOD`. `I` (Info) is not an attack: it stays fully in-screen (it
  already only logged the network; the old `main.cpp` `INFO` branch was a no-op) and publishes
  nothing. The now-unused `NetworkAction` enum is removed with the callback it served (see
  boyscout below).
- Sniffer: `executeAction(PacketAction)` maps `HANDSHAKE_CAPTURE→HANDSHAKE`,
  `DEAUTH_ATTACK→DEAUTH`, `EVIL_TWIN→EVIL_TWIN`, `KARMA_ATTACK→KARMA` and publishes.
  `COPY_BSSID` stays in-screen (logs the BSSID); `CANCEL` already early-returns. `PacketAction`
  stays — the sniffer uses it internally for its action menu (`m_availableActions`,
  `getActionName`).

`ScreenId` is already visible in both screens (each returns it from `getId()`).

### The navigator: one generic subscriber, zero per-attack knowledge

A single handler, subscribed once at `setup()`, owns what the two lambdas used to do:

```
onAttackTargetSelected(evt):
    stopAllAttacks();                         // tears down the *current* (scanner/sniffer) screen via hide()
    ScreenParams p{evt.bssid, evt.ssid, evt.channel};
    screenMgr.navigateWithParams((ScreenId)evt.targetScreen, p);
    stateMachine.transitionTo(ATTACKING);
```

It never names an attack — the target arrives in the event. This is exactly the shape a
future `ScreenManager` push/pop API and the extracted `MenuController` want. For this slice
it lives as a file-scope function in `main.cpp` registered in `setup()`; the later extraction
slice moves it into the controller unchanged.

### Ordering and teardown are preserved (behaviour-identical)

`EventBus::publish()` is **synchronous** (inline handler loop). The screen invokes the
action from its own input path (`ScannerScreen::handleAction` / `SnifferScreen::executeAction`),
so the subscriber runs on that same callstack — identical to today's direct callback. At that
moment the active screen is still the scanner/sniffer, so `stopAllAttacks()` →
`getActiveScreen()->hide()` tears *it* down before navigating:

- `ScannerScreen::hide()` → `stopScan()` + `m_scanner.deinit()`.
- `SnifferScreen::hide()` → `stopCapture()` + `WiFi.mode(OFF)`.

So the explicit `sniffer->setActive(false)` (old `main.cpp:812`) is subsumed by the same
`hide()` superset argument proven in slice-0026 — no separate stop call is needed. The
scanner network path already went through `stopAllAttacks()`; the sniffer packet path now
does too, unifying the two (and giving the sniffer→attack handoff the same full WiFi reset
the scanner→attack handoff already had). Neither screen's invoking method touches `this`
after the call, and the object is cached (hidden, not destroyed), so the synchronous
navigate-from-callback is as safe as it is today.

### Two entry pokes through the downcast were already redundant

Both extra calls `main.cpp` made through the downcast turn out to be dead weight the screens'
own `show()` already covers — deleting them (and the casts that reached them) loses nothing:

- `scanner->setActive(true)` (`main.cpp:797`) → `setActive(true)` is `startScan()`, but
  `ScannerScreen::show()` **already** calls `startScan()` at its end, and `navigateToScreen`
  already called `show()`. `startScan()` is idempotent (`if (m_scanning) return`), so the
  `main.cpp` call was a no-op re-entry. Just deleted — no scanner change needed for activation.
- `sniffer->show()` (`main.cpp:807`, the extra reset-to-STATS call) is likewise redundant:
  `navigateToScreen(SNIFFER)` already called `show()`, which already sets
  `m_screenState = STATS`. Just deleted.

This is the symmetric-lifecycle result of slice-0026 seen from the entry side: `show()`
already fully activates a screen, so nothing outside it needs to poke activation.

`handleMenuAction()`'s two cases then read exactly like their neighbours:
`navigateToScreen(...); stateMachine.transitionTo(...);`.

### Boyscout: remove dead callback machinery

With the wiring gone, the callback members are unused and deleted (rule 3, no dead code):
`ScannerScreen::{NetworkActionCallback, setOnNetworkAction, m_onNetworkAction}` and
`SnifferScreen::{PacketActionCallback, setOnPacketAction, m_onPacketAction}`, plus the
`NetworkAction` enum they carried (nothing else references it — the scanner's action menu is
char-keyed). The Scanner's **legacy** `NetworkSelectedCallback` / `setOnNetworkSelected` /
`m_onNetworkSelected` is also removed — a grep proves it was never wired from anywhere, so its
`else if (m_onNetworkSelected)` branch in `handleAction()` was permanently dead.

### Testing

The genuinely new, native-testable artifact is the event contract — the seam other code now
depends on. A regression test publishes `ATTACK_TARGET_SELECTED` with a distinctive
`{targetScreen, bssid, ssid, channel}` and asserts a subscriber receives every field intact
(the `targetScreen` field exercises the *new* union member, so it is not a duplicate of the
existing handshake-payload test). It fails before this slice (the event type and payload do
not exist) and passes after.

The end-to-end **select → correct attack launches on the right target** is an integration
behaviour: the Scanner/Sniffer screens are hardware-bound (M5/WiFi) and are not
native-instantiable — the same reason they have no native suite today. Its action→ScreenId
mapping is a small switch co-located with the action menu; extracting it into a separate
hardware-free TU purely to unit-test it would mean dragging the action enums out of the heavy
screen headers for two 4-case switches — indirection that costs more than it clarifies. So
that path is verified on-device (see Verification), consistent with slice-0026's posture.

## Verification

- Native: `pio test -e native` — full suite green, including the new
  `test_attack_target_payload_integrity` in `test/test_event_bus/`.
- Build: `pio run -e cardputer` — compiles and links a valid image; report RAM/Flash and the
  delta vs the 0027 baseline (Flash 2,468,807 B). Confirm `main.cpp` no longer includes the
  two screen headers and holds no `static_cast<ScannerScreen*>` / `static_cast<SnifferScreen*>`.
- Downcast audit: `grep -n "static_cast<adversary::\(Scanner\|Sniffer\)Screen\*>" src/main.cpp`
  returns nothing.
- On-device (deferred to the LEDGER, as with slice-0026): Scanner → each of Deauth/Handshake/
  Evil Twin/Probe Flood launches the right screen on the selected network and the scan radio
  is released; Sniffer → each of Handshake/Deauth/Evil Twin/Karma launches the right screen on
  the packet source and capture stops; Scanner INFO and Sniffer COPY_BSSID still behave
  in-screen.

## As-built

Shipped as designed, with two design notes corrected against the code during
implementation:

- **Scanner activation was already redundant, not relocated.** `ScannerScreen::show()`
  already calls `startScan()` (idempotent) at its end, and `navigateToScreen` already calls
  `show()` — so `main.cpp`'s `scanner->setActive(true)` was a no-op re-entry. It was simply
  deleted; no scanner-activation change was needed. (The doc's Design section was corrected
  from "relocate into show()" to "already redundant".)
- **`NetworkAction` was removed entirely.** Once the callback went, nothing referenced the
  enum (the scanner's action menu is char-keyed and the publish helper maps straight to
  `ScreenId`), so it was deleted as dead public API. `PacketAction` stays — the sniffer uses
  it internally.

Event contract: `EventType::ATTACK_TARGET_SELECTED = 206` + `AttackTargetEventData
{int16_t targetScreen; uint8_t bssid[6]; char ssid[33]; uint8_t channel;}` in the payload
union, with an `eventTypeToString` case. Producers: `ScannerScreen::handleAction` (a
file-static `publishAttackTarget(ScreenId, const NetworkInfo&)` helper for the four attack
keys; `INFO` publishes nothing) and `SnifferScreen::executeAction` (inline switch →
`ScreenId`, publishes; `COPY_BSSID` handled in-screen, `CANCEL` early-returns). Consumer:
`launchAttackTarget()` in `main.cpp`, a screen-agnostic navigator subscribed once in
`setup()`. Both `main.cpp` screen includes and both `static_cast<*Screen*>` are gone; the two
`handleMenuAction()` cases are now the uniform `navigateToScreen(...) + transitionTo(...)`.
Dead callback machinery removed: Scanner's `NetworkActionCallback`/`setOnNetworkAction`/
`m_onNetworkAction`, the never-wired legacy `NetworkSelectedCallback`/`setOnNetworkSelected`/
`m_onNetworkSelected`, and Sniffer's `PacketActionCallback`/`setOnPacketAction`/
`m_onPacketAction`.

A separate never-wired dead callback noticed in `sniffer_screen` —
`HandshakeCapturedCallback`/`setOnHandshakeCaptured`/`m_onHandshakeCaptured` (invoked at
`sniffer_screen.cpp:635` but `setOnHandshakeCaptured` has no caller anywhere) — was left out
of this commit to keep it atomic and logged to the LEDGER instead.

Verification:
- Native: `pio test -e native` **753/753** (was 752; +1 = `test_attack_target_payload_integrity`
  in `test/test_event_bus/`, which fails to compile before this slice as the event type and
  payload did not exist).
- Build: `pio run -e cardputer` **SUCCESS** (43 s) — pre-hook weakened the symbol, linked a
  valid ESP32-S3 image. RAM 26.4% (86,632 B, flat vs 0027); Flash 73.8% (**2,467,819 B**,
  **−988 B** vs the 0027 baseline of 2,468,807 B — the two inline lambda blocks left main.cpp).
- Downcast audit: `grep -n "static_cast<adversary::\(Scanner\|Sniffer\)Screen\*>" src/main.cpp`
  and a grep for every removed symbol both return nothing; `main.cpp` no longer includes
  `scanner_screen.h` / `sniffer_screen.h`.
- On-device select→launch is deferred to the LEDGER (screens are hardware-bound), as with
  slice-0026.
