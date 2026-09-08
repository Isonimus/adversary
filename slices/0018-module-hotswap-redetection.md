---
id: '0018'
title: "Module hot-swap re-detection (Modules dashboard)"
type: slice
status: accepted
date: 2026-09-08
supersedes: []
superseded_by: []
---

## Goal

Let an operator attach a peripheral **after boot** and use it without a reboot. Today every
peripheral is probed exactly once in `setup()` and the verdict is cached for the life of the
boot: a cap seated into the expansion header, an RFID unit plugged into Grove, or a GPS
attached later are all invisible until the next power cycle (or a manual `capOverride`). This
slice adds an operator-driven **re-detection** and a **Modules** dashboard that shows, live,
what is currently attached — the natural home for the re-scan action and for the existing
`capOverride` shortcut.

## Scope

**Cardputer**, the three detectable peripheral classes we actually have:

| Class | Bus / pins | Boot probe today | Re-detect today |
|---|---|---|---|
| CC1101 / NRF24 cap | SPI, expansion header | version-reg read → `s_cap` static | none |
| RFID (WS1850S) | I2C `0x28`, Grove G1/G2 | `VersionReg` read → `m_detected` | none |
| GPS | UART, Grove G1/G2 **or** cap G13/G15 | NMEA carrier probe → `detected_` | **`tryRedetect()` exists** |

**In scope:** a `Modules` carousel entry showing each class present/absent; a **Re-scan**
action that re-runs detection on demand; live refresh of the carousel `enabled` gates so a
tile un-greys the instant its module is found; surfacing `capOverride` as a shortcut from the
dashboard. Re-detection is **on-demand only** — no background polling.

**Out of scope:** background/automatic hot-insert polling (rejected: idle bus traffic + a
task to reason about, for a gain the operator gets from one keypress); hot-swap of the SD
card; any new peripheral type. GPS keeps its own on-entry `tryRedetect()` in the wardriving/
handshake screens unchanged — this slice unifies the *manual* re-scan path, not GPS's
autonomous one.

## Definition of Done

- **Given** the firmware booted with a peripheral absent
- **When** the operator attaches it **while the bus is idle**, opens Modules, and hits Re-scan
- **Then** the dashboard shows it present, and its carousel tile (RADIO / RFID) un-greys —
  no reboot

- **Given** a peripheral present at boot that is later removed
- **When** the operator hits Re-scan after removal
- **Then** the dashboard shows it absent and the tile re-greys (detection is bidirectional,
  not latch-on-present)

- **Given** a cap re-probe (which borrows and desyncs the shared FSPI/SD bus)
- **When** the re-scan completes
- **Then** the SD bus is remounted and intact — no corrupted SD, no stuck radio (the
  pin-conflict invariant)

- **Given** RFID and Grove-GPS contend for the same G1/G2 pins
- **When** a re-scan runs
- **Then** it honors the same arbitration as boot (`gpsBlocksRfid`, and the cap-owns-G13/G15
  GPS skip) — it never probes RFID on a bus a Grove-GPS owns, or vice versa

## Design

### One shared re-detect routine — the sequence lives in exactly one place

The boot sequence in `main.cpp setup()` already encodes the pin arbitration: skip the cap-GPS
pin set when the multi-radio cap owns G13/G15 (`GPSManager::init(probeCapPort)`); skip RFID
when a Grove-GPS owns G1/G2 (`gps::gpsBlocksRfid`, already a pure, unit-tested predicate).
That arbitration is **correctness-critical** — a re-scan that probed RFID while a Grove-GPS
drove G1/G2 would fight a live UART driver. The safety-critical decisions are therefore
*already* in shared, tested units; what must not drift between boot and the dashboard is the
**probe order** (cap → GPS → RFID, because RFID gating depends on the resolved GPS source, and
the GPS cap-pin skip depends on the resolved cap).

So that order is captured once in a free function `redetectModules()` (declared in
`core/module_detection.h`, **defined in `main.cpp`**): it re-probes cap → GPS → RFID in order,
brackets the cap probe with the SD claim/re-mount the boot path uses, and updates the three
detection globals (`ui::g_expansionCap`, `g_gpsDetected`, `g_rfidDetected`). `setup()` calls
it at boot; the Modules screen's Re-scan calls the identical function. It lives in `main.cpp`
rather than a `core/*.cpp` on purpose: the native build compiles all of `core/` but excludes
`hal/expansion/expansion_cap.cpp`, so a `core` translation unit calling `detectExpansionCap()`
would fail to link natively — `main.cpp` (native-excluded, and already the owner of the
detection globals and the SD/GPS/RFID singletons) is the correct composition-root home.

### Re-scan is bus-safe because it is synchronous

The cap probe borrows the SD SPI bus and desyncs it — exactly the boot operation, which
remounts afterward. Run from the Modules screen the re-scan executes in the UI thread, so
nothing else touches SD or the radios during it; the physical insertion must already have
happened (bus idle), and the probe itself only reads registers, then remounts. This is why
re-detection is on-demand and screen-driven rather than a background task: the synchronous
context *is* the mutual exclusion. The operator contract is **insert while idle, then
Re-scan** — the same posture the SPI header's lack of hot-plug provisions forces regardless
of software.

### Each class's re-detect entry point

