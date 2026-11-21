---
id: '0161'
title: "Autohunt per-target scoring and exponential backoff"
type: architecture
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Context

Autohunt today treats the target pool as a set of candidates it either picks once or never
touches again in a session. `HandshakeScreen::selectNextTarget()` at
`src/ui/screens/handshake_screen.cpp:830-888` filters out captured, whitelisted, open, and
already-attempted networks, then picks the highest-scoring remainder by a two-term formula:
`score = (RSSI + 100) + (authMode == WIFI_AUTH_WPA2_PSK ? 10 : 0)`. The chosen BSSID is
recorded in a 128-slot ring buffer (`attemptedBssids_` at
`src/ui/screens/handshake_screen.h:304`) via `markNetworkAttempted()`, and
`wasNetworkAttempted()` makes every subsequent `selectNextTarget()` skip it for the rest of
the session.

Two problems compound.

First, the "attempted = never retry" rule is too strict. A target can fail to yield a capture
for reasons that change within minutes: the only client was briefly off-channel, the window
elapsed just before a four-way, or the operator moved and the RSSI jumped. The 128-slot
cap defers the pain rather than solving it: once full, LRU eviction silently brings old
BSSIDs back and the engine re-hammers them as if they were fresh, with no memory of the
prior failure. Operators see autohunt either give up on a reachable target or churn through
the same handful of dead ones.

Second, the scoring formula is RSSI-dominated and ignores the one signal that actually
predicts a four-way capture: whether any client is associated with the AP at all. PMKID
solicitation (ADR-0137) is clientless, but any Deauth or Both intent against a client-less
AP wastes the window. The scanner already runs a `StationScanner` pass during `LOCKING`
(`src/modules/wifi/station_scanner.h:154`) to feed `handshake_capture.start()`, so the
machinery exists to know which BSSIDs had observed clients, but nothing persists that
knowledge for the next round's selection.

The PMF-aware policy from ADR-0153 added the `pmfRequired` bit to the scan record and used
it to pick an effective strategy at `LOCKING` time. It did not touch target selection, and
it explicitly deferred per-target scoring and backoff to this ADR.

Credit: 0ldev/Politician implements auto-targeting with a per-target backoff schedule in a
similar red-team context; the shape of this decision follows that prior art.

## Decision

Replace the binary "attempted" tombstone with a per-BSSID score-and-backoff state, and
extend the scoring formula to read presence and prior-failure signals.

**Target-state map.** Replace the `attemptedBssids_[128][6]` ring buffer with a flat
`std::array<TargetMemory, 128>` keyed by BSSID, where

```cpp
struct TargetMemory {
    uint8_t  bssid[6];        // all-zero == free slot
    uint8_t  attempts;         // consecutive no-capture windows (reset on capture)
    uint8_t  observedClients; // saturating count from the last LOCKING-time station scan
    uint32_t lastAttemptMs;   // millis() at last window start
    uint32_t nextEligibleMs;  // millis() before which selection skips this BSSID
};
```

128 slots stays as the cap. Replacement on overflow evicts the slot with the *oldest*
`lastAttemptMs` whose `nextEligibleMs` is already in the past (a target that was retried
long ago and succeeded or was dropped from scan), falling back to oldest overall. This
keeps active backoffs intact until they expire. Added RAM at the screen instance: 128 x
(6 + 1 + 1 + 4 + 4) = 2 KB, up from the prior 768 B for the ring buffer. Allocation is
static, lives on the `HandshakeScreen` instance, and is not held during `IDLE`.

**Pure scoring function.** The score formula moves to a free function in
`src/utils/autohunt_scoring.h` so the native test rig can cover it with no WiFi stack:

```cpp
namespace utils {

struct TargetScoreInputs {
    int8_t  rssi;              // dBm as reported by WiFi.RSSI()
    bool    isWpa2Psk;
    bool    pmfRequired;
    bool    intentIsDeauthOnly;
    uint8_t observedClientsHint; // prior-session memory; 0 == unknown
    uint8_t priorFailures;      // consecutive no-capture attempts; 0 == fresh
};

int16_t scoreTarget(const TargetScoreInputs& in) noexcept;

} // namespace utils
```

Weights, each cited in a unit test:

- **Base RSSI**: `score += rssi + 100`. Matches the existing normalization; a -40 dBm AP
  starts at 60, a -90 dBm at 10.
- **WPA2 preference**: `+10` when `isWpa2Psk`. Preserves the existing bias toward WPA2-PSK
  over WPA/WPA3 mixed modes that are less reliably captured.
- **PMF demote on Deauth-only intent**: `-20` when `pmfRequired && intentIsDeauthOnly`.
  Under ADR-0153 the effective strategy resolves to `Skipped (PMF)` for this case, so
  the target burns zero time but should not be *preferred* over a scoring-tied non-PMF
  AP. The demote is score-only, not a skip: an unambiguous best PMF-required target is
  still selected and surfaces the `Skipped (PMF)` status.
