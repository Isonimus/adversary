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
     */
    void startScan(uint32_t durationSeconds = 0);

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
