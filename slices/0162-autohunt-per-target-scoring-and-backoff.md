---
id: '0162'
title: "Autohunt per-target scoring and exponential backoff"
type: slice
status: accepted
date: 2026-11-21
supersedes: []
superseded_by: []
---

## Goal

Make autohunt spend its cycles on productive targets. Replace the tombstone ring buffer in
`HandshakeScreen` (`attemptedBssids_[128]`) with a per-BSSID state that remembers prior
failures, caches observed clients from the LOCKING station scan, and gates re-selection
through an exponential backoff schedule. Move the scoring arithmetic into a pure free
function so native tests cover it. Policy and schedule are defined in ADR-0161; this slice
implements it and verifies the math.

Folds in the v0.11.0 lead ledger item under "Next release" (Autohunt per-target scoring +
exponential backoff) and consumes the `pmfRequired` bit ADR-0153 added to the scan record
at selection time, not only at effective-strategy time.

## Definition of Done

- **Given** an autohunt session with two WPA2-PSK targets at the same RSSI, one with
  `pmfRequired` and the user's intent is `Deauth` only,
- **When** `selectNextTarget()` runs,
- **Then** the non-PMF target is chosen and the PMF-required one is scored lower by exactly
  the `-20` demote weight from ADR-0161, verifiable from the debug log and the pure
  scoring unit test.

- **Given** a target whose window has just closed with no capture,
- **When** `NEXT_TARGET` transitions and the autohunt engine updates its memory,
- **Then** the target's `attempts` increments, its `nextEligibleMs` is set to
  `now + BASE_COOLDOWN_MS * (1 << min(attempts-1, 4))` capped at `MAX_COOLDOWN_MS`, and
  subsequent `selectNextTarget()` passes skip this BSSID until `millis() >= nextEligibleMs`.

- **Given** a target that was in backoff and has since been observed with associated
  clients during a prior LOCKING scan,
- **When** backoff expires and the target is considered again,
- **Then** the stored `observedClients` hint adds `+15` to its score (from ADR-0161) and
  a successful capture clears the slot (`attempts = 0`, `nextEligibleMs = 0`,
  `observedClients` preserved for the record), verifiable from the native unit test.

## Design

The policy and the constants (`BASE_COOLDOWN_MS = 60 * 1000`, `MAX_COOLDOWN_MS = 15 min`,
weights +10/-20/+15/-5-per-failure, floor -20) live in ADR-0161. This slice carries the
implementation: three code sites change.

**New pure module**: `src/utils/autohunt_scoring.h` and `.cpp`. Holds
`struct TargetScoreInputs` and `int16_t scoreTarget(const TargetScoreInputs&) noexcept`.
No Arduino, no WiFi headers; compiles for `native` and `esp32`. Also holds a plain
`struct TargetMemory` and the backoff helpers
`uint32_t nextEligibleAfterMiss(uint8_t attempts, uint32_t now)` and
`bool isInBackoff(const TargetMemory&, uint32_t now)`. Keeping the backoff math pure lets
one test fixture drive scenarios without a FreeRTOS tick.

**Replace the ring buffer** in `handshake_screen.h` (`attemptedBssids_[128][6]` +
`attemptedCount_` + `attemptedHead_`) with
`std::array<utils::TargetMemory, 128> targetMemory_{};` and an `uint8_t
targetMemoryCount_`. `markNetworkAttempted(bssid)` becomes `recordAttempt(bssid, now)`
which increments `attempts` and writes `nextEligibleMs`. `wasNetworkAttempted(bssid)`
becomes `isInBackoff(bssid, now)` returning `true` only while the slot is active.
`recordCapture(bssid)` clears the backoff on success, preserving the observed-clients
hint. A new `recordObservedClients(bssid, count)` runs at `NEXT_TARGET` to latch the
LOCKING-time station-scan count into the slot.

**Rewrite `selectNextTarget()`** in `handshake_screen.cpp` to:

1. Loop candidates once, filtering captured / whitelisted / open / in-backoff.
2. Build `TargetScoreInputs` from the fresh scan (RSSI, authMode, PMF bit from
   `WiFiScanner::getInstance().getNetworks()`, current intent, `observedClientsHint` and
   `priorFailures` from the memory slot).
3. Call `utils::scoreTarget(inputs)`; keep the max.
4. Emit `LOG_D("AUTOHUNT", "...")` per candidate with score and backoff state (gated by
   the existing log level).
5. On a pick, write the slot's `lastAttemptMs = millis()` so eviction's "oldest first"
   rule stays current.

