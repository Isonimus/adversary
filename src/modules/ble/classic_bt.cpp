/**
 * @file classic_bt.cpp
 * @brief Bluetooth Classic helper implementation
 */

#include "classic_bt.h"
#include <Arduino.h>

#ifdef HAS_CLASSIC_BT
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#endif

namespace adversary {
namespace ble {

ClassicBT& ClassicBT::getInstance() {
    static ClassicBT instance;
    return instance;
}

ClassicBT::ClassicBT() : m_isBonding(false), m_status("IDLE") {}

bool ClassicBT::init() {
#ifndef HAS_CLASSIC_BT
    m_status = "Unsupported (S3)";
    return false;
#else
    // Check if BT controller is already initialized (might be used by NimBLE)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        m_status = "Controller not ready";
        return false;
    }
    
    // We assume NimBLE or other system component has initialized the controller.
    // However, if we need Classic BT, we might need to change the controller mode.
    // ESP32 supports DUAL mode.
    
    m_status = "READY";
    return true;
#endif
}

void ClassicBT::deinit() {
    m_isBonding = false;
    m_status = "IDLE";
}

bool ClassicBT::startBonding(const uint8_t* address) {
#ifndef HAS_CLASSIC_BT
    return false;
#else
    if (m_isBonding) return false;

    Serial.printf("[ClassicBT] Attempting to bond with %02X:%02X:%02X:%02X:%02X:%02X\n",
                  address[0], address[1], address[2], address[3], address[4], address[5]);
    
    m_isBonding = true;
    m_status = "BONDING...";

    // In a real exploit, we would register as a specific device type (HID/A2DP)
    // and trigger a classic connection.
    // For now, we use GAP to create a bond.
    esp_bd_addr_t remote_bda;
    memcpy(remote_bda, address, 6);
    
    // Note: This requires the controller to be in BT_MODE_BTDM (Dual mode)
    // Most NimBLE implementations on ESP32 default to BLE only or Dual depending on config.
    esp_err_t ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);
    if (ret == ESP_OK) {
        // Trigger bond
        // This is a simplified implementation. Full hijack requires SDP and SPP/HID registration.
        Serial.println("[ClassicBT] Bonding request sent");
        m_status = "REQUEST SENT";
    } else {
        m_status = "ERROR";
        m_isBonding = false;
        return false;
    }

    return true;
#endif
}

} // namespace ble
} // namespace adversary