- **Client-presence boost**: `+15` when `observedClientsHint > 0`. Fed by the `LOCKING`
  station scan's `getStationCountForAP()` latched into the `TargetMemory` when the
  window ends. First selection sees `0` and is RSSI-driven; subsequent rounds prefer
  BSSIDs where we actually saw a client.
- **Prior-failure demote**: `-5` per `priorFailures` up to a floor of `-20`. A four-times-
  failed target becomes ~20 points worse than a fresh one of equal RSSI, without being
  suppressed outright (the backoff gate handles outright suppression).

Returns a signed `int16_t`; selection picks the max.

**Exponential backoff.** When a `WAITING -> NEXT_TARGET` transition closes a window with
no capture, the just-tried target gets `attempts++` and
`nextEligibleMs = now + BASE_COOLDOWN_MS * (1 << min(attempts-1, 4))`, capped at
`MAX_COOLDOWN_MS`. Constants:

```cpp
constexpr uint32_t BASE_COOLDOWN_MS = 60 * 1000;    // 60 s after the first miss
constexpr uint32_t MAX_COOLDOWN_MS  = 15 * 60 * 1000; // 15 min ceiling
```

Schedule: 60 s, 120 s, 240 s, 480 s, 900 s, 900 s, ... A capture clears the slot
(`attempts = 0`, `nextEligibleMs = 0`) so a dormant target re-enters the pool at full
score the moment a client comes back. Backoff is a hard skip in `selectNextTarget()`:
`now < nextEligibleMs` means the target is not considered this round, regardless of score.

**Observed-clients latch.** When `NEXT_TARGET` runs, the LOCKING-time station-scan count is
read back via `StationScanner::getStationCountForAP(target->bssid)` and written into the
slot's `observedClients` (saturating to 255). Subsequent `selectNextTarget()` reads this
back through `TargetScoreInputs.observedClientsHint`. No scanner or protocol change; we are
latching information already in hand.

**Debug trace.** Target selection emits a single `LOG_D("AUTOHUNT", ...)` per considered
BSSID with score and backoff state when the log level is debug, so a field session can be
reviewed from the serial log for why a specific target was or was not chosen. No info-level
spam; the existing `LOG_I("Selected target...")` line stays.

## Consequences

- Autohunt stops silently never-retrying a target that failed once and later changed
  conditions. Over a long session the backoff schedule means a stubborn target is still
  tried roughly every 15 minutes rather than monopolizing cycles or being permanently
  retired.
- The scoring formula is now visible, unit-testable, and documented in one pure function;
  future adjustments (another sibling hint, a channel-utilization input, a priority tag
  from a scan annotation) extend `TargetScoreInputs` without touching the autohunt state
  machine.
- Target-selection cost grows from O(N) one-pass filter with an O(k) ring-buffer scan on
  each candidate (k up to 128), to O(N) one-pass filter with an O(k) linear BSSID lookup.
  Same asymptotic; the per-candidate constant is a touch higher (we read four fields and
  compare against millis()). N is at most `MAX_NETWORKS = 50`, k = 128, so the whole pass
  is ~6400 comparisons per target selection, once per `AUTO_WAIT_TIME` window. Negligible.
- RAM at the screen instance grows by ~1.3 KB (2 KB map vs 768 B ring buffer). The
  `HandshakeScreen` is only live when the Handshake screen is on top; this does not change
  the steady-state budget the TLS upload path (ADR-0042) depends on.
- The 128-slot cap is now a memory of real backoffs rather than a tombstone list, so the
  eviction policy must preserve active backoffs. The "oldest expired first, then oldest
  overall" rule above handles the common case (session rolls on, old attempts expire,
  slot frees cleanly) and the pathological case (128 active backoffs; we evict the
  oldest active to make room, accepting that a target may be retried early once if the
  operator has somehow accumulated 128 targets in simultaneous backoff).
- The observed-clients latch depends on ADR-0153's LOCKING-time `StationScanner` already
  running. If a future change reorders or skips that scan, the hint reverts to `0` and the
  score formula gracefully degrades to the RSSI + WPA2 + PMF path; no new failure mode.
- `pmfRequired` is now read at *selection* time, not only at *effective strategy* time.
  ADR-0153 added the bit and used it to resolve the strategy inside the window; this ADR
  consumes it in the scoring upstream, so the Skipped (PMF) outcome is deprioritized
  rather than permitted to win the round against a non-PMF alternative of equal signal.
- The slice (slices/0161) carries the Given/When/Then Definition of Done, the pure-score
  unit tests, and the integration write-up. The scoring and backoff constants above are
  the ones the slice tests pin: any later tuning lands in a superseding ADR, not an
  in-place edit.
