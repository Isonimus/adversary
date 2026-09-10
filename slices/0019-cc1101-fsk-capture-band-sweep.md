---
id: '0019'
title: "CC1101 RSSI band-sweep + FSK capture (2-FSK/GFSK/4-FSK/MSK)"
type: slice
status: accepted
date: 2026-09-09
supersedes: []
superseded_by: []
---

## Goal

slice-0003 capture/replay and slice-0017 jamming all assume **OOK/ASK**: they slice the
carrier *envelope*. A field test against a tobacco-vending "adult activator" captured
nothing on 433.92 — the remote is on another band, another modulation, or both, and the
OOK envelope slicer sees a flat line on any constant-amplitude (FSK) carrier regardless.

Two capabilities close that gap, in order of operator value:

1. **RSSI band-sweep** — press the remote, read the CC1101's RSSI across the four band
   presets, and show which band carries energy. **Modulation-independent**: it answers
   "*where* does this thing transmit?" before any demodulation question arises. This is
   the primitive that directly unblocks the vending-machine test.
2. **FSK demodulated capture** — configure the CC1101 for 2-FSK/GFSK/4-FSK/MSK with an
   explicit deviation / data-rate / RX-bandwidth parameter set, so a constant-amplitude
   remote can be received and (for the binary schemes) stored and replayed.

## Why FSK is not "just another preset"

OOK encodes data as carrier **on/off**; the envelope *is* the data, so capture is
protocol- and bitrate-agnostic (record edge widths, replay by re-keying the PA). FSK
encodes data as **frequency shifts** at **constant amplitude** — there is no envelope to
slice. The CC1101 must *demodulate* it internally: `MOD_FORMAT` = the scheme, `DEVIATN` =
the shift, plus a matching data rate and RX bandwidth. Unlike OOK, **wrong parameters
yield garbage bits, indistinguishable from the wrong frequency.** The CC1101 is a
narrowband demodulator, **not an SDR** — it cannot record I/Q for later sorting. This is
exactly why the band-sweep (which needs none of those parameters) comes first.

## The capture-path reuse (the load-bearing decision)

The existing RMT capture/replay (`ook_rmt.cpp`) times **edges on GDO0** and stores them as
an `OokSignal` (carrier + run-length edge list). Put the CC1101 in **async-serial mode**
with an FSK demod and GDO0 carries the *recovered NRZ data line* — so the RMT path
captures FSK-demodulated bits with **no change to the RMT layer at all**. Replay is the
mirror: RMT drives GDO0, the CC1101 FSK-*modulates* it, provided the radio is reconfigured
with the same parameters the capture was taken under.

Consequence for storage: a replayed signal must reconstruct the radio state, so the
on-SD format must carry the modulation + parameters, not just the carrier. See "Format".

## Scope

**CC1101 cap, Cardputer only.** Two new verbs on the existing Sub-GHz console
(`RadioScreen`), alongside Capture / Saved / Frequency / Jam.

### Band-sweep
- Sweeps the **four existing band presets** (315 / 433.92 / 868.35 / 915 MHz), reading the
  CC1101 RSSI status register per band, **peak-held** while the operator holds the remote.
- Output: four labelled bars (dBm), strongest highlighted — the directly actionable "go
  capture here" answer. Follows the slice-0006 NRF24-sweep idiom (per-frame stepping,
  footer-hint `Back`, a pure downsample/normalise model).
- **Not** a fine-grained in-band sweep — the presets are the frequencies capture can use,
  so those are the bars that tell the operator what to do next. Fine stepping is a
  possible v2 and is logged, not built.

### FSK capture
- `MOD_FORMAT`-selectable: **2-FSK, GFSK, 4-FSK, MSK** (the operator's explicit choice per
  the scope decision on 2026-09-09).
- An explicit parameter set per capture — **deviation, data rate, RX bandwidth** — offered
  as a small table of common sub-GHz presets (e.g. 868.35 / 2-FSK / 47 kHz dev / 4.8 kBaud),
  because these cannot be inferred from an unknown signal (see "Why FSK…").
- **Capture ceiling, stated honestly (not silently dropped):**
  - **2-FSK / GFSK** — *fully* capturable/replayable. GFSK reception **is** the 2-FSK demod
    path (GFSK is TX-side Gaussian shaping only), so one RX config serves both.
  - **MSK** — binary, so the async-serial + RMT path works mechanically, but the CC1101
    supports MSK **only above ~26 kBaud**; presets below that floor are rejected, not
    silently mis-tuned.
  - **4-FSK** — **2 bits/symbol cannot ride a single binary GDO0 async line**, so it has
    **no raw-capture path** without a known sync word + packet length (unavailable for an
    unknown remote). 4-FSK is therefore **tunable and RSSI-detectable** (the band-sweep and
    a carrier-sense readout work), but raw capture of an unknown 4-FSK signal is out of
    reach on this silicon. The doc says so rather than shipping a button that returns noise.

