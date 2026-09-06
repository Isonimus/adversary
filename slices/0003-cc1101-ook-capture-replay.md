---
id: '0003'
title: "CC1101 OOK raw capture & replay"
type: slice
status: accepted
date: 2026-09-05
supersedes: []
superseded_by: []
---

## Goal

Deliver the first RF-attack feature on the multi-radio cap: **raw OOK (ASK) sub-GHz
capture and replay** — the Flipper "Read RAW" equivalent. The operator picks a frequency
preset (315 / 433.92 / 868.35 / 915 MHz), captures the raw on-off-keyed pulse train from a
nearby transmitter (garage remote, cheap 433 fob, doorbell, weather sensor), saves it to SD
as a named signal, and replays it verbatim. **No protocol decoding** — the pulse train is
recorded and re-emitted as-is, which is enough to replay most fixed-code (non-rolling)
remotes. This builds directly on slice-0002's cap-detection + SD-shared-bus foundation
(the `Radio` carousel entry it gated now leads somewhere) and is the first concrete step
against the SubGHz half of the competitor-parity audit item; NRF24 and JS scripting stay
open, and rolling-code / protocol-aware decode is explicitly out of scope for a later slice.

## Definition of Done

- **Given** a multi-radio cap seated (`Radio` entry active) and a 433.92 MHz OOK remote
  transmitting nearby
- **When** the operator opens `Radio → CC1101`, selects the 433.92 MHz preset and starts a
  capture
- **Then** the raw pulse train is recorded, the operator can name and save it, and it
  appears in the saved-signals list on SD (`/adversary/subghz/<name>.sub`)

- **Given** a previously saved signal in the list
- **When** the operator selects it and confirms Replay
- **Then** the CC1101 transmits the recorded pulse train on the signal's stored frequency,
  and a receiver (the original device, or a second capture) sees the same pulse train

- **Given** a capture or a replay has just finished
- **When** control returns to the menu
- **Then** the SD card still reads and writes with no CRC/token error (the card is
  re-mounted after the radio has used the shared bus)

- **Given** no multi-radio cap is resolved, or the SD bus is launcher-mounted (unowned)
- **When** the operator reaches the CC1101 screen
- **Then** capture and replay are shown unavailable with a clear reason (the slice-0002
  hardware-gating convention), and nothing crashes or touches the bus

- **Given** the operator picks a different frequency preset
- **When** a capture or replay runs
- **Then** the CC1101 is tuned to that preset (the `FREQ2/1/0` registers are computed from
  the chosen MHz) and the stored signal records which frequency it was captured on

## Design

### What the spike measured (freeze inputs)

A throwaway on-device spike (`-DCC1101_OOK_SPIKE`, since deleted) validated the whole path
on the real Cardputer ADV before this design was frozen, retiring the bus-level risks that
slice-0002's bring-up warned about. Measured 2026-09-05:

- **Config writes are reliable and take effect.** Poll the `VERSION` status register until
  it reads `0x14` (chip-ready) after `SRES` — measured **~350–380 µs**; writing config
  before that silently drops every write. With the ready-wait, routing GDO0 to `CLK_XOSC/192`
  produced **8033 edges in 30 ms** on `G13` (≈135 kHz, the expected divide), functionally
  proving both that `IOCFG0` writes take effect and that the **GDO0 → G13 → CPU sample path
  works**.
- **RX entry** (`SRX` → `MARCSTATE == 0x0D`) took **~790–807 µs**.
- **SD survives the shared bus.** After the radio transacted, `SDManager::remount()` +
  a test write/read passed with zero CRC errors — the slice-0002 borrow-and-remount pattern
  holds for the OOK config path too.
- **Config-register single reads lag one transaction on this shared bus** (a single read
  returns the previous transaction's data); status/burst reads (`0xC0`, the `VERSION` /
  `MARCSTATE` path) are reliable. The driver therefore uses burst reads for any readback and
  does not depend on config-register readback for correctness.
- **Bus ownership is required.** The probe/config needs `SDManager::spiBus() != nullptr`
  (Method-2 / direct-flash mount); a launcher-mounted boot (Method 1) leaves it null — the
  same constraint slice-0002 documented, and the same held launcher-boot fix would unblock it.

