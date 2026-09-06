/**
 * @file raw_wifi.cpp
 * @brief Low-level WiFi frame transmission implementation
 */

#include "raw_wifi.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

bool RawWiFi::s_debugLogging = false;
uint32_t RawWiFi::s_totalTransmitted = 0;
uint32_t RawWiFi::s_totalErrors = 0;

TxResult RawWiFi::transmit(WiFiInterface iface, const uint8_t* data, size_t length, bool waitForAck) {
    TxResult result = {false, 0};
    
#ifdef ESP32
    if (!data || length == 0) {
        result.errorCode = ESP_ERR_INVALID_ARG;
        return result;
    }
    
    wifi_interface_t wifiIface = (iface == WiFiInterface::AP) ? WIFI_IF_AP : WIFI_IF_STA;
    
    esp_err_t err = esp_wifi_80211_tx(wifiIface, data, length, waitForAck);
    
    result.success = (err == ESP_OK);
    result.errorCode = err;
    
    if (result.success) {
        s_totalTransmitted++;
    } else {
        s_totalErrors++;
        if (s_debugLogging) {
            const char* ifaceName = (iface == WiFiInterface::AP) ? "AP" : "STA";
            Serial.printf("[RawWiFi] TX failed on %s: err=%d len=%zu\n", 
                         ifaceName, err, length);
        }
    }
    
    if (s_debugLogging && result.success) {
        const char* ifaceName = (iface == WiFiInterface::AP) ? "AP" : "STA";
        Serial.printf("[RawWiFi] TX %s: %zu bytes\n", ifaceName, length);
    }
#else
    (void)iface;
    (void)data;
    (void)length;
    (void)waitForAck;
    result.success = true;
    result.errorCode = 0;
#endif
    
    return result;
}

TxResult RawWiFi::transmitBoth(const uint8_t* data, size_t length, bool waitForAck) {
    // Send on AP first (usually the primary interface for attacks)
    TxResult result = transmit(WiFiInterface::AP, data, length, waitForAck);
    
    // Also send on STA (best-effort, ignore result)
    transmit(WiFiInterface::STATION, data, length, waitForAck);
    
    return result;
}

void RawWiFi::setDebugLogging(bool enabled) {
    s_debugLogging = enabled;
#ifdef ESP32
    if (enabled) {
        Serial.println("[RawWiFi] Debug logging enabled");
    }
#endif
}

void RawWiFi::resetStats() {
    s_totalTransmitted = 0;
    s_totalErrors = 0;
}

} // namespace adversary
