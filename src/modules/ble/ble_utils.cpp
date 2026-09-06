/**
 * @file ble_utils.cpp
 * @brief BLE utilities implementation
 */

#include "ble_utils.h"
#include <cstdio>

namespace adversary {
namespace ble {

std::string addressToString(const NimBLEAddress& address) {
    char buf[18];
    uint8_t* val = (uint8_t*)address.getNative();
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", 
             val[5], val[4], val[3], val[2], val[1], val[0]);
    return std::string(buf);
}

const char* getManufacturerName(uint16_t manufacturerId) {
    switch (manufacturerId) {
        case 0x0006: return "Microsoft";
        case 0x004C: return "Apple";
        case 0x00D2: return "Samsung";
        case 0x00E0: return "Google";
        case 0x0043: return "Sony";
        case 0x0059: return "Nordic";
        case 0x000F: return "Broadcom";
        case 0x038F: return "Bose";
        case 0x000A: return "Qualcomm";
        case 0x0057: return "Intel";
        default: return "Unknown";
    }
}

bool isFastPair(NimBLEAdvertisedDevice* device) {
    // Google Fast Pair Service UUID (16-bit)
    const uint16_t FAST_PAIR_SERVICE_DATA_UUID = 0xFE2C;

    // Check Service UUIDs
    if (device->haveServiceUUID()) {
        if (device->isAdvertisingService(NimBLEUUID(FAST_PAIR_SERVICE_DATA_UUID))) {
            return true;
        }
    }

    // Check Service Data
    if (device->haveServiceData()) {
        // NimBLE allows checking Service Data by UUID
        if (device->getServiceDataCount() > 0) {
            // Iterate through service data if needed, but isAdvertisingService often covers it
            // if it was parsed as such. Let's do a more direct check for 0xFE2C.
            for (int i = 0; i < device->getServiceDataCount(); i++) {
                if (device->getServiceDataUUID(i) == NimBLEUUID(FAST_PAIR_SERVICE_DATA_UUID)) {
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace ble
} // namespace adversary
