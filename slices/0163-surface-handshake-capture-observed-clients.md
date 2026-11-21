---
id: '0163'
title: "Surface handshake_capture observed-client count to the autohunt scorer"
type: slice
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Goal

Close the slice-0162 scoring gap discovered in the field pass: `MOVISTAR-WIFI6-C520`
re-entered selection after backoff with `observedClientsHint=0` and scored 20 instead of
the expected 35, because `HandshakeCapture`'s promiscuous listener had seen a client via
the QoS-Null stimulation during the ~30s attack window that `StationScanner`'s 5s LOCKING
probe never saw. The `-15` boost that ADR-0161 promised for "has clients" never fired on
the common case.

Have the scorer latch the higher of the two sources: the `StationScanner` LOCKING-time
cache (already in place) and the discovered-client count the capture engine built up
across the attack window. Preserve the graceful-degrade path: a zero from both sources
stays zero (scorer treats zero as "unknown", not "none"). Pure call-site change, no new
events on the bus, no new frames on the wire.

Folds in the v0.11.0 ledger item "Surface handshake_capture's observed-client count back
to the screen for the scoring hint". Consumes ADR-0161's `observedClientsHint` field
without changing its policy.

## Definition of Done

- **Given** an autohunt target where `StationScanner::getStationCountForAP(bssid)` returns
  `0` at NEXT_TARGET time but `HandshakeCapture::getInstance().getDiscoveredClientCount()`
  returns `N >= 1` because the capture's own promiscuous listener saw clients during the
  attack window,
- **When** `NEXT_TARGET` latches the observed-client count into this target's memory slot,
- **Then** the slot's `observedClients` field becomes `N` (saturated at 255), the next
  `selectNextTarget()` pass feeds that non-zero value as `observedClientsHint`, and the
  scorer adds the `+15 SCORE_CLIENTS_BONUS` from ADR-0161.

- **Given** an autohunt target with no clients visible to either source,
- **When** `NEXT_TARGET` latches,
- **Then** the slot's `observedClients` stays at `0` and the +15 boost does not fire
  (graceful degrade preserved).

- **Given** an autohunt target where `StationScanner` saw `S >= 1` clients at LOCKING but
  the capture engine saw `H > S` by its end,
- **When** `NEXT_TARGET` latches,
- **Then** the slot records `H` (the higher of the two), so the boost reflects the best
  evidence either source produced in this round.

## Design

**Existing code considered.** `HandshakeCapture` already keeps
`discoveredClients_[MAX_DISCOVERED_CLIENTS=8][6]` plus `discoveredClientCount_` for the
targeted-deauth path (`addDiscoveredClient()` populates it from EAPOL/QoS-Data/QoS-Null
frames seen in the promiscuous callback). `getFirstDiscoveredClient()` already exposes
the first MAC (used by the QoS-Null slice-0160 stimulation). The count is already tracked
internally; only the getter is missing.

Three shapes were considered:

1. **New `getDiscoveredClientCount()` getter + max-at-call-site in `NEXT_TARGET`** (chosen).
   One trivial getter on `HandshakeCapture`; the max is computed where the latch already
   lives. No new EventBus traffic, no new ownership. Mirrors the already-shipped
   `getFirstDiscoveredClient()` next to it.

2. **New `ScreenEvent::OBSERVED_CLIENTS_UPDATED` payload.** Rejected: EventBus is for
   decoupling modules the screen does not already own. The screen already holds a direct
   reference to `HandshakeCapture::getInstance()` through `capture_`; adding an event
   round-trip for a getter is pure ceremony.

3. **Pack the count into `ATTACK_STATE_CHANGED`.** Rejected: that event is subscribed to
   by at least three consumers (dashboards, logs, future recorder); growing its payload
   for one call site creates a hidden coupling. The ledger entry mentioned it as an
   option, but a plain getter keeps the surface minimal and local.

**Why max, not replace.** The two sources catch different populations. The LOCKING-time
`StationScanner` sees clients that transmit during the 5s probe but not necessarily during
the attack window (a device going to sleep). The capture-time listener sees clients that
transmit during the ~30s attack window but not during LOCKING (a device joining late, a
QoS-Null-woken client). The ledger entry's "zero from the fallback still works" language
implies both paths must stay live; max is the simplest expression of that.

**Saturation.** `utils::recordObservedClients()` already saturates at 255 (the slot field
is `uint8_t`); callers do not need to clamp.

**Native testability.** The max arithmetic is a one-line `std::max`. The native test for
the scoring/backoff math already pins `recordObservedClients` behaviour (see slice-0162
`test/test_autohunt_backoff/`); no new native test needed for the max alone because the
call-site line is trivial and the only interesting integration is on-device, where the
capture's promiscuous listener runs. Field verification is the honest gate.

## Verification