**`NEXT_TARGET` body** in `handshake_screen.cpp` latches observed clients
(`StationScanner::getInstance().getStationCountForAP(targetBssid_)`) into the slot via
`recordObservedClients(targetBssid_, count)` *before* calling `capture_.stop()` (the
scanner is stopped by then but the result cache survives until `clearResults()`). If a
capture landed this round, `recordCapture(targetBssid_)` runs first.

**What existing code was considered**: `tls_upload.cpp` carries a simple 300 ms -> 600 ms
-> 1200 ms backoff for TLS retries; the pattern is right but the shape is different
(TLS retries are back-to-back on one connection; autohunt's backoff is per-target across
many windows). Not reused. The `attempted...` ring-buffer helpers are retired, not
adapted: the new map is keyed by BSSID with per-slot timers, so the existing API does
not fit. `WiFiScanner::getNetworks()` is used as-is to read the PMF bit (same as the
ADR-0154 effective-strategy resolver). `StationScanner::getStationCountForAP()` is used
as-is to read observed clients at window close; no scanner change.

## Verification

- `test/test_autohunt_scoring.cpp` (new, `native` env). Covers each weight independently
  and in combination:
    - RSSI normalization (`-40 dBm -> 60`, `-90 dBm -> 10`).
    - WPA2 preference adds exactly `+10`.
    - PMF + Deauth-only demotes by exactly `-20`; PMF without Deauth-only does not.
    - Observed-clients hint `>0` adds `+15`; `0` adds nothing.
    - Prior-failures demote `-5` per attempt, floor at `-20`.
    - Composite case from the Definition of Done first scenario: two targets at the same
      RSSI, one PMF-required, intent=Deauth-only, the non-PMF wins by 20 points.
- `test/test_autohunt_backoff.cpp` (new, `native` env). Covers backoff math and the
  memory-slot behaviour with a hand-driven `now` clock:
    - `nextEligibleAfterMiss(0, now) == now + 60_000`.
    - Doubling schedule: 60 s, 120 s, 240 s, 480 s, 900 s cap, 900 s cap (cap hit).
    - `isInBackoff` true while `now < nextEligibleMs`, false otherwise.
    - `recordAttempt` -> `recordAttempt` -> `recordCapture` resets `attempts = 0` and
      `nextEligibleMs = 0`, preserves `observedClients`.
- `pio test -e native` green.
- `pio run -e cardputer` and `-e m5stick` compile clean (serially, `-j 2`).
- Device smoke test on a multi-target run (operator confirmed): a target whose window
  closes without a capture is not re-selected for at least 60 s; operator sees it come
  back after the cooldown and sees `observedClients` factor into the retry.

## As built

Implemented ADR-0161 end-to-end. New header-only pure module
`src/utils/autohunt_scoring.h` carries `TargetScoreInputs`, `TargetMemory`,
`scoreTarget()`, `nextEligibleAfterMiss()`, `isInBackoff()`, `recordAttemptMiss()`,
`recordCaptureSuccess()`, `recordObservedClients()`, and the slot helpers
`isSlotFree()` / `bssidEquals()`. Weights and schedule pinned exactly per
ADR-0161: `SCORE_RSSI_OFFSET=100`, `SCORE_WPA2_BONUS=10`,
`SCORE_PMF_DEAUTH_DEMOTE=-20`, `SCORE_CLIENTS_BONUS=15`,
`SCORE_PRIOR_FAILURE_STEP=-5` (floor `-20`), `BASE_COOLDOWN_MS=60_000`,
`MAX_COOLDOWN_MS=900_000`, `MAX_COOLDOWN_SHIFT=4`.

`HandshakeScreen` retired the 128-entry attempted-BSSID ring buffer and its
`markNetworkAttempted/wasNetworkAttempted` helpers. The new state is a flat
`std::array<utils::TargetMemory, 128>` with `findSlot`, `allocateSlot`,
`recordAttemptMissForTarget`, `recordCaptureForTarget`,
`latchObservedClientsForTarget`, and `isTargetInBackoff`. Eviction prefers the
slot with the oldest expired backoff, falling back to the oldest slot overall so
a 128-active-backoff pool still recycles. `selectNextTarget()` filters captured /
whitelisted / open / in-backoff then scores each remaining candidate through
`utils::scoreTarget()`; the picked slot gets `recordAttemptMissForTarget()`
preemptively so the target stays out of re-selection until the window closes.
`NEXT_TARGET` latches `StationScanner::getStationCountForAP(targetBssid_)` into
the slot before `capture_.stop()` and clears the slot on a successful save.
`startCapture()` resets `targetMemory_` + `targetMemoryCount_` so a new hunt
does not inherit prior backoffs or client hints.

