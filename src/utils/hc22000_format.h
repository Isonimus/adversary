#pragma once

/**
 * @file hc22000_format.h
 * @brief Pure builders for hashcat 22000 (.hc22000) hash lines.
 *
 * Extracted from handshake_save.h so the line format is testable off-device and
 * lives in one place (the PMKID line used to be duplicated across two writers).
 *
 * hashcat 22000 line layout (both types share the 9 fields):
 *   WPA*TYPE*PMKID-or-MIC*MAC_AP*MAC_STA*ESSID*ANONCE*EAPOL*MESSAGEPAIR
 * TYPE is the one discriminator a cracking tool keys on:
 *   01 = PMKID   (field 3 is the PMKID; ANONCE/EAPOL/MESSAGEPAIR empty)
 *   02 = EAPOL   (field 3 is the M2 MIC; ANONCE/EAPOL/MESSAGEPAIR populated)
 * These were previously emitted with the two numbers swapped, so every .22000
 * was mislabelled: a 4-way MIC sat in the PMKID slot (01) and never cracked.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../modules/capture/handshake_capture.h"

namespace adversary {
namespace hc22000 {

/// Append helpers shared by the builders. Bounds-checked against @p outLen.
namespace detail {
inline void appendStr(char* out, size_t outLen, size_t& pos, const char* s) {
    while (*s && pos + 1 < outLen) out[pos++] = *s++;
}
inline void appendHex(char* out, size_t outLen, size_t& pos, const uint8_t* d, size_t n) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n && pos + 2 < outLen; ++i) {
        out[pos++] = H[d[i] >> 4];
        out[pos++] = H[d[i] & 0x0F];
    }
}
}  // namespace detail

/// Find the 802.1X/EAPOL payload inside a raw 802.11 frame by scanning for the
/// LLC/SNAP + EAPOL ethertype marker (AA AA 03 00 00 00 88 8E). Robust to QoS
/// headers / varying 802.11 header lengths. Returns the offset just past the
/// marker (start of the EAPOL frame) or -1 if not found.
inline int findEapolOffset(const uint8_t* frame, size_t len) {
    static const uint8_t marker[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
    if (!frame || len < 8) return -1;
    for (size_t i = 0; i + 8 <= len; ++i) {
        if (memcmp(frame + i, marker, 8) == 0) return static_cast<int>(i + 8);
    }
    return -1;
}

/// Build the EAPOL (4-way) line: WPA*02*MIC*MAC_AP*MAC_STA*ESSID*ANONCE*EAPOL*MESSAGEPAIR.
/// Uses M2's EAPOL frame (carries SNonce + MIC) with the MIC field zeroed, and the
/// ANonce from M1/M3. @return true if a complete line was written to @p out.
inline bool buildEapolLine(const CapturedHandshake& hs, char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    // Need M2 (MIC + SNonce) plus an ANonce source (M1 or M3).
    if (!(hs.hasMsg2 && (hs.hasMsg1 || hs.hasMsg3))) return false;

    int off = findEapolOffset(hs.msg2Data, hs.msg2Len);
    if (off < 0) return false;
    const uint8_t* eapol = hs.msg2Data + off;
    int avail = static_cast<int>(hs.msg2Len) - off;
    // EAPOL-Key MIC sits at offset 81 (4B EAPOL hdr + 77B up to the MIC); need
    // at least the MIC + the 2B key-data-length that follows it.
    if (avail < 99) return false;

    // Trust the EAPOL length field (bytes 2..3, big-endian) to trim any trailing
    // 802.11 FCS/padding; fall back to what's available if it looks wrong.
    int declared = 4 + ((eapol[2] << 8) | eapol[3]);
    int eapolLen = (declared >= 99 && declared <= avail) ? declared : avail;

    static uint8_t buf[256];
    if (eapolLen > static_cast<int>(sizeof(buf))) eapolLen = sizeof(buf);
    memcpy(buf, eapol, eapolLen);
    for (int i = 81; i < 81 + 16 && i < eapolLen; ++i) buf[i] = 0;  // zero the MIC

    size_t pos = 0;
    using detail::appendStr;
    using detail::appendHex;
    appendStr(out, outLen, pos, "WPA*02*");
    appendHex(out, outLen, pos, hs.mic, 16);                                      appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, hs.apBssid, 6);                                   appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, hs.clientMac, 6);                                 appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, reinterpret_cast<const uint8_t*>(hs.ssid), strlen(hs.ssid)); appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, hs.anonce, 32);                                   appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, buf, eapolLen);
    // messagepair 00: ANonce from M1, MIC/SNonce from M2 (the authenticated pair).
    appendStr(out, outLen, pos, "*00");

    if (pos + 1 >= outLen) return false;  // truncated: don't emit a partial line
    out[pos] = '\0';
    return true;
}

/// Build the PMKID line: WPA*01*PMKID*MAC_AP*MAC_STA*ESSID*** (empty ANONCE/EAPOL/
/// MESSAGEPAIR). @return true if a complete line was written to @p out.
inline bool buildPmkidLine(const CapturedPMKID& pmkid, char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    size_t pos = 0;
    using detail::appendStr;
    using detail::appendHex;
    appendStr(out, outLen, pos, "WPA*01*");
    appendHex(out, outLen, pos, pmkid.pmkid, 16);                                     appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, pmkid.bssid, 6);                                      appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, pmkid.staMac, 6);                                     appendStr(out, outLen, pos, "*");
    appendHex(out, outLen, pos, reinterpret_cast<const uint8_t*>(pmkid.ssid), strlen(pmkid.ssid));
    appendStr(out, outLen, pos, "***");

    if (pos + 1 >= outLen) return false;  // truncated: don't emit a partial line
    out[pos] = '\0';
    return true;
}

}  // namespace hc22000
}  // namespace adversary
