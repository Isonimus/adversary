#pragma once

#include <stdint.h>

/**
 * @file pmf_resolver.h
 * @brief Pure resolver mapping an autohunt intent + a target's PMF requirement
 *        onto the effective per-target strategy and a reason code for the log.
 *
 * ADR-0153 made the user-chosen Strategy (ADR-0139) a declared intent, and the
 * device resolves an effective strategy per target at `NEXT_TARGET`. The whole
 * nine-cell truth table lives here, exercised by test_pmf_resolver.cpp.
 *
 * `CaptureIntent` is the authoritative name for the ADR-0139 enum; the UI side
 * (`AutoHuntStrategy`) aliases onto this so both share one enum (slice-0154).
 */

namespace adversary {
namespace utils {

enum class CaptureIntent : uint8_t {
    Deauth,
    PMKID,
    Both
};

enum class EffectiveStrategy : uint8_t {
    Deauth,
    PMKID,
    Both,
    Skipped  ///< Target advanced past without transmitting on it.
};

enum class PmfResolveReason : uint8_t {
    NONE,                     ///< No PMF-driven change; effective matches intent or intent is PMKID.
    PMF_DEGRADED,             ///< Effective is PMKID because the target is PMF-required; deauth half dropped.
    PMF_DEAUTH_INEFFECTIVE    ///< Target skipped: Deauth intent, PMF required, degrade toggle off.
};

struct ResolvedStrategy {
    EffectiveStrategy strategy;
    PmfResolveReason reason;
};

/**
 * @brief Resolve the effective capture strategy for the next target.
 *
 * @param intent          The operator's declared Strategy for this hunt.
 * @param pmfRequired     Whether the target advertises MFPR=1 (PMF required).
 * @param degradeToPmkid  The "Degrade on PMF" toggle (default on). Only relevant
 *                        when @p intent is Deauth and the target is PMF-required.
 * @return The effective strategy plus a reason code that the hunt log records
 *         when it differs from the intent.
 *
 * Nine-cell truth table (ADR-0153):
 * - Any intent, pmfRequired=false            -> effective = intent, reason = NONE
 * - PMKID, pmfRequired=true                  -> effective = PMKID, reason = NONE
 * - Both,  pmfRequired=true                  -> effective = PMKID, reason = PMF_DEGRADED
 * - Deauth,pmfRequired=true, degrade=on      -> effective = PMKID, reason = PMF_DEGRADED
 * - Deauth,pmfRequired=true, degrade=off     -> effective = Skipped, reason = PMF_DEAUTH_INEFFECTIVE
 *
 * Pure function: no state, no ESP dependency, native-testable.
 */
inline ResolvedStrategy resolveEffectiveStrategy(CaptureIntent intent,
                                                  bool pmfRequired,
                                                  bool degradeToPmkid) {
    if (!pmfRequired) {
        EffectiveStrategy effective = EffectiveStrategy::Deauth;
        switch (intent) {
            case CaptureIntent::Deauth: effective = EffectiveStrategy::Deauth; break;
            case CaptureIntent::PMKID:  effective = EffectiveStrategy::PMKID;  break;
            case CaptureIntent::Both:   effective = EffectiveStrategy::Both;   break;
        }
        return {effective, PmfResolveReason::NONE};
    }

    switch (intent) {
        case CaptureIntent::PMKID:
            return {EffectiveStrategy::PMKID, PmfResolveReason::NONE};
        case CaptureIntent::Both:
            return {EffectiveStrategy::PMKID, PmfResolveReason::PMF_DEGRADED};
        case CaptureIntent::Deauth:
            return degradeToPmkid
                ? ResolvedStrategy{EffectiveStrategy::PMKID, PmfResolveReason::PMF_DEGRADED}
                : ResolvedStrategy{EffectiveStrategy::Skipped, PmfResolveReason::PMF_DEAUTH_INEFFECTIVE};
    }
    // Unreachable: every enum value handled above. Keeps the compiler happy.
    return {EffectiveStrategy::Skipped, PmfResolveReason::PMF_DEAUTH_INEFFECTIVE};
}

} // namespace utils
} // namespace adversary
