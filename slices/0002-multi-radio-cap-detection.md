---
id: '0002'
title: "Multi-radio cap detection, override, and carousel gating"
type: slice
status: accepted
date: 2026-09-02
supersedes: []
superseded_by: []
---

## Goal

Bring up the CC1101 + NRF24L01 + IR expansion cap far enough to be *recognised and
surfaced*, without yet implementing any RF attack. Closes the ledger item "Multi-radio cap
(CC1101 + NRF24L01 + IR) integration" for its detection half: an SPI-probe cap
auto-detect with an NVS manual override, a `Radio` carousel entry (`icon-radio.bmp`)
gated on that detection, and a hard guard that the cap-GPS pin-set probe never runs when
the multi-radio cap is present (`G13/G15` are the cap's `CC1101 GDO0 / CC1101 CS`, not a
UART). The pre-SD chip-select de-select that keeps the SD card mountable for *either* cap
is a precondition (ADR-0001) and already sits in the working tree; this slice makes it
load-bearing by giving the code a reason to know which cap is attached. Actual radio
features — CC1101 OOK capture/replay, NRF24 scan/jam, cap-IR — are out of scope and each
earn their own later slice; this slice is the shared foundation they build on.

## Definition of Done

- **Given** a multi-radio cap seated and the cap-override setting on `Auto`
- **When** the device boots and probes the SPI bus after the SD mount
- **Then** `detectExpansionCap()` returns `MultiRadio`, and the `Radio` carousel entry is
  present and selectable

- **Given** no cap (or a LoRa cap) seated and the override on `Auto`
- **When** the device boots
- **Then** `detectExpansionCap()` returns `None`, the `Radio` carousel entry is absent, and
  the SD card still mounts with no CRC error in the boot log

- **Given** the cap-override setting forced to `MultiRadio` in Settings
- **When** the device boots with the SPI probe failing (e.g. a re-seated cap)
- **Then** the resolved cap is `MultiRadio` and the `Radio` entry appears — the manual
  override wins over a failed probe

- **Given** the multi-radio cap is the resolved cap
- **When** any cap-GPS detection path is reached
- **Then** it is skipped (not probed on `G13/G15`), because those pins are the CC1101
  control lines under this cap

## Design

**What exists and is reused.** The pre-SD de-select in `setup()` (`main.cpp`) and
`SDManager::init()` already drives `LORA_NSS` HIGH and holds `CC1101_CS`/`NRF24_CS` at
`INPUT_PULLUP` before every mount (staged, uncommitted) — this slice does not touch bus
timing, it consumes it. Pin numbers live in `src/config/pins.h` (`CC1101_CS=15`,
`CC1101_GDO0=13`, `NRF24_CS=4`, `NRF24_CE=3`, `CAP_IR_TX=6`, `CAP_IR_RX=5`). Settings
persistence reuses `SettingsManager` (`src/modules/storage/settings_manager.h`) — the
override is one new field on `WirelessSettings`, not a new store. The carousel reuses
`CarouselMenu` / `CarouselItem` (`src/ui/components/carousel_menu.h`); a `CarouselItem`'s
`enabled`/visibility is already how entries gate, so the `Radio` item is filtered at
list-build time, not a new mechanism. The GPS probe reuses the existing `g_gpsDetected`
gating idiom in `main.cpp` (the RFID-skip-when-GPS pattern) rather than inventing a new
one.

**What is deliberately NOT reused.** No `HardwareManager` capabilities singleton is
introduced here — that is a separate deferred ledger item, and folding cap detection into
a not-yet-existing abstraction now is the speculative generality the quality bar forbids.
Cap detection lands as a small free function + enum next to the pins it reads, and moves
into `HardwareManager` if and when that slice happens.

**Detection.** A pure decision function keeps the hardware-free logic testable:

```
enum class ExpansionCap : uint8_t { None, MultiRadio };   // LoRa RF shelved (ADR-0001)
enum class CapOverride  : uint8_t { Auto, ForceNone, ForceMultiRadio };

ExpansionCap resolveExpansionCap(bool cc1101Present, bool nrf24Present, CapOverride ov);
```

Precedence: a non-`Auto` override wins outright (needed because a half-seated cap probes
falsely, and because an operator swapping caps wants a deterministic force). On `Auto`,
`MultiRadio` iff either radio answers. The two probes are thin SPI reads behind the
SD-safe bus, run *after* the SD mount so they never race the de-select:

- **CC1101** has a readable `VERSION` status register (`0x31` with the burst/status read
  bits set); a seated part returns a fixed silicon revision. The exact value is a
  hardware fact to confirm on-device and record in `## As built` — the decision function
  takes the boolean "matched the expected revision", not the raw byte, so the truth table
  is testable without a radio.
- **NRF24L01** has no ID register; detect by writing a known pattern to a R/W register
  (`RF_CH`) and reading it back — present iff it reads back. Same boolean contract.

**Override UI.** One entry on the Settings screen (`Cap: Auto / None / Multi-Radio`)
bound to the new `WirelessSettings` field, persisted by `SettingsManager`.

**Carousel + icon.** `icon-radio.bmp` (already added under
`src/assets/icons/menu/`) is converted to an `ICON_RADIO[]` array in
`src/assets/generated_icons.h` in the same 1-bpp BMP-byte format as the sibling icons, and
the `Radio` `CarouselItem` is appended only when the resolved cap is `MultiRadio`. Its
action opens a placeholder Radio screen that lists the detected radios; the attack actions
are later slices.

