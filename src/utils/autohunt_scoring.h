#pragma once

#include <stdint.h>
#include <string.h>

/**
 * @file autohunt_scoring.h
 * @brief Pure scoring and backoff helpers for Autohunt target selection (ADR-0161).
 *
 * Policy, weights, and the backoff schedule are defined in ADR-0161. This header is
 * header-only so both the device build and the native test rig share the same bytes.
 * No Arduino, no WiFi headers, no FreeRTOS; `now` is a caller-provided `uint32_t` in
 * millis().
 */

namespace adversary {
namespace utils {

// ---- scoring ----------------------------------------------------------------

/**
 * @brief Inputs the scorer reads for one candidate target.
 *
 * Pulled from fresh scan results plus the per-target memory slot: the scorer itself
 * never touches scanners or state.
 */
struct TargetScoreInputs {
    int8_t  rssi;                 ///< As reported by WiFi.RSSI(); -100..0 dBm range.
    bool    isWpa2Psk;            ///< WIFI_AUTH_WPA2_PSK, same as the pre-ADR-0161 formula.
    bool    pmfRequired;          ///< From the scan record's `pmfRequired` bit (ADR-0153).
    bool    intentIsDeauthOnly;   ///< True when the operator's intent is Deauth, not PMKID/Both.
    uint8_t observedClientsHint;  ///< Prior-session latch (0 == unknown).
    uint8_t priorFailures;        ///< Consecutive no-capture windows (0 == fresh).
};

/// Added to RSSI so a -100 dBm AP starts at 0 and -0 dBm at 100 (ADR-0161).
constexpr int16_t SCORE_RSSI_OFFSET = 100;
/// WPA2-PSK preference weight (ADR-0161, preserved from the pre-0161 formula).
constexpr int16_t SCORE_WPA2_BONUS = 10;
/// Demote applied when the target requires PMF AND the intent is Deauth-only (ADR-0161).
constexpr int16_t SCORE_PMF_DEAUTH_DEMOTE = -20;
/// Added when the target's slot carries a non-zero observed-clients hint (ADR-0161).
constexpr int16_t SCORE_CLIENTS_BONUS = 15;
/// Per-prior-failure demote (ADR-0161).
constexpr int16_t SCORE_PRIOR_FAILURE_STEP = -5;
/// Floor the cumulative prior-failure demote at this value (ADR-0161).
constexpr int16_t SCORE_PRIOR_FAILURE_FLOOR = -20;

/**
 * @brief Compute the selection score for a candidate target.
 *
 * Pure: no state, no clock. The max score wins the round in `selectNextTarget()`.
 *
 * The prior-failure contribution is clamped so a very-stale target does not drop into
 * the signed-arithmetic noise; the backoff gate (isInBackoff) is the mechanism that
 * actually suppresses a target, not the score floor.
 */
inline int16_t scoreTarget(const TargetScoreInputs& in) noexcept {
    int16_t score = static_cast<int16_t>(in.rssi) + SCORE_RSSI_OFFSET;
    if (in.isWpa2Psk) score += SCORE_WPA2_BONUS;
    if (in.pmfRequired && in.intentIsDeauthOnly) score += SCORE_PMF_DEAUTH_DEMOTE;
    if (in.observedClientsHint > 0) score += SCORE_CLIENTS_BONUS;

    int16_t failureDemote = SCORE_PRIOR_FAILURE_STEP * static_cast<int16_t>(in.priorFailures);
    if (failureDemote < SCORE_PRIOR_FAILURE_FLOOR) failureDemote = SCORE_PRIOR_FAILURE_FLOOR;
    score += failureDemote;

    return score;
}

// ---- backoff ---------------------------------------------------------------

/// Base cooldown after the first no-capture window (ADR-0161).
constexpr uint32_t BASE_COOLDOWN_MS = 60u * 1000u;
/// Hard ceiling on per-target backoff (ADR-0161).
constexpr uint32_t MAX_COOLDOWN_MS  = 15u * 60u * 1000u;
/// Doubling exponent cap: schedule caps at `BASE_COOLDOWN_MS << MAX_COOLDOWN_SHIFT`.
constexpr uint8_t  MAX_COOLDOWN_SHIFT = 4;

/**
 * @brief Compute the millis() threshold a target becomes eligible again at.
 *
 * `attempts` is the *post-increment* count (1 after the first miss, 2 after the second,
 * ...). Schedule: 60s, 120s, 240s, 480s, 960s, and capped at 900s = MAX_COOLDOWN_MS on
 * every subsequent attempt. The shift cap stops arithmetic overflow at absurd attempt
 * counts.
 */
inline uint32_t nextEligibleAfterMiss(uint8_t attempts, uint32_t now) noexcept {
    if (attempts == 0) attempts = 1; // defensive: a caller that recorded no miss has none
    uint8_t shift = static_cast<uint8_t>(attempts - 1);
    if (shift > MAX_COOLDOWN_SHIFT) shift = MAX_COOLDOWN_SHIFT;
    uint32_t cooldown = BASE_COOLDOWN_MS << shift;
    if (cooldown > MAX_COOLDOWN_MS) cooldown = MAX_COOLDOWN_MS;
    return now + cooldown;
}

/**
 * @brief Per-BSSID slot the autohunt engine keeps between rounds.
 *
 * `bssid` all-zero marks a free slot. Eviction picks the oldest slot whose
 * `nextEligibleMs` is already <= now (an expired backoff), falling back to the oldest
 * slot overall so an operator who has accumulated 128 live backoffs still gets the
 * least-recently-touched one recycled.
 */
struct TargetMemory {
    uint8_t  bssid[6]{};
    uint8_t  attempts{0};
    uint8_t  observedClients{0};
    uint32_t lastAttemptMs{0};
    uint32_t nextEligibleMs{0};
};

inline bool isSlotFree(const TargetMemory& slot) noexcept {
    for (int i = 0; i < 6; ++i) {
        if (slot.bssid[i] != 0) return false;
    }
    return true;
}

inline bool bssidEquals(const uint8_t a[6], const uint8_t b[6]) noexcept {
    return memcmp(a, b, 6) == 0;
}

/**
 * @brief True while the slot's backoff is still active at `now`.
 *
 * `millis()` wraps at ~49 days; we treat `(now - nextEligibleMs)` as a signed delta so a
 * wrap during a session does not keep a target suppressed forever.
 */
inline bool isInBackoff(const TargetMemory& slot, uint32_t now) noexcept {
    if (slot.nextEligibleMs == 0) return false;
    int32_t delta = static_cast<int32_t>(now - slot.nextEligibleMs);
    return delta < 0;
}

/**
 * @brief Register a no-capture window against the slot and schedule the next eligibility.
 *
 * Called from `NEXT_TARGET` after the window closes without a capture.
 */
inline void recordAttemptMiss(TargetMemory& slot, uint32_t now) noexcept {
    if (slot.attempts < 255) slot.attempts = static_cast<uint8_t>(slot.attempts + 1);
    slot.lastAttemptMs = now;
    slot.nextEligibleMs = nextEligibleAfterMiss(slot.attempts, now);
}

/**
 * @brief Clear the backoff on a successful capture, preserving the observed-clients hint.
 */
inline void recordCaptureSuccess(TargetMemory& slot, uint32_t now) noexcept {
    slot.attempts = 0;
    slot.lastAttemptMs = now;
    slot.nextEligibleMs = 0;
    // observedClients kept: the fact that this AP had clients is still true.
}

/**
 * @brief Latch an observed client count (saturating) into the slot.
 *
 * The `NEXT_TARGET` body reads `StationScanner::getStationCountForAP(bssid)` and feeds
 * it here before the scanner's cache is cleared.
 */
inline void recordObservedClients(TargetMemory& slot, size_t count) noexcept {
    uint8_t clamped = count > 255 ? 255 : static_cast<uint8_t>(count);
    slot.observedClients = clamped;
}

} // namespace utils
} // namespace adversary
