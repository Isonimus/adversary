#pragma once

#include <stdint.h>
#include <string.h>

/**
 * @file pmkid_utils.h
 * @brief Pure PMKID extraction from an EAPOL-Key (message 1) payload.
 *
 * Split out of HandshakeCapture so the parse is unit-testable on the native host
 * (modules/capture/ is not in the native build_src_filter, so the capture .cpp
 * never linked into a test; this header is under utils/, which does). The offset
 * arithmetic here is the part that was wrong for the entire life of the feature:
 * the PMKID counter never moved because Key Data was read two bytes early, into
 * the Key MIC field, so the KDE walk always desynced.
 */

namespace adversary {
namespace utils {

// EAPOL-Key field offsets measured from the Key Descriptor byte, i.e. with the
// 4-byte EAPOL header (version/type/length) already skipped. Derived from the
// 802.11 EAPOL-Key layout: KeyInfo(2) + KeyLen(2) + Replay(8) + Nonce(32) +
// IV(16) + RSC(8) + KeyID(8) + MIC(16) places the Key Data Length field at 93
// and the Key Data itself at 95. (Validated against the ANonce, which the same
// mapping puts at offset 13 where the capture code reads it correctly.)
constexpr uint16_t EAPOL_KEY_DATA_LEN_OFFSET = 93;
constexpr uint16_t EAPOL_KEY_DATA_OFFSET = 95;
constexpr uint16_t EAPOL_HEADER_LEN = 4;

// PMKID KDE inside Key Data: Tag=0xDD, Len, OUI=00-0F-AC, Type=0x04, PMKID(16).
constexpr uint8_t PMKID_KDE_TAG = 0xDD;
constexpr uint8_t PMKID_KDE_OUI_0 = 0x00;
constexpr uint8_t PMKID_KDE_OUI_1 = 0x0F;
constexpr uint8_t PMKID_KDE_OUI_2 = 0xAC;
constexpr uint8_t PMKID_KDE_TYPE = 0x04;
constexpr uint8_t PMKID_KDE_MIN_LEN = 20;  // OUI(3) + Type(1) + PMKID(16)
constexpr uint8_t PMKID_KDE_VALUE_OFFSET = 6;  // Tag(1) + Len(1) + OUI(3) + Type(1)
constexpr uint16_t PMKID_LENGTH = 16;

/**
 * @brief Extract a PMKID from an EAPOL-Key message-1 payload.
 *
 * @param eapolData Pointer to the EAPOL header (version/type/length/descriptor...).
 * @param eapolLen  Length of the EAPOL payload in bytes.
 * @param out       Receives the 16-byte PMKID when one is found.
 * @return true when the Key Data carries a PMKID KDE; false otherwise.
 *
 * Pure and bounds-safe: never reads past @p eapolLen, so a truncated or
 * malformed frame returns false instead of walking off the buffer.
 */
inline bool extractPmkidFromEapol(const uint8_t* eapolData, uint16_t eapolLen,
                                  uint8_t out[PMKID_LENGTH]) {
    if (eapolData == nullptr || out == nullptr) return false;
    // Need the EAPOL header plus everything up to and including the Key Data.
    if (eapolLen < EAPOL_HEADER_LEN + EAPOL_KEY_DATA_OFFSET) return false;

    const uint8_t* keyFrame = eapolData + EAPOL_HEADER_LEN;
    const uint16_t keyFrameLen = eapolLen - EAPOL_HEADER_LEN;

    const uint16_t keyDataLen =
        (static_cast<uint16_t>(keyFrame[EAPOL_KEY_DATA_LEN_OFFSET]) << 8) |
        keyFrame[EAPOL_KEY_DATA_LEN_OFFSET + 1];
    if (keyDataLen == 0) return false;
    if (EAPOL_KEY_DATA_OFFSET + keyDataLen > keyFrameLen) return false;  // claims more than present

    const uint8_t* keyData = keyFrame + EAPOL_KEY_DATA_OFFSET;

    // Walk the Key Data element list (Tag, Len, Value) looking for the PMKID KDE.
    uint16_t i = 0;
    while (i + 2 <= keyDataLen) {
        const uint8_t tag = keyData[i];
        const uint8_t len = keyData[i + 1];
        if (i + 2 + len > keyDataLen) break;  // element claims past the buffer
        if (tag == PMKID_KDE_TAG && len >= PMKID_KDE_MIN_LEN &&
            keyData[i + 2] == PMKID_KDE_OUI_0 &&
            keyData[i + 3] == PMKID_KDE_OUI_1 &&
            keyData[i + 4] == PMKID_KDE_OUI_2 &&
            keyData[i + 5] == PMKID_KDE_TYPE) {
            memcpy(out, keyData + i + PMKID_KDE_VALUE_OFFSET, PMKID_LENGTH);
            return true;
        }
        // An element header is 2 bytes (Tag, Len), so a well-formed element always
        // occupies 2 + len bytes; len is a uint8_t, so this advances by >= 2 every
        // iteration and can never stall. (The old `if (len == 0) ++i` desynced the
        // walk by one byte and dropped a valid PMKID KDE sitting after any element.)
        i += 2 + len;
    }
    return false;
}

} // namespace utils
} // namespace adversary
