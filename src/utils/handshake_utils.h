/**
 * @file handshake_utils.h
 * @brief Utility functions for handshake operations
 */

#pragma once

#include "../modules/capture/handshake_capture.h"

namespace adversary {
namespace utils {

/**
 * @brief Get display string for handshake type
 * @param hs Captured handshake structure
 * @param hasPmkid Whether a real PMKID was extracted (clientless capture).
 *                 CapturedHandshake doesn't carry this — pass capture.hasPMKID()
 *                 on the save path; display callers leave it false (session
 *                 handshakes are always EAPOL-family, never pure PMKID).
 * @return Type string: "4WAY", "EAPOL", or "PMKID"
 *
 * Type determination:
 * - "4WAY":  Complete 4-way handshake (all 4 messages)
 * - "EAPOL": Any crackable EAPOL pair — M1+M2 (ANonce+SNonce+MIC) or M2+M3
 * - "PMKID": Genuine extracted PMKID only
 */
// Templated on the record type: works for both the full CapturedHandshake and
// the lightweight SessionHandshakeEntry — it only reads the hasMsg1..4 flags.
template <typename T>
inline const char* getHandshakeTypeString(const T& hs, bool hasPmkid = false) {
    // Complete 4-way handshake.
    if (hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4) {
        return "4WAY";
    }

    // Any crackable EAPOL pair. M2 carries the SNonce + MIC; pairing it with M1
    // (ANonce) or M3 yields a crackable handshake. M1+M2 used to fall through to
    // "PMKID" here — that was the misclassification this fixes.
    if (hs.hasMsg2 && (hs.hasMsg1 || hs.hasMsg3)) {
        return "EAPOL";
    }

    // Genuine clientless PMKID (extracted from M1's RSN IE).
    if (hasPmkid) {
        return "PMKID";
    }

    // Degenerate leftover (e.g. lone M1 with no PMKID): still EAPOL-family.
    return "EAPOL";
}

} // namespace utils
} // namespace adversary
