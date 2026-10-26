---
id: '0128'
title: "RSSI proximity tracker: a shared honest-signal core for BLE and WiFi hunting"
type: architecture
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Context

The scanners list devices and APs but cannot help an operator walk toward one. A rogue
beacon, a skimmer, an AirTag-class tracker, an evil-twin AP or a dropped node is found by
"hot/cold": watch the signal climb as you close in. Both radios already capture RSSI per
sighting: BLE in `BLEDeviceInfo::rssi` (updated every advertisement, `ble_scanner.h:22`),
WiFi in the promiscuous `rx_ctrl.rssi` the sniffer and station scanner already read
(`packet_sniffer.cpp:369`, `station_scanner.cpp:132`). So a tracker is a new screen plus a
single-target signal loop, not new radio infrastructure.

Two forces shape the design:

- **RSSI is not distance, and pretending otherwise is a fail-loud violation.** Converting
  dBm to metres needs the transmitter's calibrated TxPower-at-1m (often absent or wrong in
  a BLE advert, and not in a WiFi beacon at all) and an environment path-loss constant that
  swings with walls, bodies and antenna orientation. A "~N m" readout would be a confident
  lie. The honest output is *relative signal*: stronger means closer, nothing more.
- **Raw RSSI is jumpy.** Per-advertisement and per-beacon RSSI swings several dB at rest,
  so an unsmoothed needle flickers between bands and reads as false movement. It must be
  smoothed, and the smoothing must not be so heavy that real movement lags behind the
  operator's steps.

BLE and WiFi differ only in where the samples come from (a live passive scan filtered to an
address, versus beacon capture on a locked channel filtered by BSSID). The decision logic
(smooth, classify, detect direction, decide when the target is lost) is identical, pure,
and has no hardware in it. The device shells are untestable; that logic is exactly where a
regression would otherwise hide.

## Decision

Put the tracker's decision logic in a pure, host-tested core,
`core/proximity_tracker.{h,cpp}`, and give each radio a thin device shell that feeds it live
RSSI samples and renders the reading. The BLE shell ships first (slice-0129); the WiFi-AP
shell is its sibling (ledgered), reusing this core unchanged.

The core is a small stateful `ProximityTracker` driven by `(rssiDbm, nowMs)` samples and
queried with `evaluate(nowMs)`:

- **Smoothing.** A fast EWMA is the needle value. Its weight (`ewmaAlpha`) is the
  "meter feel" knob; the Balanced default damps per-sample jitter while following movement
  within about a second. Tunable, final value is an on-device follow-up.
- **Direction (approaching / receding / steady).** A second, slower EWMA is a baseline;
  the trend is the sign of (fast baseline) outside a dead-band (`trendDeltaDb`). This needs
  no history buffer and stays pure, unlike a ring-buffer slope.
- **Band (close / mid / far).** Thresholds on the smoothed value (`closeDbm`, `farDbm`).
  This drives the red/orange/green colour, labelled as signal strength, never distance.
- **Meter fill.** The smoothed value mapped linearly across a `[floorDbm, ceilDbm]` window
  to 0-100%, clamped. A bar for glanceability; the raw dBm is shown alongside so the number
  is never hidden behind the abstraction.
- **Lost-signal state machine.** `Acquiring` before the first sample; `Tracking` while a
  sample is fresher than `staleMs`; `Lost` (hold the last reading, dimmed, with an age
  counter) once it goes stale; `Dropped` once it exceeds `dropMs`, at which point the shell
  releases the lock and returns to the scanner. This is the operator-chosen "hold, then
  drop" behaviour (2026-10-26): tell the operator the target is gone without stranding them
  on a dead screen.

All tunables live in a `ProximityConfig` with a `kDefaultProximityConfig`; no bare dB or ms
magic values sit in the branches.

## Consequences

- One tested body of logic serves both trackers; the second radio is a shell, not a
  reimplementation. The DRY/testability case carries the extraction now (two consumers
  designed together, and the device shell cannot be host-tested), rather than waiting for a
  third.
- The output is honest by construction: a signal meter, a dBm number, a near/far band and a
  direction arrow, all labelled signal. There is no distance readout to be wrong. This is
  the non-negotiable contract both ledger items name.
- The smoothing defaults are a starting point. Advert and beacon cadences differ by device
  and AP, so the band thresholds and EWMA weights want on-device tuning; that is a hardware
  follow-up, recorded in slice-0129, not a blocker for merge.
- The core assumes samples are fed with a monotonic `nowMs` and RSSI in dBm; it does not
  reach for a clock or a radio itself. Feeding it is the shell's job, which keeps it
  host-testable and keeps the radio-specific sampling (passive BLE scan vs locked-channel
  beacon capture) out of the shared code.
- Keep-not-promote per ADR-0022: red-team feature and on-device internals, bus-factor 1.
