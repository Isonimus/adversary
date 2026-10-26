---
id: '0129'
title: "BLE device proximity tracker (hot/cold RSSI finder)"
type: slice
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Goal

Let the operator physically locate a selected BLE device by signal: lock onto it from the
BLE scanner and show an honest hot/cold meter that rises as they close in. Implements the
BLE shell of ADR-0128. The WiFi-AP tracker is the ledgered sibling and reuses the same core.

## Definition of Done

- **Given** a device selected in the BLE scanner
- **When** the operator picks "Track device" from its ActionMenu
- **Then** a tracker screen locks onto that address and shows a smoothed signal meter, the
  raw dBm, a close/mid/far band (green/orange/red) and an approaching/receding arrow, all
  labelled as signal, never distance.

- **Given** the target keeps advertising
- **When** the operator moves toward or away from it
- **Then** the meter follows within about a second (Balanced EWMA) without flickering bands
  at rest, and the arrow reflects the direction of travel.

- **Given** the target stops being heard
- **When** it stays silent past the stale window
- **Then** the screen holds the last reading dimmed with a seconds-since-seen counter, and
  once it exceeds the drop timeout (15 s) it releases the lock and returns to the scanner.

- **Given** no distance can be honestly computed from RSSI
- **When** anything is shown
- **Then** no metres / distance figure appears anywhere; the readout is signal only.

## Design

Per ADR-0128:

- `core/proximity_tracker.{h,cpp}` is the pure core: `ProximityTracker` fed `(rssiDbm,
  nowMs)` samples, queried by `evaluate(nowMs)` for a `ProximityReading` (state, smoothed
  dBm, raw dBm, band, trend, meter %, age). Dual EWMA (needle + trend baseline), band
  thresholds, and the Acquiring/Tracking/Lost/Dropped state machine. Host-tested.
- A new `BLE_TRACKER` screen owns a dedicated continuous **passive** scan (the no-heap-growth
  path per ADR-0103), filters incoming advertisements to the locked address, and feeds each
  matching RSSI into the tracker with `millis()`. It renders the meter, dBm, band and arrow,
  and on `Dropped` returns to `BLE_SCANNER`.
- The BLE scanner ActionMenu gains a `T` "Track device" item (`ble_scanner_screen.cpp`
  `setupActionMenu`), routing the selected `BLEDeviceInfo` to the tracker.

## Verification

- `pio test -e native`: `test_proximity_tracker` added (smoothing, bands, trend, lost/drop
  state machine, meter clamp); full suite green.
- `pio run -e cardputer -j 2` and `pio run -e m5stick -j 2`: both SUCCESS (built serially).
- On-device (follow-up, hardware): confirm the meter climbs approaching a real beacon,
  falls receding, the arrow tracks movement, and tune the Balanced EWMA weight + the
  close/mid/far dBm thresholds against a known device.

## As built

As designed. The shared pure core is `core/proximity_tracker.{h,cpp}` (dual-EWMA needle +
trend baseline, band thresholds, Acquiring/Tracking/Lost/Dropped state machine); the device
shell is `ui/screens/ble_tracker_screen.{h,cpp}`. The BLE scanner ActionMenu gained a `T`
"Track device" item (`ble_scanner_screen.cpp`).

Launch is UAF-safe by design: the scanner **queues** a new `BLE_TRACK_TARGET_SELECTED` event
(address in the `attackTarget` payload's bssid, name in ssid) rather than navigating inline.
The main loop drains it in `processQueue()` after input unwinds, so the scanner screen is not
deleted mid-call. A passive `launchBleTracker` handler (no stopAllAttacks, no ATTACKING
transition, unlike the attack path) does `navigateWithParams(BLE_TRACKER, ...)` and sets the
slice-0031 return breadcrumb, so ESC and a dropped lock both return to the BLE scanner. The
tracker owns a continuous passive scan and feeds each fresh advertisement (new `lastSeen`)
from the locked address into the core.

Verification run this change:
- `pio test -e native`: 936/936 (was 925; test_proximity_tracker adds 11 cases).
- `pio run -e cardputer -j 2`: SUCCESS. Flash 75.7% (2,530,579 B, +4,620 over slice-0127's
  2,525,959 B).
- `pio run -e m5stick -j 2`: SUCCESS. RAM 21.0% (68,808 B), Flash 78.6% (2,473,407 B, +4,592
  over slice-0127's 2,468,815 B). Built serially after cardputer, one at a time.
- Docs: index + lint green (129 on disk; 128 staged, excluding the untracked 0033).

On-device verification and threshold tuning remain a hardware follow-up (ledgered): confirm
the meter climbs approaching a real beacon and falls receding, the arrow tracks movement, and
tune the Balanced EWMA weight + the close/mid/far dBm thresholds against a known device.