- **Cap** — no new function: `detectExpansionCap(override)` is already unguarded and re-probes
  `probeCC1101` / `probeNRF24` and overwrites `s_cap` on every call. `redetectModules()` just
  calls it again inside the SD claim/re-mount bracket (the same the boot path uses).
- **RFID** — `RFIDManager::init()` self-guards with `if (m_detected) return true`, so it
  already re-probes an *absent→present* insert, but cannot see a *present→absent* removal. A
  new `RFIDManager::redetect()` frees the reader, clears `m_detected`, then re-probes — making
  detection bidirectional (the DoD requires re-greying on removal). This is the one piece of
  new logic with a native regression test.
- **GPS** — `tryRedetect()` gains a `bool probeCapPort = true` parameter threaded to its
  `init()` calls; `redetectModules()` passes `probeCapPort = (cap != MultiRadio)` so a re-scan
  never drives the cap's GPS UART pins while the cap owns them. GPS stays *absent→present only*
  (its existing self-heal semantics — `tryRedetect` early-returns when already detected), which
  is why removal-regreying in the DoD is scoped to cap + RFID. The default `true` preserves the
  existing wardriving/handshake callers exactly; that those callers are cap-unaware is a
  pre-existing latent issue logged to the LEDGER, not touched here.

### The carousel `enabled` gates must become live, not boot-frozen

Today the carousel items are built once with `enabled` computed from the boot-time globals
(`g_rfidDetected`, `capPresent`); flipping a global after a re-scan would **not** un-grey the
tile, and `enabled` gates *five* sites (render greying ×2, next/prev skip ×2, select-block +
`disabledReason` toast). This is the one architectural change. `CarouselItem` gains an optional
`std::function<bool()> enabledFn` and a `bool isEnabled() const` that returns
`enabledFn ? enabledFn() : enabled`; all five sites consult `isEnabled()`. The RADIO / RFID /
MODULES tiles set `enabledFn` to read the live global, so they reflect current detection every
frame — no rebuild, no item-mutation API, and the re-scan simply updates the globals the
predicates read. The seven always-on tiles keep the plain `enabled` bool (the additive form
avoids rewriting them as `[]{return true;}`).

### The Modules dashboard

A new `ScreenId::MODULES` + carousel tile ("MODULES", `ICON_MODULES`), lazy-registered like
every other screen. The screen is a static list: one row per class showing name · bus ·
**present/absent** (green/grey), the cap row also showing the raw version byte from
`lastCapProbe()` (the existing diagnostic). Two actions: **Re-scan** (runs `redetectModules`,
then requests a redraw) and a **capOverride** shortcut (the same 0..2 enum already in
Settings → Wireless, surfaced here because this is where an operator forcing cap presence
would look). `capOverride` itself stays owned by the settings store; the dashboard only edits
the same field.

### The icon is a drop-in

`src/assets/icons/menu/icon-modules.bmp` already exists and is byte-identical in format (514
bytes, 48×48) to the nine shipped icons. Implementation emits its `const uint8_t ICON_MODULES[]`
array into `generated_icons.h` in the existing pattern — no new art, no new tooling.

### The host-testable piece: RFID bidirectional re-detect

The arbitration decisions are already pure and covered — `resolveExpansionCap()` and
`gpsBlocksRfid()` both have native tests (the latter pins exactly the G1/G2 RFID-vs-Grove-GPS
cases), so this slice does not duplicate them. The genuinely *new* testable logic is
`RFIDManager::redetect()` being **bidirectional**: `init()` short-circuits on `m_detected`, so
a naive re-call can never report a removal, and the DoD requires it. The MFRC522 native mock
gains a settable version byte (default `0x92`, so existing RFID tests are untouched) and
`test_rfid_manager` gets a case that inits present, flips the mock to an absent version, calls
`redetect()`, and asserts `isDetected()` is now false — which **fails before** the new method
(the guard keeps it detected) and **passes after**. The probe I/O, `redetectModules()`
orchestration, and the carousel render stay on-device verification, as with the CC1101 slices.

## Verification

- **Native:** `test_rfid_manager` gains the bidirectional `redetect()` regression (present →
  absent flips `isDetected()` false; re-detect while present stays true). Existing
  `gpsBlocksRfid` / `resolveExpansionCap` coverage stands unchanged.
- **Build:** `pio run -e cardputer` links the new screen, the shared `redetectModules()`, the
  RFID `redetect()`, the GPS `tryRedetect(probeCapPort)`, the live carousel predicates, and the
  new `ICON_MODULES` array.
- **On-device (the DoD, verified on hardware before commit):**
  1. Boot with the cap absent → RADIO tile grey; seat the cap while idle → Modules → Re-scan →
     tile un-greys and RADIO opens.
  2. Boot with cap present → remove → Re-scan → tile re-greys; confirm SD still reads/writes
     after each cap re-probe (bus intact).
  3. Boot with RFID absent → plug RFID into Grove → Re-scan → RFID row present, RFID tile
     un-greys.
  4. With a Grove-GPS attached, confirm a Re-scan does **not** disturb the GPS UART / never
     reports a phantom RFID on G1/G2.

## As built

_To be completed once the dashboard and re-scan are exercised on hardware against a real
seat/unseat of each peripheral class; tracked in the LEDGER until then._
