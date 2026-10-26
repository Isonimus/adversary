---
id: '0130'
title: "WiFi-AP proximity tracker: locked-channel beacon capture, not full-sweep scan"
type: architecture
status: accepted
date: 2026-10-26
supersedes: []
superseded_by: []
---

## Context

ADR-0128 put the tracker's decision logic in a pure core and shipped the BLE shell
(slice-0129). The WiFi-AP shell is its sibling: lock onto an AP chosen in the WiFi scanner
and guide the operator toward it by signal, to find a rogue AP, an evil twin, a pineapple,
a hidden-camera or IoT AP, or a dropped node. ADR-0128 settled everything radio-agnostic
(smoothing, bands, direction, the lost-signal state machine, the honest "signal not
distance" contract). What it deferred to this ADR is the one thing that is WiFi-specific and
load-bearing: **where the per-sample RSSI comes from**, because the obvious source is a trap.

The obvious source is `WiFi.scanNetworks` (`wifi_scanner.cpp`), which the scanner already
uses. It sweeps all channels and takes roughly 3 to 5 seconds per sweep. A hot/cold meter
driven off it would only update every few seconds and lag the operator's steps by that much:
the exact sluggishness class the operator confirmed on-device for karma's full-sweep probe
view (ledgered under the ScrollableList scroll-only item). A meter that trails you by three
seconds is worse than none, because it points where you *were*.

The responsive source is already in the tree. `station_scanner.cpp:132` reads
`rx_ctrl.rssi` off every frame in promiscuous mode, filtered to one BSSID, on a channel it
sets with `esp_wifi_set_channel`. An AP beacons about ten times a second regardless of
whether any client is talking to it, and a hidden AP still beacons (it only omits the SSID).
So per-frame promiscuous capture on the AP's own channel gives a sample roughly every
100 ms, on idle and hidden APs alike.

## Decision

Drive the WiFi-AP tracker from **locked-channel promiscuous beacon capture filtered by
BSSID**, never from a full-channel scan.

- **Lock the channel.** The scanner already knows the AP's channel (`NetworkInfo::channel`),
  so the tracker sets that channel once with `wifi_utils::setChannel` and stays there. No
  hopping, no sweeping.
- **Capture management frames from the target.** A new singleton module
  `modules/wifi/ap_rssi_monitor.{h,cpp}`, modelled on `StationScanner`, enables promiscuous
  mode with the management-frame filter (`wifi_utils::managementFrameFilter`) and, in its
  callback, keeps the RSSI of frames whose Address 2 (the transmitter) is the target BSSID.
  For a beacon or probe response that transmitter is the AP itself, so this is the AP's own
  signal at the operator's position. It exposes the latest RSSI, its timestamp, and a
  monotonic sample sequence so the shell feeds each new frame into the core exactly once.
- **The frame test is a pure function.** The only non-trivial logic in the device path is
  "is this a management frame from the target BSSID", which is pure byte parsing of the
  802.11 header. It goes in `core/wifi_frame.h` (`managementFrameFromBssid`) and is
  host-tested, so the device module stays a thin radio wrapper around tested logic, matching
  the ADR-0128 shell/core split.
- **The shell reuses the core unchanged.** `wifi_ap_tracker_screen` owns the monitor, feeds
  each new sample to a `ProximityTracker` built from `kDefaultProximityConfig`, and renders
  the same meter / dBm / band / direction UI as the BLE tracker.

**Channel change is treated as loss, not chased.** Infrastructure APs do not hop channels
the way our scanner does; a channel change is a reconfiguration, not normal behaviour. If
the target leaves its channel we simply stop hearing it, and the ADR-0128 state machine does
the honest thing: hold the last reading dimmed (`Lost`), then release the lock after the
drop timeout (`Dropped`) and return to the scanner, where a fresh scan re-finds it on its
new channel. Re-acquiring by scanning from inside the tracker would reintroduce the
multi-second full-sweep lag this decision exists to avoid, so we do not.

**Single radio, so the tracker owns it.** Promiscuous capture on a locked channel cannot
coexist with a concurrent scan or attack on this hardware. The tracker is a dedicated screen
that monopolises the radio for its lifetime and restores normal state on exit; this is
acceptable for a focused "walk toward it" task and needs no arbitration.

## Consequences

- The meter follows beacons at roughly 100 ms cadence instead of a 3-to-5 second sweep, so
  it tracks walking pace, and it works on idle and hidden APs because both still beacon.
- Reusing the ADR-0128 core means the WiFi side is one module, one screen, one menu item and
  one pure frame-filter, with no new smoothing, banding or state logic and no new infra:
  promiscuous mode, channel set and per-frame RSSI all already exist.
- The honest-signal contract from ADR-0128 holds unchanged: a signal meter, a dBm number, a
  near/far band and a direction, all labelled signal, never distance. A WiFi beacon carries
  no usable calibrated TxPower either, so distance would be the same confident lie.
- `kDefaultProximityConfig` is the BLE-tuned starting point; WiFi beacon RSSI near an AP sits
  in a different range, so the bands and EWMA weight want on-device tuning against a known
  AP. That is a hardware follow-up (ledgered alongside the BLE tuning), not a merge blocker.
- A target that changes channel drops after the timeout rather than being re-found live.
  This is a deliberate honesty/latency trade: the alternative (scan to re-acquire) is the
  very lag this ADR rejects. Re-finding it is one ESC and a re-scan away.
- The WiFi-AP menu action is keyed `L` ("Locate AP"), not `T`, because `T` is already Evil
  Twin on the scanner ActionMenu; the BLE sibling could use `T` only because that menu had
  it free.
- Keep-not-promote per ADR-0022: red-team feature and on-device radio internals, bus-factor 1.