### Out of scope
- Protocol decode, rolling-code defeat, auto-detection of deviation/datarate (no I/Q — see
  above). FIFO/packet-mode capture (the only thing that would make 4-FSK meaningful) — a
  separate, larger slice if a concrete 4-FSK target ever warrants it.

## Format

The `SUB1` binary (slice-0003) stores magic + version + flags + carrier + edge list.
Introduce **`SUB2`** adding a modulation descriptor {mod-format, deviation, data-rate,
RX-bandwidth}; `SUB1` files keep loading as OOK (the reader branches on magic). A captured
signal replays by reconstructing the radio from its own descriptor — an OOK `SUB1` through
`cc1101ConfigureOok()`, an FSK `SUB2` through the new `cc1101ConfigureFsk()`.

## Definition of Done

**Band-sweep**
- **Given** the cap is resolved and the SD/FSPI bus is owned
- **When** the operator opens Band-sweep and holds the remote's button
- **Then** the four band bars update live and the band carrying the remote's energy reads
  visibly above the noise floor and is highlighted

**FSK capture**
- **Given** a 2-FSK/GFSK preset matching a test transmitter's parameters
- **When** the operator captures, then replays
- **Then** the stored `SUB2` round-trips and the replay reproduces the signal (verified
  against a controlled 2-FSK source; rolling-code targets are not expected to actuate)

## Host-testable core (the regression surface)

