/**
 * @file pwncrack_potfile.cpp
 * @brief Implementation of the pure pwncrack potfile-line parser (ADR-0149).
 */
#include "modules/network/pwncrack_potfile.h"

#include <cstring>

namespace adversary {

namespace {

constexpr size_t kBssidHexChars = 12;  ///< 6 octets, two hex nibbles each.
constexpr int kColonsBeforePassword = 4;  ///< BSSID is field 1, ESSID field 3: four colons precede the password.

/// Convert one hex nibble to its value, or return false if @p c is not a hex digit.
bool hexNibble(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') { out = static_cast<uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<uint8_t>(c - 'A' + 10); return true; }
    return false;
}

/// Parse the BSSID field into six octets. The field sits between two colons so it holds no colon, but
/// the potfile writes the MAC either bare (`aabbccddeeff`) or hyphen-grouped (`aa-bb-cc-dd-ee-ff`); we
/// accept both by skipping a single '-' between octets and requiring exactly 12 hex nibbles. Anything
/// else leaves the BSSID invalid so the row falls back to an SSID-only match rather than keying a
/// zeroed MAC. Mirrors the sibling's verified tolerance (ADR-0149).
bool parseBssidField(const char* field, size_t len, uint8_t out[6]) {
    uint8_t nibbles[kBssidHexChars];
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        const char c = field[i];
        if (c == '-') continue;  // tolerate a hyphen-grouped MAC.
        if (n >= kBssidHexChars) return false;  // more hex than a MAC holds.
        if (!hexNibble(c, nibbles[n])) return false;
        ++n;
    }
    if (n != kBssidHexChars) return false;
    for (size_t i = 0; i < 6; ++i) {
        out[i] = static_cast<uint8_t>((nibbles[2 * i] << 4) | nibbles[2 * i + 1]);
    }
    return true;
}

/// Copy @p len bytes of @p src into @p dst (capacity @p cap including NUL), truncating loudly-safe at
/// the bound and always NUL-terminating.
void copyBounded(char* dst, size_t cap, const char* src, size_t len) {
    const size_t n = (len < cap - 1) ? len : cap - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

bool parsePotfileLine(const char* line, size_t len, PotfileResult& out) {
    if (line == nullptr) return false;

    // Tolerate a CRLF (or bare LF) terminator and trailing spaces a streaming caller may leave on.
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        --len;
    }
    if (len == 0) return false;

    // Locate the first four colons. BSSID is field index 1 (between colon[0] and colon[1]), ESSID is
    // field index 3 (between colon[2] and colon[3]), password is everything after colon[3]. Field
    // index 0 (before colon[0]) is an auxiliary name field and is ignored (ADR-0149).
    int colon[kColonsBeforePassword];
    int found = 0;
    for (size_t i = 0; i < len && found < kColonsBeforePassword; ++i) {
        if (line[i] == ':') colon[found++] = static_cast<int>(i);
    }
    if (found < kColonsBeforePassword) return false;  // not a result row.

    const char* password = line + colon[3] + 1;
    const size_t passwordLen = len - static_cast<size_t>(colon[3] + 1);
    if (passwordLen == 0) return false;  // a row with no password recovers nothing.

    const char* bssidField = line + colon[0] + 1;
    const size_t bssidLen = static_cast<size_t>(colon[1] - colon[0] - 1);
    out.bssidValid = parseBssidField(bssidField, bssidLen, out.bssid);
    if (!out.bssidValid) std::memset(out.bssid, 0, sizeof(out.bssid));

    const char* essid = line + colon[2] + 1;
    const size_t essidLen = static_cast<size_t>(colon[3] - colon[2] - 1);
    copyBounded(out.ssid, sizeof(out.ssid), essid, essidLen);
    copyBounded(out.password, sizeof(out.password), password, passwordLen);
    return true;
}

}  // namespace adversary
