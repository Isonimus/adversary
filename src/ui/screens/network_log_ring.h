/**
 * @file network_log_ring.h
 * @brief Fixed-capacity live log for the wardriving Network Log view.
 *
 * Display-free so it is unit-testable natively; the WardrivingScreen that renders it is
 * device-only (NimBLE, esp_heap_caps, Canvas). This mirrors the formatWigleRow split: the
 * pure policy lives here, the hardware glue stays in the screen.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "modules/wardriving/wardriving_types.h"   // ObservationType

namespace adversary {

/**
 * @brief One row in the live network-log view: a WiFi AP or a BLE advertiser.
 */
struct LoggedNetwork {
    char ssid[33];                      ///< SSID (WiFi) or device name (BLE); "" for an unnamed advertiser
    uint8_t bssid[6];                   ///< AP BSSID / BLE BD_ADDR; part of the dedup key so radios never share a row
    int8_t rssi;
    uint8_t channel;                    ///< WiFi channel; 0 for BLE (WiGLE convention)
    uint8_t security;                   ///< WiFi security enum; unused for a BLE row
    wardriving::ObservationType type;   ///< selects the row shape (WiFi channel/security vs "BLE" tag) and quota bucket
};

/**
 * @brief Capacity-bounded live log shared by WiFi and BLE observations, with a per-type
 *        minimum so one radio cannot evict the other out of the view.
 *
 * BOTH mode sees far more BLE advertisers than WiFi APs: BLE MAC randomization alone feeds a
 * steady stream of new BD_ADDRs. In a single FIFO ring that stream fills every slot and evicts
 * all WiFi rows within seconds, so the live log reads as BLE-only though the CSV records both
 * (the reported bug). This ring instead reserves a minimum per type: once a type holds at least
 * its reservation, a row of the other type never evicts it. The CAPACITY - (reserves) spare
 * slots still float to whichever radio is busier, so a WiFi-heavy environment is not forced to
 * waste slots on absent BLE. Dedup is by (type, bssid) across the whole buffer, so each distinct
 * radio shows once (slice-0075), unchanged from the previous ring.
 */
class NetworkLogRing {
public:
    static constexpr size_t CAPACITY = 25;
    static constexpr size_t WIFI_RESERVE = 10;  ///< WiFi rows this ring will not evict for BLE
    static constexpr size_t BLE_RESERVE = 10;   ///< and vice versa; the rest float to the busier radio
    static_assert(WIFI_RESERVE + BLE_RESERVE <= CAPACITY, "per-type reserves cannot exceed capacity");
    // Each reserve must be >= 1: the eviction rule picks the other type as victim only while
    // this type is below its reserve, so a zero reserve would let a full buffer choose an empty
    // victim and write past the end. Both radios also genuinely want a guaranteed minimum.
    static_assert(WIFI_RESERVE >= 1 && BLE_RESERVE >= 1, "each per-type reserve must be at least 1");

    /**
     * @brief Append one observation (newest-last). A (type, bssid) already present is ignored:
     *        the existing row is not refreshed, matching the view's "each distinct radio once"
     *        contract. @p name may be null (unnamed advertiser), stored as "".
     */
    void append(const char* name, const uint8_t* bssid, int8_t rssi,
                uint8_t channel, uint8_t security, wardriving::ObservationType type);

    size_t count() const { return count_; }

    /**
     * @brief Row for display index [0, count()), newest first. Index must be in range.
     */
    const LoggedNetwork& newestFirst(size_t displayIndex) const {
        return entries_[count_ - 1 - displayIndex];
    }

    void clear() { count_ = 0; }

private:
    size_t typeCount(wardriving::ObservationType type) const;
    void eraseOldestOfType(wardriving::ObservationType type);

    LoggedNetwork entries_[CAPACITY];
    size_t count_ = 0;
};

} // namespace adversary
