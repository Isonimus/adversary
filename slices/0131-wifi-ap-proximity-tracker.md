---
id: '0131'
title: "WiFi-AP proximity tracker (hot/cold beacon RSSI finder)"
type: slice
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Goal

Let the operator physically locate a selected WiFi AP by signal: lock onto it from the WiFi
scanner and show the same honest hot/cold meter the BLE tracker shows, driven by the AP's
beacons on its own channel. Implements the WiFi shell of ADR-0128 with the data source
decided in ADR-0130. Reuses the shared `core/proximity_tracker.*` core unchanged.

## Definition of Done

- **Given** an AP selected in the WiFi scanner
- **When** the operator picks "Locate AP" from its ActionMenu
- **Then** a tracker screen locks onto that BSSID, sets the AP's channel, and shows a
  smoothed signal meter, the raw dBm, a close/mid/far band (green/orange/red) and an
  approaching/receding direction, all labelled as signal, never distance.

- **Given** the AP keeps beaconing (idle or hidden counts: both still beacon)
- **When** the operator moves toward or away from it
- **Then** the meter follows within about a second without flickering bands at rest, and the
  direction reflects the travel, because beacons arrive about every 100 ms on the locked
  channel (ADR-0130), not every full-sweep scan.

- **Given** the AP stops being heard (out of range, powered off, or moved channel)
- **When** it stays silent past the stale window
- **Then** the screen holds the last reading dimmed with a seconds-since-seen counter, and
  once it exceeds the drop timeout it releases the lock and returns to the scanner, where a
  fresh scan re-finds it (including on a new channel).

- **Given** no distance can be honestly computed from beacon RSSI
- **When** anything is shown
- **Then** no metres / distance figure appears anywhere; the readout is signal only.

## Design

Per ADR-0130:

- `core/wifi_frame.h` adds the pure, host-tested predicate `managementFrameFromBssid(frame,
  len, bssid)`: parse the 802.11 frame-control, confirm a management frame, and match
  Address 2 (the transmitter) against the target BSSID. The only non-trivial device-path
  logic, lifted into the tested core.
- `modules/wifi/ap_rssi_monitor.{h,cpp}` is a singleton modelled on `StationScanner`: lock
  the channel, enable promiscuous with the management filter, and in the callback keep the
  latest RSSI + timestamp + a monotonic sample sequence for frames passing the predicate.
- A new `WIFI_AP_TRACKER` screen owns the monitor, feeds each new sample into a
  `ProximityTracker(kDefaultProximityConfig)` with `millis()`, renders the meter/dBm/band/
  direction, and on `Dropped` returns to `SCANNER`. Same UI as the BLE tracker.
- The WiFi scanner ActionMenu gains an `L` "Locate AP" item (`scanner_screen.cpp`
  `setupActionMenu`). `T` is already Evil Twin, so the sibling's `T` "Track" mnemonic is not
  available here (ADR-0130). Launch is UAF-safe: the scanner **queues** a new
  `WIFI_TRACK_TARGET_SELECTED` event (BSSID, name, channel in the `attackTarget` payload)
  drained in the main loop, not an inline navigate that would delete the scanner mid-input.

## Verification

- `pio test -e native`: `test_wifi_frame` added (too-short frame, data frame rejected,
  beacon from BSSID accepted, beacon from a different BSSID rejected, boundary length). The
  proximity core is unchanged and its suite still passes.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (built serially).
- On-device (follow-up, hardware): confirm the meter climbs approaching a known AP, falls
  receding, the direction tracks movement, and works on a hidden/idle AP; tune the EWMA
  weight + close/mid/far dBm thresholds for WiFi beacon ranges (shared follow-up with the
  BLE tracker, since both use `kDefaultProximityConfig`).

## As built

As designed. The data source is `modules/wifi/ap_rssi_monitor.{h,cpp}`, a StationScanner-shaped
singleton that locks the channel, enables promiscuous management-frame capture, and keeps the
latest RSSI + timestamp + a monotonic sample sequence for frames passing the pure
`core/wifi_frame.h` predicate (`managementFrameFromBssid`). The device shell is
`ui/screens/wifi_ap_tracker_screen.{h,cpp}`, feeding each new sample into a
`ProximityTracker(kDefaultProximityConfig)` and rendering the same meter/dBm/band/direction as
the BLE tracker. The WiFi scanner ActionMenu gained an `L` "Locate AP" item.

Launch is UAF-safe: the scanner **queues** a new `WIFI_TRACK_TARGET_SELECTED` event (BSSID,
name, channel in the `attackTarget` payload) drained in the main loop, not an inline navigate.
A passive `launchWifiApTracker` handler (no stopAllAttacks, no ATTACKING transition) does
`navigateWithParams(WIFI_AP_TRACKER, ...)` and sets the slice-0031 return breadcrumb, so ESC
and a dropped lock both return to the WiFi scanner.

The meter render is duplicated from the BLE tracker shell deliberately: these are the first two
proximity consumers, and the rule-of-three says extract on the third, not the second. A ledger
line records that a third tracker (or any change touching both renders) should extract a shared
`ProximityMeter` UI component.

Verification run this change:
- `pio test -e native`: 942/942 (was 936; `test_wifi_frame` adds 6 cases). The proximity core
  is unchanged and its 11 cases still pass.
- `pio run -e cardputer -j 2`: SUCCESS. Flash 75.8% (2,534,915 B, +4,336 over slice-0129's
  2,530,579 B). RAM 26.5% (86,776 B).
- `pio run -e m5stick -j 2`: SUCCESS. RAM 21.0% (68,816 B), Flash 78.8% (2,477,675 B, +4,268
  over slice-0129's 2,473,407 B). Built serially after cardputer, one at a time.
- Docs: index + lint green (130 on disk; 129 staged, excluding the untracked 0033).

On-device verification and WiFi threshold tuning remain a hardware follow-up (ledgered, shared
with the BLE tracker since both use `kDefaultProximityConfig`): confirm the meter climbs
approaching a known AP and falls receding, the direction tracks movement, it works on a
hidden/idle AP, and tune the EWMA weight + close/mid/far dBm thresholds for WiFi beacon ranges.
