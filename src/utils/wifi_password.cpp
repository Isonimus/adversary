/**
 * @file wifi_password.cpp
 * @brief Pure WPA2-safe WiFi password generator (see wifi_password.h).
 */

#include "wifi_password.h"

namespace adversary {
namespace utils {

bool fillWifiPassword(char* out, size_t outCap, size_t charCount,
                      const uint8_t* entropy, size_t entropyLen) {
    // Guard the misuse cases loudly rather than emit a short/empty key: WPA2 would
    // silently fall back to an OPEN AP for a key under 8 chars, so a bad result here
    // must be a detectable failure, not a weak password.
    if (out != nullptr && outCap >= 1) {
        out[0] = '\0';
    }
    if (out == nullptr || entropy == nullptr) return false;
    if (charCount == 0) return false;
    if (outCap <= charCount) return false;      // need room for charCount + NUL
    if (entropyLen < charCount) return false;

    // 32-symbol alphabet -> low 5 bits index it without modulo bias.
    constexpr uint8_t kAlphabetMask = 31;
    for (size_t i = 0; i < charCount; ++i) {
        out[i] = WIFI_PASSWORD_ALPHABET[entropy[i] & kAlphabetMask];
    }
    out[charCount] = '\0';
    return true;
}

} // namespace utils
} // namespace adversary