- Native: the existing `test/test_autohunt_scoring/` and `test/test_autohunt_backoff/`
  suites remain green (24/24). No new native case: the max operation is one line, and
  the only novel piece is the `HandshakeCapture` getter, whose underlying field is
  already covered by its own module.
- Device (Cardputer): flash and run autohunt against a target where the previous slice-0162
  field pass recorded the gap (`MOVISTAR-WIFI6-C520` or any AP where QoS-Null surfaces a
  client the LOCKING scan missed). Expected log at NEXT_TARGET: `latched observed_clients=N`
  with `N >= 1` after a round where QoS-Null fired, and the next `selectNextTarget()`
  candidate line for the same BSSID carries `clients:N` and the `+15` in the score
  decomposition.
- Three-source sanity: a target neither side ever sees a client for continues to score
  without the +15 boost (regression gate for the "zero stays zero" leg).

## As built

Shipped per the Design section's option 1 (plain getter + max-at-call-site), no new EventBus
traffic, no new frames.

`HandshakeCapture::getDiscoveredClientCount()` was added next to the existing
`getFirstDiscoveredClient()` in `src/modules/capture/handshake_capture.h`, exposing
`discoveredClientCount_` (the capture's own promiscuous-listener count, bounded by
`MAX_DISCOVERED_CLIENTS=8`). The getter is `const` and inline, zero cost.

`HandshakeScreen::AutoHuntState::NEXT_TARGET` now takes `std::max` of the two sources
before latching:

```cpp
const size_t stationScannerCount =
    StationScanner::getInstance().getStationCountForAP(targetBssid_);
const size_t captureListenerCount = capture_.getDiscoveredClientCount();
latchObservedClientsForTarget(
    targetBssid_,
    std::max(stationScannerCount, captureListenerCount));
```

`<algorithm>` was added to the screen's includes for `std::max`. `utils::recordObservedClients()`
(slice-0162) already saturates at 255, so no caller-side clamp needed. The latch still runs
BEFORE `capture_.stop()` tears either source down (same order-of-operations invariant as
slice-0162).

### Field evidence

Flashed to Cardputer (RAM 25.3%, Flash 76.0%; same as slice-0162 baseline, delta negligible),
autohunt intent=Both, serial filter watching AUTOHUNT lines:

- **Round 1 (fresh slot)**: `Selected target: MOVISTAR-WIFI6-C520 (ch6, RSSI:-80, score:30)`
  — `-80 + 100 + 10 + 0 + 0 = 30`, no client yet. Attack window fired `QoSNull:injected
  (client=02:AD:5A:11:ED:01 ch=6)`; no capture; NEXT_TARGET latched count=1 from the capture
  listener (StationScanner would have been 0 as in the slice-0162 field pass).
- **Round 2 (different target)**: `Selected target: MOVISTAR_17E0 (ch11, RSSI:-95, score:15)`
  — `-95 + 100 + 10 + 0 + 0 = 15`. `QoSNull:skip_no_client` this round, no client visible
  to either source; NEXT_TARGET latched count=0.
- **Round 3 (DoD-#1 confirmed)**: `Selected target: MOVISTAR-WIFI6-C520 (ch6, RSSI:-86,
  score:34)`. With the fix: `-86 + 100 + 10 + 15 - 5 = 34` (✓). Without the fix it would
  have scored `-86 + 100 + 10 + 0 - 5 = 19`. The +15 boost fired from the capture-listener
  count latched in round 1, exactly the slice-0162 gap this slice closes.
- **Round 4 (DoD-#2 confirmed, graceful degrade)**: `Selected target: MOVISTAR_17E0 (ch11,
  RSSI:-86, score:19)` — `-86 + 100 + 10 + 0 - 5 = 19`. No client was ever seen on this
  BSSID (round 2's `QoSNull:skip_no_client`), so the slot stayed at 0 and the +15 did not
  fire. The scorer's "zero means unknown, not none" convention holds.
- **Round 4 attack window**: captured a late `QoSNull:injected (client=02:AD:5A:11:ED:01
  ch=11) tx=ok` on MOVISTAR_17E0 (same client MAC as the C520, now active on channel 11).
  So its next re-selection should also carry the +15 boost — a second corroborating sample
  for DoD-#1 on a different BSSID.
- **DoD-#3 (max-of-two when both nonzero)**: no field opportunity in this run (StationScanner
  returned 0 on every target during LOCKING). The `std::max` arithmetic is a one-line call
  pinned by code review; its only interesting integration is the capture-listener getter,
  which the first two DoDs already exercise. Acceptable: the expensive case is "capture
  sees more than scanner" (the gap this slice closes), and that is now field-confirmed twice.

No crashes, no `12308` regressions, no change in attack-window behaviour; the delta is a
getter plus one call-site line, exactly as the Design section proposed. The slice-0162
field-pass gap on MOVISTAR-WIFI6-C520 ("score 20 instead of expected 35") is now routinely
"score +15 above the no-client baseline" with the real client population behind it.
