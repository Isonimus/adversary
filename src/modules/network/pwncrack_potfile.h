/**
 * @file pwncrack_potfile.h
 * @brief Pure, host-tested parsing of pwncrack's `download_potfile_script` results (ADR-0149).
 *
 * Replaces the parse that lived inside an ESP32-only lambda in PwncrackService::fetchCrackedResults,
 * which (1) matched the SSID fallback against field index 0 and (2) applied at most 32 results per
 * fetch, silently dropping the rest. The field layout is measured, not guessed: a firsthand probe
 * (esp32-wifi-sapper ADR-0109 decision #5, verified against the live site and the community
 * pwncrack-addons plugin) showed each line is colon-delimited with BSSID at field index 1, ESSID at
 * field index 3, and the password everything after the fourth colon (so a password containing ':'
 * survives). Field index 0 is an auxiliary name field and is ignored.
 *
 * The parser is pure and line-oriented so the device fetcher can stream lines without buffering the
 * whole account, and so the parse and the no-cap collection are both unit-tested on the native lane.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace adversary {

/// Longest potfile line the streaming reader buffers. Potfile rows are short (a MAC, an ESSID, a
/// password); a longer line is not a valid result row and is truncated to this bound harmlessly.
constexpr size_t kPotfileLineMax = 256;

/// One parsed cracked row. Sized to the manifest's own fields (HandshakeRecord::ssid is [33],
/// pwncrackPassword is [65]) so a result copies in without a second truncation.
struct PotfileResult {
    char ssid[33];       ///< ESSID (field index 3); may be empty if the field was empty.
    char password[65];   ///< Password (everything after the fourth colon), kept verbatim.
    uint8_t bssid[6];    ///< BSSID (field index 1) when @ref bssidValid; otherwise zeroed.
    bool bssidValid;     ///< true when the BSSID field parsed to six octets.
};

/**
 * @brief Parse one pwncrack potfile line into @p out.
 *
 * Tolerates a trailing CR/LF and trailing spaces the endpoint has been seen to pad with. A line is a
 * result row only when it has at least four colons and a non-empty password; such a row always fills
 * @p out (SSID from field 3, password after the fourth colon, BSSID from field 1 when it parses).
 *
 * @param line the raw line (need not be NUL-terminated; read over @p len only).
 * @param len  length of @p line in bytes.
 * @param out  populated only when the function returns true; untouched otherwise.
 * @return true when @p line is a result row (>=4 colons, non-empty password); false otherwise.
 */
bool parsePotfileLine(const char* line, size_t len, PotfileResult& out);

/**
 * @brief Drive a line source through @ref parsePotfileLine and emit every valid result, uncapped.
 *
 * This is the exact collection the device runs: it pulls lines, parses each, and invokes @p onResult
 * for every valid row with no fixed limit, so a large potfile is never truncated (ADR-0149). The
 * caller decides what a result costs: the device matches against resident capture metadata (pure RAM)
 * and buffers only the matched rows for a post-`http.end()` apply, so no SD work happens while the TLS
 * socket is open.
 *
 * @param nextLine fills @p buf (NUL-terminated, capacity @p cap) with the next line and returns its
 *                 length, or a negative value when the source is exhausted.
 * @param onResult invoked once per valid @ref PotfileResult, in line order.
 * @return the number of valid result rows emitted.
 */
template <class NextLine, class OnResult>
int collectPotfileResults(NextLine&& nextLine, OnResult&& onResult) {
    char line[kPotfileLineMax];
    int validRows = 0;
    for (;;) {
        const int len = nextLine(line, sizeof(line));
        if (len < 0) break;
        PotfileResult result;
        if (parsePotfileLine(line, static_cast<size_t>(len), result)) {
            onResult(result);
            ++validRows;
        }
    }
    return validRows;
}

}  // namespace adversary
