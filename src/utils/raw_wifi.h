/**
 * @file raw_wifi.h
 * @brief Low-level WiFi frame transmission utility
 * 
 * Centralizes esp_wifi_80211_tx() calls for better debugging,
 * error handling, and potential future enhancements like
 * transmission logging or rate limiting.
 */

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef ESP32
#include <esp_wifi.h>
#endif

namespace adversary {

/**
 * @brief WiFi interface selection for raw transmission
 */
enum class WiFiInterface : uint8_t {
    STATION = 0,  // WIFI_IF_STA
    AP = 1        // WIFI_IF_AP
};

/**
 * @brief Result of raw frame transmission
 */
struct TxResult {
    bool success;
    int errorCode;  // ESP_OK or ESP error code
    
    operator bool() const { return success; }
};

/**
 * @brief Utility class for raw 802.11 frame transmission
 * 
 * Wraps esp_wifi_80211_tx() with:
 * - Unified error handling
 * - Optional debug logging
 * - Statistics tracking
 */
class RawWiFi {
public:
    /**
     * @brief Transmit a raw 802.11 frame
     * 
     * @param iface WiFi interface to use (STATION or AP)
     * @param data Frame data buffer
     * @param length Frame length in bytes
     * @param waitForAck If true, wait for transmission completion
     * @return TxResult with success status and error code
     */
    static TxResult transmit(WiFiInterface iface, const uint8_t* data, size_t length, bool waitForAck = false);
    
    /**
     * @brief Transmit on both interfaces (STA and AP)
     * 
     * Useful for deauth attacks where frame should be sent from both interfaces.
     * 
     * @param data Frame data buffer
     * @param length Frame length in bytes
     * @param waitForAck If true, wait for transmission completion
     * @return TxResult from first interface (AP), second is best-effort
     */
    static TxResult transmitBoth(const uint8_t* data, size_t length, bool waitForAck = false);
    
    /**
     * @brief Enable/disable debug logging of transmitted frames
     */
    static void setDebugLogging(bool enabled);
    
    /**
     * @brief Get transmission statistics
     */
    static uint32_t getTotalTransmitted() { return s_totalTransmitted; }
    static uint32_t getTotalErrors() { return s_totalErrors; }
    static void resetStats();
    
private:
    static bool s_debugLogging;
    static uint32_t s_totalTransmitted;
    static uint32_t s_totalErrors;
};

} // namespace adversary
