#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file rsn_ie_parser.h
 * @brief Pure RSN IE walker that returns the two PMF bits (MFPC and MFPR).
 *
 * Needed because the pinned IDF 5.5 scan API (`wifi_ap_record_t` in
 * esp_wifi_types_generic.h) carries no PMF fields and discards the beacon RSN
 * IE buffer after building the record, so the two advertised bits have to be
 * read off a captured beacon or probe-response frame. See slice-0154, and the
 * LEDGER "own a shared patched ESP-IDF" item for the broader blind spot.
 *
 * Bit layout (802.11 RSN Capabilities field, 2 octets little-endian; byte 0 is
 * least significant): bit 7 = MFPC (PMF capable), bit 6 = MFPR (PMF required).
 */

namespace adversary {
namespace utils {

constexpr uint8_t RSN_IE_ELEMENT_ID = 0x30;
constexpr uint8_t RSN_CAP_MFPR_BIT = 0x40;  // byte 0 bit 6
constexpr uint8_t RSN_CAP_MFPC_BIT = 0x80;  // byte 0 bit 7

/**
 * @brief Extract the MFPC and MFPR bits from an RSN IE.
 *
 * @param ieBuf   Pointer to the first byte of the IE (the element id).
 * @param ieLen   Total length of the IE buffer, including the 2-byte header.
 * @param outMfpc Set to true if MFPC (capable) bit is advertised.
 * @param outMfpr Set to true if MFPR (required) bit is advertised.
 * @return true when a full RSN Capabilities field was read; false on wrong
 *         element id, truncation at any boundary, or null output pointers. On
 *         false the output booleans are left untouched.
 *
 * Pure and bounds-safe: never reads past @p ieLen or past the declared IE
 * length byte (whichever is smaller). Caller passes the IE including its
 * element-id and length header, so a mismatched id fails fast.
 */
inline bool parseRsnPmfBits(const uint8_t* ieBuf, size_t ieLen,
                            bool* outMfpc, bool* outMfpr) {
    if (ieBuf == nullptr || outMfpc == nullptr || outMfpr == nullptr) return false;
    if (ieLen < 2) return false;
    if (ieBuf[0] != RSN_IE_ELEMENT_ID) return false;

    const uint8_t declaredLen = ieBuf[1];
    if (ieLen < static_cast<size_t>(2) + declaredLen) return false;

    // Walk the Value portion. All offsets are relative to ieBuf[2].
    const uint8_t* value = ieBuf + 2;
    size_t off = 0;

    // Version (2 bytes).
    if (off + 2 > declaredLen) return false;
    off += 2;

    // Group cipher suite (4 bytes).
    if (off + 4 > declaredLen) return false;
    off += 4;

    // Pairwise cipher suite list: count (2 bytes LE) + count*4 bytes.
    if (off + 2 > declaredLen) return false;
    const uint16_t pairwiseCount =
        static_cast<uint16_t>(value[off]) |
        (static_cast<uint16_t>(value[off + 1]) << 8);
    off += 2;
    const size_t pairwiseBytes = static_cast<size_t>(pairwiseCount) * 4;
    if (off + pairwiseBytes > declaredLen) return false;
    off += pairwiseBytes;

    // AKM suite list: count (2 bytes LE) + count*4 bytes.
    if (off + 2 > declaredLen) return false;
    const uint16_t akmCount =
        static_cast<uint16_t>(value[off]) |
        (static_cast<uint16_t>(value[off + 1]) << 8);
    off += 2;
    const size_t akmBytes = static_cast<size_t>(akmCount) * 4;
    if (off + akmBytes > declaredLen) return false;
    off += akmBytes;

    // RSN Capabilities field (2 bytes). The MFPC/MFPR bits live in byte 0.
    if (off + 2 > declaredLen) return false;
    const uint8_t capLow = value[off];

    *outMfpc = (capLow & RSN_CAP_MFPC_BIT) != 0;
    *outMfpr = (capLow & RSN_CAP_MFPR_BIT) != 0;
    return true;
}

} // namespace utils
} // namespace adversary
