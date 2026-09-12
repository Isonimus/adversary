---
id: '0021'
title: "Grey out SD-blocked menu entries when no card is present"
type: slice
status: accepted
date: 2026-09-12
supersedes: []
superseded_by: []
---

## Goal

When no SD card is mounted, the menu entries that are **useless without a card** must be
visibly greyed out and unselectable — so the operator sees the feature is unavailable
*before* opening it, not after. slice-0020 shipped the boot-level story (no freeze, a
no-card toast, a Settings → Retry SD Mount recovery path); this is the follow-up UX pass
that pre-empts the dead-end navigation the toast only explains after the fact.

Scope is deliberately narrow. Only entries that **cannot do anything** without a card are
greyed. Features that run live and merely can't persist stay fully enabled — greying a
working feature would lie to the operator about what the device can still do.

## Definition of Done

- **Given** no SD card inserted
- **When** the device boots
- **Then** exactly the SD-**blocked** entries are greyed and unselectable — **Captures**
  (Wireless, a browser over SD capture files), **BadBLE (HID)** (BLE, no built-in scripts —
  only SD `.txt`), and the **SERVER** root tile (serves the dashboard off the card) — while
  the greyed list entries are skipped in navigation and the SERVER tile reports "No SD card"
  on select

- **Given** no SD card inserted
- **When** the idle menu is shown
- **Then** every SD-**degraded** entry stays **enabled** (Sniffer, Handshake, Wardriving,
  Evil Twin, Karma, RFID, Radio, IR Record, Settings, Whitelist) — each runs live and only
  loses persistence, which is surfaced in-screen, not by greying the menu

- **Given** the operator inserts a card after a cardless boot
- **When** the Settings → Retry SD Mount action succeeds
- **Then** the blocked entries un-grey **without a reboot** — including the boot-frozen list
  submenu entries, via a menu rebuild on successful mount (slice-0020's no-reboot promise)

- **Given** an SD card **is** inserted
- **When** the device boots
- **Then** Captures, BadBLE, and SERVER are all enabled exactly as today — no regression

## Design

### What is "blocked" vs "degraded" (measured, not guessed)

A per-action survey of every menu leaf classified each as BLOCKED (useless without a card),
DEGRADED (runs live, can't persist), or INDEPENDENT (never touches SD). Only three are
BLOCKED:

| Entry | Evidence |
|---|---|
| Captures | `CaptureRegistry` reads handshakes/creds via `SD.open` (`capture_registry.h`); empty list without a card. |
| BadBLE (HID) | No built-in scripts; only SD `.txt` (`ble_bad_ble_screen.cpp`), shows "No scripts on SD." |
| SERVER | Dashboard files + `/api/files` served from SD (`server_manager.cpp`); no card = error page. |

Everything else that touches SD (Sniffer/Handshake pcap, Wardriving CSV, Evil Twin/Karma
credential logs, RFID dumps, Radio `.sub` files, IR save/replay, Settings/Whitelist
persistence) is DEGRADED: the live function works and only the write is unavailable. IR
Record is the weakest — its replay re-reads an SD file — but it still learns and displays a
decoded frame live, so it stays enabled and handles the missing card in-screen.

### The two menu layers gate differently

- **List submenu items** (`MenuItem`) are boot-frozen structs with a `disabled()` variant
  already used for `Wardriving (No GPS)` and `RFID (Module missing)`. Captures and BadBLE
  reuse that exact idiom: `SD ready ? action(...) : disabled("... (No SD)")`.
- **Carousel root tiles** (`CarouselItem`) already gate SERVER on `fileExists(...)` computed
  once at build time. That check requires SD I/O, so it must **not** move into the
  per-frame live `enabledFn` (which is reserved for cheap GPIO-detection globals like
  `g_rfidDetected`). It stays a build-time bool, with an SD-aware disabled reason.

### Un-grey without a reboot

Because both layers gate at menu-**build** time, the no-reboot promise is met by rebuilding
the menu when SD state changes. `initializeMenu()` is already a single, idempotent
menu-definition site; it is exposed via `core/module_detection.h` (which already declares
`redetectModules()`, the sibling main.cpp callback screens invoke) and called from the
Settings → Retry SD Mount handler after a successful mount. One rebuild recomputes all three
blocked entries at once — list items and the carousel tile — with no per-frame cost. This is
strictly better than a live `enabledFn` here: no SD access in the render loop, and one
source of truth for what the menu contains.

### Why not gate the degraded entries too

Greying an entry states "this does nothing." A live sniffer or handshake capture *does*
something without a card — it just can't save. Greying it would be a false claim and would
block a legitimate RAM-only session. The "can't persist" state is a per-screen concern
(surfaced there), not a menu-level one. This keeps the change honest and small.

## Verification

- Native: `pio test -e native` full suite green (no new pure logic to unit-test — the
  change is menu wiring; the SD predicate `isReady()` is existing, tested behavior).
- Build: `pio run -e cardputer` succeeds.
- On-device:
  - No-card boot → Captures, BadBLE greyed/unselectable; SERVER tile greyed; degraded
    entries enabled.
  - Settings → Retry SD Mount with a card inserted → all three un-grey without reboot.
  - Card-present boot → all three enabled (no regression).

## As-built

Shipped as designed. `initializeMenu()` was promoted from a global free function to a
`namespace adversary` member (declared in `core/module_detection.h` beside
`redetectModules()`) so the Settings → Retry SD Mount handler can trigger a rebuild; the
three blocked entries gate at build time (Captures/BadBLE via the `disabled()` idiom,
SERVER via an SD-aware `serverEnabled` + honest disabled reason). No new unit test — the
change is menu wiring over the existing `isReady()` predicate.

Verification:
- `pio run -e cardputer` SUCCESS (RAM 26.4%, Flash 73.8% — unchanged); `pio test -e native`
  749/749.
- On-device (Cardputer), all three scenarios confirmed by the operator:
  - No-card boot → Captures and BadBLE greyed/unselectable, SERVER tile greyed and reading
    "No SD card", degraded entries (Sniffer, Handshake, Wardriving, Radio, IR Record,
    Whitelist) all enabled.
  - Insert card → Settings → Retry SD Mount → all three un-greyed **without a reboot**.
  - Card-present boot → all three enabled, no regression.
