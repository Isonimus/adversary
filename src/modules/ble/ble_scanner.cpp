/**
 * @file ble_scanner.cpp
 * @brief BLEScanner implementation
 */

#include "ble_scanner.h"
#include "ble_utils.h"
#include "../system/system_manager.h"

namespace adversary {

BLEScanner::BLEScanner() 
    : m_scanning(false)
    , m_scanStartTime(0) {
}

// static bool s_bleInitialized = false; // Removed: Use NimBLEDevice::getInitialized()

void BLEScanner::init() {
    if (NimBLEDevice::getInitialized()) return;
    
    Serial.printf("[BLE] Starting NimBLE init (Heap: %u)\n", (unsigned int)ESP.getFreeHeap());
    NimBLEDevice::init("ADV-BLE");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // CRITICAL: Explicitly set own address type to PUBLIC.
    // This prevents rc=530 (HCI 0x12) errors when switching from BLE Spam (which uses RANDOM)
    // because NimBLEDevice::m_ownAddrType persists across soft-deinit/re-init cycles.
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    
    Serial.printf("[BLE] NimBLE initialized (Heap: %u)\n", (unsigned int)ESP.getFreeHeap());
}

bool BLEScanner::isInitialized() {
    return NimBLEDevice::getInitialized();
}

void BLEScanner::deinit() {
    // HARD STOP without deletion: Shutdown radio but keep C++ objects
    // This releases the radio for WiFi without the "clearAll" crash

    if (!NimBLEDevice::getInitialized()) {
        // Never brought up this session — there is nothing to release and no
        // retained controller memory. Returning quietly avoids the misleading
        // "Objects retained" log that made BLE look like it was holding heap
        // during the WPA-SEC/TLS window (it wasn't).
        return;
    }

    stopScan();
    delay(50); // Transition time
    clearDevices();

    // Clear internal NimBLE results to free memory before shutdown
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
        pScan->clearResults();
    }

    Serial.println("[BLE] Releasing controller radio (deinit false)...");
    try {
        NimBLEDevice::deinit(false);
    } catch (...) {
        Serial.println("[BLE] Exception during radio deinit");
    }

    Serial.println("[BLE] Scanner shut down (radio released, host objects retained)");
}

void BLEScanner::startScan(uint32_t durationSeconds) {
    if (m_scanning) return;

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(this);
    pScan->setActiveScan(true); // Active scan for more info (name, services)
    pScan->setInterval(100);
    pScan->setWindow(99); 

    // Start asynchronous scan
    if (pScan->start(durationSeconds, nullptr, false)) {
        m_scanning = true;
        m_scanStartTime = millis();
        Serial.printf("[BLE] Scan started for %u seconds\n", (unsigned int)durationSeconds);
    } else {
        Serial.println("[BLE] Failed to start scan");
    }
}

void BLEScanner::stopScan() {
    if (!m_scanning) return;
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
        pScan->stop();
    }
    m_scanning = false;
    Serial.println("[BLE] Scan stopped");
}

std::vector<BLEDeviceInfo> BLEScanner::getDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices;
}

void BLEScanner::clearDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool hadDevices = !m_devices.empty();
    m_devices.clear();
    m_devices.shrink_to_fit(); // CRITICAL: Release memory capacity back to heap
    if (hadDevices) {
        Serial.println("[BLE] Results cleared (RAM freed)");
    }
}

size_t BLEScanner::getDeviceCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices.size();
}

void BLEScanner::onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    if (!advertisedDevice) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if device already in list
    bool found = false;
    for (auto& dev : m_devices) {
        if (dev.address == advertisedDevice->getAddress()) {
            // Update existing entry
            dev.rssi = advertisedDevice->getRSSI();
            dev.lastSeen = millis();
            if (advertisedDevice->haveName()) {
                dev.name = advertisedDevice->getName();
            }
            
            // Re-check Fast Pair status (may arrive in scan response)
            dev.isFastPair = ble::isFastPair(advertisedDevice);
            
            if (advertisedDevice->haveManufacturerData()) {
                std::string mData = advertisedDevice->getManufacturerData();
                if (mData.length() >= 2) {
                    dev.manufacturerId = (uint8_t)mData[0] | ((uint8_t)mData[1] << 8);
                }
            }
            
            found = true;
            break;
        }
    }

    if (!found) {
        // Add new device
        BLEDeviceInfo info;
        info.address = advertisedDevice->getAddress();
        info.name = advertisedDevice->haveName() ? advertisedDevice->getName() : "Unknown";
        info.rssi = advertisedDevice->getRSSI();
        info.lastSeen = millis();
        info.isFastPair = ble::isFastPair(advertisedDevice);
        
        info.manufacturerId = 0xFFFF;
        if (advertisedDevice->haveManufacturerData()) {
            std::string mData = advertisedDevice->getManufacturerData();
            if (mData.length() >= 2) {
                info.manufacturerId = (uint8_t)mData[0] | ((uint8_t)mData[1] << 8);
            }
        }

        // Limit list size
        if (m_devices.size() < MAX_DEVICES) {
            m_devices.push_back(info);
        } else {
            // Replace oldest if full
            size_t oldestIdx = 0;
            uint32_t oldestTime = m_devices[0].lastSeen;
            for (size_t i = 1; i < m_devices.size(); ++i) {
                if (m_devices[i].lastSeen < oldestTime) {
                    oldestTime = m_devices[i].lastSeen;
                    oldestIdx = i;
                }
            }
            m_devices[oldestIdx] = info;
        }
    }
}

} // namespace adversary
