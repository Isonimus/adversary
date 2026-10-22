/**
 * @file ble_scanner.h
 * @brief Singleton for BLE device scanning using NimBLE
 */

#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <mutex>

namespace adversary {

/**
 * @struct BLEDeviceInfo
 * @brief Simplified structure to hold discovered BLE device data
 */
struct BLEDeviceInfo {
    NimBLEAddress address;
    std::string name;
    int rssi;
    bool isFastPair;
    uint16_t manufacturerId;
    std::string serviceData;
    uint32_t lastSeen;
};

// Placeholder stored in BLEDeviceInfo::name when the advertiser broadcasts no name (NimBLE
// haveName() is false). It is a display stand-in, not a real name, so most advertisers carry
// it; a consumer that wants to show the address instead must test hasAdvertisedName() rather
// than name.empty(), which never fires while this stand-in is in force.
constexpr const char* BLE_NAME_UNKNOWN = "Unknown";

// True when the device advertised a usable name (not absent, not the BLE_NAME_UNKNOWN stand-in).
inline bool hasAdvertisedName(const BLEDeviceInfo& device) {
    return !device.name.empty() && device.name != BLE_NAME_UNKNOWN;
}

class BLEScanner : public NimBLEAdvertisedDeviceCallbacks {
public:
    static BLEScanner& getInstance() {
        static BLEScanner instance;
        return instance;
    }

    /**
     * @brief Initialize NimBLE and local state
     */
    void init();

    /**
     * @brief Fully deinitialize NimBLE and release all memory
     */
    void deinit();

    /**
     * @brief Check if NimBLE is currently initialized
     */
    static bool isInitialized();

    /**
     * @brief Start an asynchronous scan
     * @param durationSeconds 0 for continuous (manual stop)
     * @param activeScan true sends SCAN_REQ to collect scan-response data (names,
     *        services); false is passive (listen only). Passive is required for a
     *        long *continuous* scan, because active + continuous + legacy ADV_IND makes
     *        NimBLE retain a NimBLEAdvertisedDevice per unique address while it waits for
     *        a scan response, and the maxResults=0 erase only runs after the callback
     *        fires (NimBLEScan.cpp:143-158); DISC_COMPLETE, which would flush them, never
     *        fires in continuous mode. Under BLE MAC randomization that vector grows
     *        unbounded and exhausts the heap (ADR-0103 phase 3). Passive scan reports each
     *        advertisement immediately, so the device is erased at once and memory stays
     *        flat, at the cost of scan-response-only fields.
     */
    void startScan(uint32_t durationSeconds = 0, bool activeScan = true);

    /**
     * @brief Stop current scan
     */
    void stopScan();

    /**
     * @brief Check if scanning is active
     */
    bool isScanning() const { return m_scanning; }

    /**
     * @brief Get a copy of discovered devices
     */
    std::vector<BLEDeviceInfo> getDevices();

    /**
     * @brief Clear results list
     */
    void clearDevices();

    /**
     * @brief Get count of discovered devices
     */
    size_t getDeviceCount();

    // NimBLE callback implementation
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override;

private:
    BLEScanner();
    ~BLEScanner() = default;
    BLEScanner(const BLEScanner&) = delete;
    BLEScanner& operator=(const BLEScanner&) = delete;

    std::vector<BLEDeviceInfo> m_devices;
    mutable std::mutex m_mutex;
    bool m_scanning;
    uint32_t m_scanStartTime;
    
    static const size_t MAX_DEVICES = 50;
};

} // namespace adversary
