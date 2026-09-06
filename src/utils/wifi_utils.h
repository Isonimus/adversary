#pragma once

/**
 * @file wifi_utils.h
 * @brief WiFi utility functions for promiscuous mode and channel management
 * 
 * Consolidates common WiFi operations used across attack modules.
 */

#include <cstdint>

#ifdef ESP32
#include <esp_wifi.h>

namespace adversary {
namespace wifi_utils {

/**
 * @brief Enable promiscuous mode with callback
 * @param callback Packet receive callback
 * @param filter Optional packet filter (nullptr for no filter change)
 * @return true if successful
 */
inline bool enablePromiscuous(wifi_promiscuous_cb_t callback,
                               const wifi_promiscuous_filter_t* filter = nullptr) {
    // Disable first to be safe
    esp_wifi_set_promiscuous(false);
    
    // Set callback and filter BEFORE enabling to prevent race conditions
    esp_wifi_set_promiscuous_rx_cb(callback);
    if (filter) {
        esp_wifi_set_promiscuous_filter(filter);
    }
    
    // Now enable
    return esp_wifi_set_promiscuous(true) == ESP_OK;
}

/**
 * @brief Disable promiscuous mode and clear callback
 */
inline void disablePromiscuous() {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
}

/**
 * @brief Set WiFi channel
 * @param channel Channel number (1-14)
 * @return true if successful
 */
inline bool setChannel(uint8_t channel) {
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

/**
 * @brief Get current WiFi channel
 * @return Current channel number, or 0 on error
 */
inline uint8_t getChannel() {
    uint8_t primary;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        return primary;
    }
    return 0;
}

/**
 * @brief Create promiscuous filter for management frames only
 */
inline wifi_promiscuous_filter_t managementFrameFilter() {
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    return filter;
}

/**
 * @brief Create promiscuous filter for data frames only
 */
inline wifi_promiscuous_filter_t dataFrameFilter() {
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    return filter;
}

/**
 * @brief Create promiscuous filter for all frames
 */
inline wifi_promiscuous_filter_t allFrameFilter() {
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    return filter;
}

/**
 * @brief Create promiscuous filter for management + data frames
 */
inline wifi_promiscuous_filter_t mgmtAndDataFilter() {
    wifi_promiscuous_filter_t filter = {0};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    return filter;
}

} // namespace wifi_utils
} // namespace adversary

#else
// Native build - provide minimal stubs that don't require ESP32 types

namespace adversary {
namespace wifi_utils {

inline bool setChannel(uint8_t channel) {
    (void)channel;
    return false;
}

inline uint8_t getChannel() {
    return 0;
}

inline void disablePromiscuous() {
    // No-op
}

// Note: enablePromiscuous and filter functions are not available in native builds
// because they require ESP32-specific types. Tests should mock these separately.

} // namespace wifi_utils
} // namespace adversary

#endif
