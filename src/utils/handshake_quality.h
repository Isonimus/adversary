/**
 * @file handshake_quality.h
 * @brief Single crackability-based handshake quality helper (slice-0173).
 *
 * Two scales used to collide on the same concept (one persisted by the save
 * path, one recomputed by the list render), both substantively wrong on
 * crackability. This helper is the one source of truth both consumers call,
 * so stored == displayed by construction.
 *
 * Tiers (hashcat-crackable minimum = ANonce + MIC):
 *   100  PMKID alone, OR M2 paired with M1 or M3 (full 4-way included).
 *   50   Any other non-empty combination (partial, not crackable).
 *   25   Exactly one of M1..M4 and no PMKID (fragment).
 *   0    No messages, no PMKID.
 *
 * Pair validity (ADR-0145 same-client/same-replay) is NOT a helper input: the
 * save path gates writes on hasValidPair() so an invalid set never reaches
 * metadata, and the list screen's SessionHandshakeEntry doesn't carry replay
 * counters.
 */
#pragma once

namespace adversary {
namespace utils {

struct HandshakeQualityFlags {
    bool hasMsg1;
    bool hasMsg2;
    bool hasMsg3;
    bool hasMsg4;
    bool hasPMKID;
};

int computeHandshakeQuality(const HandshakeQualityFlags& f);

}  // namespace utils
}  // namespace adversary
