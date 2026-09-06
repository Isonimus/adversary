---
id: '0006'
title: "NRF24 2.4 GHz spectrum analyzer"
type: slice
status: accepted
date: 2026-09-06
supersedes: []
superseded_by: []
---

## Goal

Give the multi-radio cap's **second radio** a first, honest feature: a passive **2.4 GHz
channel-occupancy analyzer** on the nRF24L01+. The operator opens the NRF24 console and
sees a live bar display of activity across all 126 channels (2400–2525 MHz) — where WiFi,
BLE, Zigbee, and other 2.4 GHz traffic is sitting — built from the chip's on-board
**Received Power Detector (RPD)**. It is the 2.4 GHz analog of a spectrum sweep, not an
attack: nothing is transmitted, nothing is decoded, nothing is stored.

This is deliberately **not** the nRF24 "record/replay" shape the parity-audit line implies.
The nRF24L01+ is an addressed GFSK packet radio (channel + pipe address + CRC), so there is
no raw waveform to record verbatim the way CC1101 OOK (slice-0003) and IR (slice-0004) are
recorded. The real nRF24 *attack* is Mousejack — promiscuous sniff of a wireless-HID dongle
then keystroke injection — which is a specific, device-targeted capability that can only be
verified against an owned Logitech Unifying-class dongle. No such dongle is on hand, and this
project already carries two RF features shipped unverified for want of hardware (CC1101 TX
replay, RAW-IR strip). Adding a third unverifiable feature to clear a checkbox is the wrong
trade. The analyzer, by contrast, is **fully verifiable today** against ambient WiFi/BLE with
zero target hardware, so it ships genuinely green. Mousejack stays a separate, later slice,
explicitly gated on owning a dongle — this slice is what makes the second radio real and lays
the console structure that Mousejack will extend.

## Definition of Done

- **Given** a multi-radio cap seated (`Radio` entry active) and normal 2.4 GHz activity
  nearby (any WiFi AP or BLE device)
- **When** the operator opens `Radio → NRF24 → 2.4 GHz Scan`
- **Then** a live per-channel bar display appears and channels carrying the ambient traffic
  read as occupied (non-zero bars) while quiet channels stay low, updating as the sweep runs

- **Given** the analyzer is running
- **When** the operator presses `` ` `` (back)
- **Then** the sweep stops, the radio is returned to power-down/idle, and control pops one
  level to the NRF24 menu (a second `` ` `` leaves to the carousel) — no radio left emitting
  or drawing current

- **Given** an analyzer sweep has just finished or been cancelled
- **When** control returns to the menu
- **Then** the SD card still reads and writes with no CRC/token error (the shared bus is
  released and the card re-mounted after the radio has used it — the slice-0002/0003 pattern)

- **Given** no multi-radio cap is resolved, or the SD bus is launcher-mounted (unowned)
- **When** the operator reaches the NRF24 screen
- **Then** the scan is shown unavailable with a clear reason (the slice-0002 hardware-gating
  convention, identical to the CC1101 gate), and nothing crashes or touches the bus

## Design

### Mechanism — RPD sweep over the borrowed SD bus

The nRF24L01+ exposes a one-bit **RPD** register (`0x09`, bit 0): while the radio is in RX on
a channel, RPD latches high when the received power exceeds a hardware-fixed **−64 dBm**
threshold. It is a coarse presence detector, not a graded dBm meter — an honest limitation
stated in the UI (the bars are *occupancy frequency over the sweep window*, not signal
strength). This is the well-known nRF24 "poor man's scanner" technique from the Nordic
datasheet, and it needs no packet reception, no address match, and no CRC — exactly why it is
verifiable against arbitrary ambient traffic.

Per channel, per sweep:

1. Set `RF_CH` to the channel (0–125).
2. Drive `CE` high to enter RX; wait the RX settling time (`Tstby2a` ≈ 130 µs) plus a short
   listen dwell.
3. Drive `CE` low back to standby.
4. Read `RPD` (`0x09`); accumulate a hit if bit 0 is set.

`CE` is a plain GPIO (`G3`), **not** on the SPI data bus, so toggling it never disturbs SD.
The register reads/writes ride the **borrowed** FSPI bus exactly as the slice-0002 probe and
the slice-0003 CC1101 driver do: `SDManager::spiBus()` + `deselectSdCard()` +
`capSpiSettings()`, CS toggled per transaction, then the card re-mounted before any SD write.
The whole sweep is SPI-register traffic interleaved with `CE`/dwell — SPI is idle during the
dwell, so it plays the same borrow-and-remount game the OOK path already proved safe across 14
back-to-back ops.