**GPS guard.** The cap-GPS probe (ATGM336H on `G13/G15`, a LoRa-cap-only peripheral) is
guarded by `resolveExpansionCap(...) != MultiRadio`, so it is never driven onto the
CC1101 control lines. The Grove-port AT6668 GPS (`G1/G2`) is unaffected and keeps its
existing detection path.

## Verification

- **Native unit test** `test/test_expansion_cap.cpp` (env `native`) exercises
  `resolveExpansionCap()` across the full truth table: every `{cc1101Present, nrf24Present}`
  pair under each `CapOverride`, asserting override precedence over a failed probe and
  `Auto` returning `MultiRadio` iff either radio answers. This is the fail-before/pass-after
  regression for the decision logic and the proof for DoD scenarios 1, 3, and the
  `Auto`→`None` half of 2.
- **On-device (operator checklist, recorded in `## As built`)** — the scenarios that cross
  the HAL boundary and cannot run natively (CLAUDE.md: HAL is mock/manual only): SD mounts
  with no CRC error under each of {no cap, LoRa cap, multi-radio cap}; the `Radio` entry
  appears only with the multi-radio cap; the CC1101 `VERSION` byte and NRF24 read-back are
  confirmed and the expected CC1101 revision constant is pinned. These prove the SD-safety
  and carousel-visibility halves of DoD scenarios 1, 2, and 4.

## As built

_Filled at merge (the freeze step): the confirmed CC1101 `VERSION` value, the on-device
checklist results, and any deviation from the design above._

**Confirmed hardware facts (on-device, Cardputer ADV, 2026-09-05).** CC1101 `VERSION`
register reads `0x14` when the cap is seated and `0xFF` (floating bus) when it is absent;
detection therefore treats "neither `0x00` nor `0xFF`" as present rather than pinning the
`0x14` revision constant, because the read is already unambiguous and the looser test
survives a revision change. NRF24 presence is the `RF_CH` write/read-back of `0x2A`
(reads back `0xFF` when absent). Both are logged at boot (`[Cap] CC1101 VERSION=…`,
`[Cap] NRF24 RF_CH read-back=…`).

**On-device checklist.** DoD-1: cap seated + `Auto` → `Resolved: Multi-Radio`, Radio
carousel entry present. DoD-2: cap removed + `Auto` → `Resolved: None`, no Radio entry,
SD mounts with zero CRC errors. DoD-3: `resolveExpansionCap` truth table (override wins
over a failed probe) covered by `test/test_expansion_cap`. DoD-4: with the cap present,
GPS auto-detect probes the Grove pin set only and never the cap-GPS pins (`G13/G15`).

**Deviations from the Design above.** The design assumed the probes would be "thin SPI
reads behind the SD-safe bus" on their own SPI handle. On this device the launcher owns
the FSPI hardware peripheral before our firmware runs, so a second `SPIClass(FSPI)`
re-registers the APB-change callback and deadlocks the bus, and bit-banging the shared
data lines directly wedged the SD bus with no clean recovery (even `SD.end()` hung). The
shipped approach instead:

- `SDManager` promotes its Method-2 FSPI `SPIClass` to a file-scoped instance and exposes
  it via `spiBus()`; the probes borrow that exact instance and transact with proper
  `beginTransaction`/`endTransaction` locking and per-device chip-selects (`SD_CS` driven
  high to deselect the card during radio access). This is the standard
  many-devices-on-one-bus pattern.
- Sharing the bus still desyncs the SD card's SPI state machine (CRC/token errors on the
  next access), so detection is followed by `SDManager::remount()` (a full `SD.end()` +
  re-init from `CMD0`) to re-sync the card. Post-remount the SD sees zero errors.
- **Launcher-mounted boots (SDManager Method 1) leave the bus unowned** (`spiBus()` is
  `nullptr`); the SPI probe is skipped and the operator override decides. Auto-detection
  therefore works on cold/Method-2 boots but not launcher-chained ones — the `Cap:
  Multi-Radio` override is the workaround there. Improving this (forcing SD-bus ownership)
  is deferred to the LEDGER.
- Radios are driven to idle after detection (`capRadiosToIdle`: NRF24 `CE` low + CONFIG
  power-down, CC1101 SIDLE/SPWD). The NRF24 module LED nonetheless stays lit at boot — it
  is almost certainly a hard-wired VCC indicator, not a firmware-controllable line; kept
  open in the LEDGER pending a hardware look. The `capRadiosToIdle` code stays because
  driving the radios to a known idle is correct regardless of the LED.

The Radio carousel entry is gated on the resolved cap; the Radio screen *factory* is
registered unconditionally (harmless — nothing reaches it without the gated entry).

**Post-freeze (operator review, 2026-09-05).** The Radio carousel entry ships *always
present but greyed/disabled when the cap is absent* (disabled reason "Multi-radio cap not
found"), matching the RFID entry's optional-hardware convention, rather than absent as
DoD-2 stated. Navigation skips it and selecting it shows the reason toast — the same
behaviour as every other hardware-gated entry (RFID, wardriving-without-GPS). This
supersedes DoD-2's "entry is absent"; the None-cap SD-safety half of DoD-2 is unchanged.