The **433.92 MHz register set is SmartRF-derived** (transcribed from the MIT-licensed
`LSatan/SmartRC-CC1101-Driver-Lib`, so no new dependency): `FREQ2/1/0 = 0x10/0xB0/0x71`,
`MDMCFG2 = 0x30` (ASK/OOK, no sync/preamble — raw async), `PKTCTRL0 = 0x32` (async serial,
infinite length), `IOCFG0 = 0x0D` (GDO0 = async serial data), `MDMCFG4/3 = 0x87/0x93`
(RX BW ≈ 203 kHz, ≈5 kBaud), plus the standard `AGCCTRL/FSCAL/TEST/FREND` block.
Real-signal pulse-width characterisation was **not** captured (no 433 MHz remote available
at freeze); it is an on-device verification item below, and the RMT tick resolution is set
fine enough (1 µs) to not depend on it.

### Capture / replay mechanism — RMT, considered against CPU bit-bang

Capture and replay use the **ESP32 RMT peripheral** bound to `CC1101_GDO0` (`G13`): RMT RX
records the pulse train item-by-item in hardware; RMT TX re-emits it. In CC1101 async
serial mode the same GDO0 pin carries demodulated data **out** in RX and accepts data **in**
in TX, so one RMT binding (direction flipped with the radio's RX/TX strobe) serves both —
the spike proved that pin path in both the sample and clock-output directions.

The spike showed a CPU `digitalRead` loop *can* resolve 135 kHz, so CPU bit-bang is
technically possible — it is **deliberately not used**. RMT is chosen because it (a) times
edges in hardware, immune to the jitter a `micros()` loop suffers under WiFi/other ISRs,
(b) replays with precise, non-blocking timing, and (c) does not monopolise a core for the
waveform duration — exactly the failure mode CLAUDE.md's raw-RF pitfall warns about. RMT is
also already the mechanism the IR stack uses under the hood, so it is the house pattern, not
a new one. RMT capture runs while SPI is idle, so the SD bus is undisturbed during the
window; the radio is strobed to idle and the card re-mounted before any SD write.

### What is reused

- **`SDManager::spiBus()` + `remount()`** and the per-device chip-select / `deselectSdCard`
  idiom from `expansion_cap.cpp` (slice-0002) — the CC1101 driver borrows the exact FSPI
  `SPIClass` the card is mounted on and re-mounts after use. No new bus, no bit-bang.
- **The `Radio` carousel entry and its cap-gating** (slice-0002) — the entry that was a
  placeholder now opens the CC1101 sub-menu; the "hardware absent → disabled with a reason"
  convention (RFID/wardriving-without-GPS) covers DoD scenario 4 unchanged.
- **`ListView` + footer hints** for the saved-signals list, and `SDManager` file I/O for the
  `.sub` files under a new `/adversary/subghz/` directory.
- **The replay-confirmation pattern** — replay transmits, so it is gated behind an explicit
  operator confirm, matching the deauth known-instability-warning convention already tracked
  for Evil Twin/Karma.

### What is deliberately NOT reused / NOT built

- **No `HardwareManager` capabilities singleton** — same reasoning as slice-0002: folding the
  CC1101 into a not-yet-existing abstraction is speculative generality. The driver is a small
  unit next to the pins it drives, and moves into `HardwareManager` if that slice happens.
- **No protocol decode / IRremote-style codec** — this slice records and re-emits raw pulses
  only. `IRremoteESP8266` is IR-carrier-modulation specific and does not apply to sub-GHz OOK.
- **No rolling-code handling** — fixed-code replay only; rolling codes are out of scope and
  cannot be defeated by verbatim replay anyway.

### Module layout

Three small, cohesive units plus the screen (kept separate by concern, not split to hit a
number):

- `src/hal/expansion/cc1101.{h,cpp}` — CC1101 driver over the borrowed bus: `configureOok
  (freqMHz)`, `enterRx()`, `enterTx()`, `idle()`, using the frozen register set. The pure
  **frequency → `FREQ2/1/0`** math is a free function (`cc1101FreqRegs`) so it is native-
  testable without hardware.
- `src/modules/rf/ook_signal.{h,cpp}` — the in-memory pulse train and its `.sub` file
  (de)serialisation (frequency header + edge-duration list). Pure; native-testable.
- `src/modules/rf/ook_rmt.{h,cpp}` — RMT bind on `GDO0`: `capture(window) → pulse train` and
  `replay(pulse train)`. Firmware-only (RMT), thin over the ESP-IDF driver.
- `src/ui/screens/radio_screen.*` — extend to a CC1101 sub-menu: Capture, Saved signals
  (list → Replay/Delete), Frequency preset. Replaces the slice-0002 placeholder content.

Frequency presets are named constants (`315.0 / 433.92 / 868.35 / 915.0` MHz), never bare
literals.

## Verification

- **Native unit tests** (`pio test -e native`), each a fail-before/pass-after regression on
  the host-testable logic:
  - `test/test_cc1101_freq.cpp` — `cc1101FreqRegs()` against known vectors, including
    `433.92 → {0x10, 0xB0, 0x71}`, plus each preset; asserts the `f · 2¹⁶ / 26 MHz` rounding.
  - `test/test_ook_signal.cpp` — `.sub` (de)serialisation round-trips a pulse train with its
    frequency; rejects an empty/oversized train and a malformed file (fail-loud, no silent
    default).
- **On-device operator checklist** (the HAL/RMT/RF boundary that cannot run natively —
  CLAUDE.md: HAL is mock/manual only), recorded in `## As built`:
  - Capture a real 433.92 MHz remote → the recorded pulse widths are plausible OOK
    (~100–1000 µs edges); **this is where the real pulse-width numbers deferred at freeze get
    measured** and the RMT tick/buffer sizing confirmed.
  - Replay the saved signal → the original device responds (or a second capture reproduces the
    train).
  - SD reads/writes with zero CRC error immediately after both capture and replay.
  - CC1101 screen with the cap absent → capture/replay unavailable with the reason shown, no
    bus access, no crash.
  - Each frequency preset tunes the radio (capture/replay round-trips at 315 / 868.35 / 915).

  The bus/config/RX-entry/GDO0-path risks these would otherwise carry are already retired by
  the pre-freeze spike (numbers above), so the checklist is real-signal + integration, not a
  re-proof of the bus.

## As built

Shipped as four cohesive units, exactly as designed: `hal/expansion/cc1101.{h,cpp}` (driver +
pure `cc1101FreqRegs`), `modules/rf/ook_signal.{h,cpp}` (`SUB1` codec), `modules/rf/ook_rmt.{h,cpp}`
(RMT capture/replay), and the extended `ui/screens/radio_screen.{h,cpp}`. A small
`resolvedExpansionCap()` getter was added to `expansion_cap` for the screen's cap gate.

**Verified on-device (Cardputer ADV, 2026-09-06):**

- **Cap detection**: `CC1101 VERSION=0x14 present`, `NRF24 present`, `Resolved: Multi-Radio` —
  VERSION `0x14` matches the driver's chip-ready gate constant.
- **Real-signal capture — the freeze-deferred measurement, now taken.** An ambient 433.92 MHz
  OOK burst captured cleanly: **887 edges**, first widths **~125 / 470–475 / 983–999 µs** — a
  short/long ~470 µs-base PWM train, i.e. plausible fixed-code OOK, well under the 2048-edge
  cap. (The burst was a bystander's signal caught by accident during ambient testing; it was
  deleted un-replayed — see the privacy note below.)
- **Idle behaviour**: with no transmitter the OOK slicer stays squelched, so the RMT window
  ends in a clean `No signal captured` timeout rather than recording a noise floor. Two
  spurious single-edge captures (1 edge, 7898 µs / 50 µs) were stray transients, harmless.
- **SD-safe (DoD scenario 3)**: across **14 back-to-back RF ops** the card re-mounted cleanly
  every time (SDHC), zero crashes, heap steady (~151 KB) — the slice-0002 borrow-and-remount
  pattern holds for the OOK path.
- **No-crash / gating**: `RadioScreen` opens and drives the Capture path with the cap present;
  the cap-absent "unavailable + reason" branch is coded per the slice-0002 convention.

**RMT/register values that shipped:** 1 µs RMT tick; RX min-pulse 1 µs, idle-gap 12 ms; RX
buffer 1024 symbols (2048 edges); TX replay 3× repeats with 8 ms inter-frame gaps. The 433.92
register block is the SmartRF-derived set from the pre-freeze spike, verbatim, with `FREQ2/1/0`
computed from the preset by `cc1101FreqRegs`.

**Deviation from design:** added a permanent `[Radio] Captured N edges … first: …us` (and the
timeout counterpart) info log — operator feedback on capture outcome, useful and cheap.

**NOT yet verified — deferred (tracked in LEDGER):**

- **The replay (TX) round-trip.** `cc1101EnterTx` + `ookRmtReplay` are code-complete and
  compile, but the end-to-end *capture-an-owned-fob → replay → the device actuates* test was
  **not run**: no owned 433 transmitter was available at merge, and replaying a bystander's
  captured access-control signal is out of scope (unauthorised, and a privacy liability). The
  shipped replay repeat/gap tuning is therefore a reasoned default, not a measured one.
- **Frequency presets other than 433.92** exercised the retune code path (no error, clean
  timeout) but were not signal-confirmed.

**Privacy note (decision):** a captured signal is a third party's credential unless it is from
hardware you own. The bystander burst caught here was deleted without transmitting it; replaying
captured third-party fob/remote signals is not something this feature is for.
