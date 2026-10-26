/**
 * @file wifi_frame.h
 * @brief Pure, host-testable 802.11 frame parsing for the WiFi-AP tracker (ADR-0130).
 *
 * The WiFi-AP proximity tracker locks to an AP's channel and reads the RSSI of the AP's own
 * transmissions (beacons, probe responses) out of promiscuous frames. Deciding whether a raw
 * frame is a management frame sent by the target BSSID is pure byte parsing with no radio in
 * it, so it lives here and is tested natively; the device module is then a thin wrapper.
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace adversary {

/// Shortest header we must see to read Address 2: frame control (2) + duration (2) +
/// Address 1 (6) + Address 2 (6) = 16 bytes.
constexpr uint16_t WIFI_ADDR2_HEADER_MIN_LEN = 16;

/// 802.11 frame-control Type field value for management frames (beacons, probe responses).
constexpr uint8_t WIFI_FRAME_TYPE_MGMT = 0;

/// Byte offset of Address 2 (the transmitter address) in an 802.11 MAC header.
constexpr uint16_t WIFI_ADDR2_OFFSET = 10;

/**
 * @brief True if `frame` is a management frame whose transmitter (Address 2) is `bssid`.
 *
 * For a beacon or probe response the transmitter is the AP itself, so a match means the
 * frame is the target AP's own signal. Rejects null buffers, frames too short to hold
 * Address 2, and non-management frames (data/control).
 *
 * @param frame Raw 802.11 frame starting at the MAC header (frame-control byte first).
 * @param len   Length of `frame` in bytes.
 * @param bssid Six-byte target BSSID to match against Address 2.
 */
inline bool managementFrameFromBssid(const uint8_t* frame, uint16_t len, const uint8_t bssid[6]) {
    if (!frame || !bssid) return false;
    if (len < WIFI_ADDR2_HEADER_MIN_LEN) return false;

    const uint16_t frameControl = static_cast<uint16_t>(frame[0]) |
                                   (static_cast<uint16_t>(frame[1]) << 8);
    const uint8_t type = (frameControl >> 2) & 0x03;
    if (type != WIFI_FRAME_TYPE_MGMT) return false;

    return memcmp(frame + WIFI_ADDR2_OFFSET, bssid, 6) == 0;
}

} // namespace adversary