Tests: new `test_autohunt_scoring` (12 cases pinning each weight plus the DoD-#1
composite) and `test_autohunt_backoff` (12 cases pinning the 60s→15min schedule,
the `isInBackoff` gate including a millis-wrap case, and the capture-resets-
attempts behaviour). `pio test -e native` green for both suites (24/24). Device
builds green serial: Cardputer flash 76.0% / RAM 25.3%; M5StickC Plus2 flash
79.5% / RAM 20.9%. `lint-docs.mjs` green, 161 docs.

Field smoke test on the deployment ADV (Cardputer, 2026-11-21, Auto Hunt mode,
Strategy=Both), 8+ consecutive rounds captured from the serial log:

- Round 1: `Selected target: MOVISTAR-WIFI6-C520 (ch6, RSSI:-79, score:31)` →
  31 = -79 + 100 + 10 (fresh, no WPA2 bonus check fails).
- Round 2: `Selected target: MOVISTAR_17E0 (ch11, RSSI:-93, score:17)` → 17 =
  -93 + 100 + 10. Different BSSID than round 1; the previously-tried
  MOVISTAR-WIFI6-C520 was in backoff and skipped.
- Round 3: `Selected target: 6356 (ch1, RSSI:-93, score:17)` → 17 = -93 + 100 + 10.
  Third unique BSSID.
- Round 4: `Selected target: ALDARA REPETIDO (ch6, RSSI:-92, score:18)` → 18 =
  -92 + 100 + 10. Fourth unique BSSID; no re-selection in any 60s window.
- Round 5: `Selected target: MOVISTAR_17E0 (ch11, RSSI:-96, score:9)` → 9 =
  -96 + 100 + 10 - 5. Backoff expired on MOVISTAR_17E0 (first miss ~3-4 min
  earlier); re-entered with the -5 prior-failure demote confirmed to the dB.
- Between rounds, when every scanned BSSID was in backoff,
  `AUTOHUNT: State: IDLE_SCAN` fired at least three times instead of
  LRU-rehammering the pool, demonstrating the honest-exhaustion behaviour the
  pre-ADR-0161 ring buffer could not provide.
- No `Guru Meditation`, `abort()`, `Backtrace:`, or `assert failed` across the
  full run.

DoD scenarios confirmed:

- **DoD-#1** (PMF-required + Deauth-only demotes by -20): pinned in
  `test_autohunt_scoring.cpp:test_two_wpa2_same_rssi_pmf_vs_non_pmf_non_pmf_wins_by_twenty`.
  Field pass ran under intent=Both, so the demote did not have a chance to fire
  (all surveyed APs were non-PMF and the demote only applies to Deauth-only
  intent); the math is pinned by the native suite.
- **DoD-#2** (window close → attempts++ and `nextEligibleMs` set; subsequent
  selects skip until expiry): field-confirmed across rounds 1-5. The
  MOVISTAR_17E0 round-5 pick returning at a prior-failure-demoted score is the
  observation that pins the whole backoff path end to end.
- **DoD-#3** (observed-clients hint adds +15, capture clears the slot): the
  scoring math is pinned in
  `test_autohunt_scoring.cpp:test_observed_clients_hint_adds_fifteen` and the
  reset path in
  `test_autohunt_backoff.cpp:test_capture_clears_backoff_preserves_observed_clients`.
  The field pass, however, surfaced a real gap: when MOVISTAR-WIFI6-C520 came
  back in round 6 after its cooldown (score 20 = -85 + 100 + 10 - 5), the
  observed-clients boost did **not** fire even though the AP visibly had a
  client (02:AD:5A:11:ED:01, injected into by QoS-Null in round 1). Root cause:
  `StationScanner::getStationCountForAP()` only sees clients that transmit
  during the 5 s LOCKING window, and the client that `HandshakeCapture` later
  discovered via its own promiscuous listener during the ~30 s attack window
  was never visible to the latch. The scoring math is correct; the signal is
  starved. Follow-up `LEDGER.md` entry opened under v0.11.0 to have
  `HandshakeCapture` surface its observed-client count to the screen at
  `capture_.stop()`, so the latch reads the broader source. The slice-0162
  implementation ships with its graceful-degrade path: a zero hint drops the
  scoring to RSSI + WPA2 + PMF only, which is exactly what the field run
  exercised.

Non-regressions observed in the same session: the known-cosmetic
`[E][STA.cpp:530] disconnect(): STA disconnect failed! 0x3001:
ESP_ERR_WIFI_NOT_INIT` line (its own `LEDGER.md` bug entry under v0.11.0) kept
printing once per deauth start as expected; `AUTOHUNT: State:` transitions
flowed cleanly across SCANNING → LOCKING → ATTACKING → WAITING → NEXT_TARGET →
SCANNING without drops, and the screen neither crashed nor hung at the slot
latch point even when the scanner surfaced previously-latched BSSIDs.