Per the slice-0003 precedent (`cc1101FreqRegs` native-tested in `test/test_cc1101_freq`),
the new **pure register math** is the unit-tested piece — a wrong shift here silently
mistunes the demod:
- RSSI raw byte → dBm (the CC1101 two's-complement + offset formula).
- deviation (Hz) → `DEVIATN` (mantissa/exponent).
- data rate (baud) → `MDMCFG4.DRATE_E` / `MDMCFG3.DRATE_M`.
- RX bandwidth (Hz) → `MDMCFG4.CHANBW`.
- `SUB2` (de)serialise round-trip + `SUB1`-still-reads-as-OOK (in `test_ook_signal`).

Each must fail before its implementation and pass after. The radio I/O and the screen stay
hardware-only (native-excluded), same split as `ook_rmt` / `radio_screen`.

## Risks / honest limits

- **Parameter guesswork.** For a truly unknown FSK remote, deviation/datarate are a
  search, not a read. The band-sweep narrows the frequency; the operator still iterates
  parameter presets. This is a CC1101 limitation, not a design defect — an SDR is the right
  tool for blind FSK characterisation, and this is explicitly not one.
- **4-FSK/MSK expectations.** Covered above; the UI must not imply 4-FSK raw capture works.
- **Shared GDO0 / keyboard matrix.** Same G13 constraint as slice-0003/0017 — the sweep and
  FSK RX own the bus in short windows, then release it; no change to that invariant.

## Verification

**Phase 1 — band-sweep**
- **Native:** `test_cc1101_rssi` covers the RSSI raw→dBm formula (5 vectors incl. the
  raw≥128 two's-complement sign split and the ordering it implies). `test_band_sweep`
  covers the peak-hold model (empty→none, first-reading, peak retain, strongest-across-bands,
  ties→lowest index, out-of-range ignored). Both fail before their implementation, pass after.
- **Build:** `pio run -e cardputer` links the new HAL read (`cc1101ReadRssiDbm`), the
  `BandSweep` model, and the `BAND_SWEEP` screen state.
- **On-device (the DoD, verified on hardware before commit):** open Band-sweep, hold an
  unknown remote — the band carrying its energy rises visibly above the floor and is
  highlighted. Confirmed 2026-09-10 against the tobacco-vending activator and a car keyfob
  (both → 433.92); see As built.

**Phase 2 — FSK capture** (pending)
- **Native:** register math (`DEVIATN`, `DRATE_E/M`, `CHANBW`) each fail-before/pass-after;
  `SUB2` (de)serialise round-trip + `SUB1`-still-reads-as-OOK in `test_ook_signal`.
- **Build + on-device:** `pio run -e cardputer`; capture→replay a controlled 2-FSK source and
  confirm the `SUB2` round-trips and the replay reproduces the signal.

## As built

### Phase 1 — RSSI band-sweep (shipped, field-verified 2026-09-10)

The band-sweep landed exactly as designed, reusing the slice-0006 idiom end to end:

- **Pure register math** — `cc1101RssiDbm(raw)` in `cc1101.h` applies the CC1101
  two's-complement + 74 dBm offset (§17.3); native-tested in `test/test_cc1101_rssi`
  (5 vectors incl. the raw≥128 sign split). `cc1101ReadRssiDbm()` reads status register
  `0x34` under the owned FSPI bus, returning `INT16_MIN` when the bus is not owned (fail
  loud, no fabricated reading).
- **Pure peak-hold model** — `rf::BandSweep` (`band_sweep.{h,cpp}`): per-band peak retain +
  `bandSweepStrongest()`; native-tested in `test/test_band_sweep` (6 tests: empty→none,
  peak-hold, strongest-across-bands, ties→lowest index, out-of-range ignored).
- **Screen** — `RadioScreen::BAND_SWEEP` steps one preset per `update()` frame
  (`cc1101ConfigureOok` retune → `cc1101EnterRx` → 1500 µs settle → read → observe),
  owning the bus across bands and only `cc1101Idle()`+remounting the SD on exit — the NRF
  scan lifecycle. `drawBandSweep` renders four peak-held bars, strongest in `SUCCESS`.
  RSSI is modulation-independent, so this reads energy regardless of OOK vs FSK.
- Boyscout: the CC1101 main menu now dispatches on a named `MainItem` enum
  (`MAIN_CAPTURE`…`MAIN_JAM`) instead of bare indices, so inserting "Band sweep" mid-list
  was not a silent landmine.

**Field verification (2026-09-10).** Against the tobacco-vending adult-activator *and* a
car keyfob: both localised correctly to the **433.92 MHz** bar (rose visibly above the
other three, highlighted) while the button was held. The tobacco activator then
captured + replayed flawlessly through the existing OOK path — the band-sweep did its job
of answering "*which band*" before capture. The **car fob's OOK capture failed empty**,
which is the *expected, diagnostic* outcome, not a defect: modern fobs are constant-
amplitude **FSK** (no envelope for the OOK slicer — the Phase 2 gap) and additionally
**rolling-code**, so even once Phase 2 makes the fob capturable, a replayed frame is a
spent code and will not actuate the car. This is the DoD's "rolling-code targets are not
expected to actuate", observed in the field.

### Phase 2 — FSK demodulated capture

_Pending. `cc1101ConfigureFsk()` register math (DEVIATN / DRATE / CHANBW) + native tests,
`MOD_FORMAT` selection with the 2-FSK/GFSK/MSK/4-FSK ceilings documented above, the `SUB2`
format bump + round-trip test, and the FSK capture states in `RadioScreen`. To be verified
against a controlled 2-FSK source. Do not edit above this line._

## Amendment — 2026-09-10: Phase 2 built (on-device round-trip deferred)

Phase 2 shipped, native-tested with a clean Cardputer build. The on-device capture/replay
round-trip is **deferred to the LEDGER** — no controlled, static 2-FSK source was on hand
(the operator's realistic FSK target, a car keyfob, is rolling-code and cannot serve as a
replay fixture). Scope choice recorded 2026-09-10: **2-FSK / GFSK / MSK** presets ship;
4-FSK stays detect-only.

- **Register math (native-tested, `test_cc1101_fsk`).** `cc1101DeviatnReg` (DEVIATN
  mantissa/exponent), `cc1101DrateRegs` (DRATE_E/M), `cc1101ChanbwNibble` (CHANBW), and the
  `cc1101ModemRegs` composer — anchored to SmartRF-documented vectors. The composed
  `MDMCFG4` for (47.6 kHz, 4.8 kBaud, 203 kHz) is `0x87`, which **cross-checks the OOK
  block's own hand-written `MDMCFG4`** — a formula regression is caught against known-good
  silicon values. `cc1101FskConfigValid` enforces the MSK ≥26 kBaud floor (native-tested,
  and `cc1101ConfigureFsk` refuses a config that fails it — fail loud).
- **`SUB2` format (native-tested, `test_ook_signal`).** `OokSignal` gained a modulation
  descriptor (`Modulation` + deviation/data-rate/RX-bandwidth). OOK serialises as `SUB1`
  **byte-identically** (existing captures unaffected — regression-pinned); FSK serialises as
  `SUB2` and round-trips the descriptor; a `SUB2` whose modFormat says OOK or names an
  unknown scheme is rejected (fail loud).
- **HAL (`cc1101ConfigureFsk`, hardware-only).** Programs MOD_FORMAT + the computed modem
  regs, single-entry PATABLE (constant envelope, `FREND0=0x10`), GDO0 async-serial — so the
  **RMT capture/replay layer is unchanged** (the FSK demod drives GDO0 with recovered NRZ
  exactly as the OOK slicer does). The FS-calibration block is **deliberately duplicated**
  from the OOK path, not shared: the OOK block is on-device-verified and this FSK path is
  not, so they must not be coupled where an FSK change could silently regress OOK. The AGC/
  FOCCFG/BSCFG values are SmartRF 2-FSK defaults, unverified on hardware (part of the
  deferred round-trip).
- **Screen (`RadioScreen`).** "Capture" renamed **"OOK Capture"**; new **"FSK Capture"**
  opens an `FSK_SELECT` preset picker (the operator can't infer deviation/data-rate from an
  unknown signal, so a table is offered). Capture/replay/describe are modulation-aware;
  replay rebuilds the radio from a signal's own descriptor, so a saved `SUB2` file replays
  through the FSK config transparently. The main menu and FSK list scroll (both exceed the
  space above the footer at 6 rows). GUI verified on hardware 2026-09-10 before commit.
