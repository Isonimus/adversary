/**
 * @file ble_utils.h
 * @brief Common BLE utility functions
 */

#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string>

namespace adversary {
namespace ble {

/**
 * @brief Convert NimBLEAddress to a human-readable MAC string (XX:XX:XX:XX:XX:XX)
 */
std::string addressToString(const NimBLEAddress& address);

/**
 * @brief Get manufacturer name from ID
 */
const char* getManufacturerName(uint16_t manufacturerId);

/**
 * @brief Check if a scan result contains the Google Fast Pair service (0xFE2C)
 */
bool isFastPair(NimBLEAdvertisedDevice* device);

} // namespace ble
} // namespace adversary