One-time RX config before sweeping (`beginRxScan`): `CONFIG = PWR_UP | PRIM_RX`, `EN_AA = 0`
(auto-ack off — we must not filter), a fixed data rate and address width, `RF_SETUP` for the
lowest-rate/most-sensitive setting. No pipes opened, no payloads read.

### What is native-testable (the pure core), and what is on-device only

Split so the arithmetic and the display mapping are proven on the host, and only the true
HAL/RF boundary needs a device — the slice-0003 discipline.

- **`nrf24ChannelToMHz(ch)`** — pure `2400 + ch`, rejecting `ch > 125` (fail-loud, no clamp).
  A free function in the driver header, unit-tested against the band edges.
- **`spectrumColumns(hits, channelCount, sweeps, outCols, width)`** — pure downsample +
  normalise: map the 126-channel hit-count histogram onto a fixed pixel width as per-column
  bar heights (0…`FULL`), bucketing channels per column by their max and normalising against
  the sweep count so a fully-occupied channel reads full-scale. This is the magnitude-
  histogram analog of slice-0005's `pulseStripColumns` (occupancy bars, not duty), and its
  first use — a *new* small pure function, not a forced reuse of the duty downsampler. Unit-
  tested for proportional bucketing, normalisation, degenerate inputs (fail-loud false), and
  never writing past `width` — the exact contract shape `test_pulse_strip` pins.
- **On-device only** (HAL/RMT/RF boundary, CLAUDE.md: HAL is mock/manual): the RPD read
  itself, RX settling/dwell timing, and that ambient traffic actually lights the right
  channels. Recorded in `## As built`.

### Module layout

Mirrors slice-0003's three-units-plus-screen split, by concern:

- `src/hal/expansion/nrf24.{h,cpp}` — NRF24 driver over the borrowed bus: `beginRxScan()`,
  `setChannel(ch)`, `sampleRpd(dwellUs) → bool`, `idle()` (power-down + `CE` low). Pure
  `nrf24ChannelToMHz()` lives in the header. Reuses the shared cap-bus helper (below).
- `src/modules/rf/spectrum_scan.{h,cpp}` — the pure occupancy model: the per-channel hit
  accumulator across sweeps and `spectrumColumns()`. No hardware; native-testable.
- `src/ui/screens/radio_screen.{h,cpp}` — extended with the NRF24 console (see UX note).

Channel range (`0…125`), the −64 dBm note, settling/dwell µs, and sweep count are named
constants, never bare literals.

### UX: a radio-select root in RadioScreen (the one call to confirm)

RadioScreen is today the CC1101 console directly (`MAIN` = Capture / Saved / Frequency). With
a second radio it needs a root. Two options:

1. **Radio-select root (proposed).** RadioScreen's top level becomes `CC1101 Sub-GHz` /
   `NRF24 2.4 GHz`; each dives into its own sub-tree (CC1101 unchanged below its node; NRF24 =
   `2.4 GHz Scan` today, room for Mousejack later). One carousel entry, one cap gate, and the
   structure Mousejack will slot into cleanly. Cost: CC1101 gains one extra selection step.
2. **Fourth item on the CC1101 menu** (`… / 2.4 GHz Scan`). Least code, no extra step for
   CC1101 — but it files an NRF24 action under the CC1101 console, which is incoherent and
   gives Mousejack nowhere clean to land.

Proposed: **option 1** — it is the correct model for a two-radio cap and is the foundation the
planned Mousejack slice needs; the extra keypress is a fair price. This is the design point to
confirm before code.

### What is reused / refactored (rule-of-three + boyscout)

- **Shared cap-bus helper — extract now.** `capSpiSettings()` and `deselectSdCard()` are
  currently **duplicated** file-local in both `expansion_cap.cpp` and `cc1101.cpp`. The NRF24
  driver is the **third** consumer, so this slice extracts them (plus `releaseChipSelect`) into
  `src/hal/expansion/cap_bus.{h,cpp}` and points all three at it — rule-of-three satisfied, and
  the boyscout fix for a real duplication the previous slices left. No behaviour change; the
  existing cap/OOK on-device behaviour is the regression guard.
- **The `Radio` carousel entry + cap-gating** (slice-0002/0003) and the "absent → disabled with
  a reason" convention — reused verbatim for the NRF24 gate (DoD scenario 4).
- **`SDManager::spiBus()` + `remount()`** borrow-and-remount, and the `ListView` + footer-hints
  menu idiom for the radio-select root and NRF24 menu.

### What is deliberately NOT built

- **No Mousejack / sniff-inject** — a separate later slice, gated on an owned Unifying dongle
  (see Goal). This slice is passive-only.
