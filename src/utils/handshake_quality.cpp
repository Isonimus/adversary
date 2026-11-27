/**
 * @file handshake_quality.cpp
 * @brief Implementation of the shared handshake quality tier (slice-0173).
 */

#include "handshake_quality.h"

namespace adversary {
namespace utils {

int computeHandshakeQuality(const HandshakeQualityFlags& f) {
    if (f.hasPMKID) return 100;
    // ANonce + MIC is the hashcat-crackable minimum. M1 and M3 both carry the
    // ANonce; M2 carries the MIC.
    if (f.hasMsg2 && (f.hasMsg1 || f.hasMsg3)) return 100;

    const int msgCount = (f.hasMsg1 ? 1 : 0) + (f.hasMsg2 ? 1 : 0)
                       + (f.hasMsg3 ? 1 : 0) + (f.hasMsg4 ? 1 : 0);
    if (msgCount == 0) return 0;
    if (msgCount == 1) return 25;
    return 50;
}

}  // namespace utils
}  // namespace adversary
