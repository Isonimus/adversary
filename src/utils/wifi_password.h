#pragma once

/**
 * @file wifi_password.h
 * @brief Pure generator for WPA2-safe, human-typeable WiFi passwords.
 *
 * Used to auto-provision a per-device key for the dashboard SoftAP (slice-0036).
 * The function takes externally supplied entropy rather than calling an RNG, so it
 * is deterministic and native-testable; the device feeds it esp_fill_random().
 */

#include <stddef.h>
#include <stdint.h>

namespace adversary {
namespace utils {

// Length of the auto-generated dashboard AP key. 12 chars over a 32-symbol
// alphabet is ~60 bits of entropy and sits well inside the WPA2-PSK 8..63 range.
constexpr size_t kDashboardApPasswordChars = 12;

// Unambiguous WPA2-safe alphabet: 32 symbols, no O/0/I/1/l so the key reads
// cleanly off the device screen. 32 is a power of two, so (byte & 31) indexes it
// without modulo bias.
constexpr const char* WIFI_PASSWORD_ALPHABET =
    "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/**
 * @brief Fill @p out with @p charCount chars drawn from WIFI_PASSWORD_ALPHABET,
 *        mapping one entropy byte per char.
 *
 * @param out        destination buffer (NUL-terminated on success)
 * @param outCap     capacity of @p out in bytes; must be > @p charCount (room for NUL)
 * @param charCount  number of password characters to produce (must be >= 1)
 * @param entropy    source bytes; must hold at least @p charCount bytes
 * @param entropyLen number of valid bytes in @p entropy
 * @return true on success; false (and out[0]='\0' when writable) if any argument is
 *         invalid, the buffer is too small, or there is not enough entropy. Never
 *         produces a short or empty key silently.
 */
bool fillWifiPassword(char* out, size_t outCap, size_t charCount,
                      const uint8_t* entropy, size_t entropyLen);

} // namespace utils
} // namespace adversary