- **No packet RX / decode / replay** — the nRF24 has no faithful raw-waveform capture; a
  "record/replay" framing would be a leaky abstraction. Not built.
- **No `HardwareManager` capabilities singleton** — deferred again, same reasoning as
  slice-0002/0003: the driver is a small unit next to its pins, the screens already gate on
  `resolvedExpansionCap()`, and there is still no present consumer that an abstraction would
  simplify. The rule-of-three *radio* count is now three (CC1101, IR, NRF24), but the shared
  need they actually have is the **bus helper** extracted above, not a capabilities registry —
  so that is what gets built. `HardwareManager` stays tracked in the LEDGER.

## Verification

- **Native unit tests** (`pio test -e native`), each fail-before/pass-after on host-testable
  logic:
  - `test/test_nrf24_channel.cpp` — `nrf24ChannelToMHz()`: `0 → 2400`, `125 → 2525`, and
    `ch > 125` rejected (fail-loud, no clamp).
  - `test/test_spectrum_scan.cpp` — `spectrumColumns()`: proportional channel→column
    bucketing, normalisation against sweep count (a channel hit every sweep reads full-scale;
    half the sweeps reads mid), degenerate inputs return false without writing, and a guard-
    byte check that it never writes past `width` (the `test_pulse_strip` contract shape).
- **On-device operator checklist** (the RF boundary that cannot run natively), recorded in
  `## As built`:
  - Open `Radio → NRF24 → 2.4 GHz Scan` with the cap seated → a live bar display appears.
  - Bring up known 2.4 GHz traffic (phone hotspot / BLE beacon on a known WiFi channel) →
    the corresponding nRF24 channels light; turning it off quiets them. This is the ambient-
    traffic verification the analyzer path is chosen for — no target hardware needed.
  - `` ` `` stops the sweep and powers the radio down (CE low, PWR_DOWN); the board draws no
    extra current and nothing emits.
  - SD reads/writes with zero CRC error immediately after a sweep (borrow-and-remount).
  - NRF24 screen with the cap absent / bus unowned → scan unavailable with the reason shown,
    no bus access, no crash.

## As built

Shipped as designed, four cohesive units plus the screen: `hal/expansion/cap_bus.{h,cpp}`
(the rule-of-three extraction of `capSpiSettings`/`deselectSdCard`/`releaseChipSelect`, now
shared by expansion_cap.cpp, cc1101.cpp and nrf24.cpp — a real duplication the prior slices
left, fixed here with no behaviour change), `hal/expansion/nrf24.{h,cpp}` (RPD scanner + the
pure `nrf24ChannelToMHz`), `modules/rf/spectrum_scan.{h,cpp}` (`SpectrumScan` tally + the pure
`spectrumColumns`), and the extended `ui/screens/radio_screen.{h,cpp}`. The UX call landed as
**option 1**: the single `Radio` carousel entry now opens on a radio-select root (CC1101
Sub-GHz / NRF24 2.4 GHz), CC1101's `` ` `` pops to that root, and the NRF24 node holds the
live scan — the clean landing spot the Mousejack follow-up needs.

**Verified on-device (Cardputer ADV, 2026-09-06):** the DoD checklist passed — ambient
2.4 GHz traffic lights the corresponding channels on the live occupancy display and they
quiet when the source stops; `` ` `` stops the sweep, powers the radio down and pops to the
root; the SD card reads/writes clean immediately after a sweep, so the slice-0002/0003
borrow-and-remount pattern holds for the RPD path too. Native + compile were green at merge
(test_nrf24_channel, test_spectrum_scan; full suite 692/692; cardputer build SUCCESS). The
RPD display is qualitative (occupancy frequency), so no per-channel dBm numbers are recorded —
that is the -64 dBm hardware limitation stated in the UI, not a gap.

**Values that shipped:** 120 µs per-channel RX dwell on top of the 130 µs (Tstby2a) settling;
42 channels sampled per `update()` so a full 0..125 sweep spans ~3 frames and the Back key
stays responsive; the uint16 occupancy tally rolls (reset) at 4000 sweeps (~a couple of
minutes) so the bars show recent activity rather than a saturated all-time count. RX config is
PWR_UP|PRIM_RX with auto-ack disabled and no pipes opened; RPD is read from register 0x09
bit 0.

**Deliberately not built (tracked in LEDGER):** Mousejack sniff+inject (a dongle-gated
follow-up slice under this same NRF24 node) and any packet decode/replay (nRF24 is addressed
GFSK — no faithful raw-waveform capture). `HardwareManager` was deferred again: the radios'
real shared need turned out to be the cap-bus helper, now extracted, and the screens still
gate on `resolvedExpansionCap()`.
