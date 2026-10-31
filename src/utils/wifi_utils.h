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

// Regulatory domain this tool operates under. The WiFi stack resets the country
// to a conservative default (channels 1-11) on every esp_wifi_init, which the
// Arduino layer triggers whenever a module cycles WiFi off and back on. Under
// that default, tuning to or starting a softAP on channels 12-14 fails
// (ESP_ERR_INVALID_ARG / "channel out of range"), silently breaking scan,
// capture and deauth against the ch12-13 APs that are standard under ETSI
// (Europe). This is a capability tool: it must reach every 2.4GHz channel, so we
// pin the widest manual domain (JP = 1-14) and RE-APPLY it after every WiFi
// bring-up, not just once. TX on 12-14 is then unconditional; that posture is
// documented in the README (the engagement authorization is the legal basis, not
// the country register, consistent with the deauth/evil-twin/karma frames this
// device already transmits).
constexpr char REGULATORY_COUNTRY_CODE[3] = {'J', 'P', '\0'};
constexpr uint8_t REGULATORY_FIRST_CHANNEL = 1;
constexpr uint8_t REGULATORY_CHANNEL_COUNT = 14;  // 1-14 inclusive

/**
 * @brief Pin the regulatory domain to all 14 2.4GHz channels (manual policy).
 *
 * Call once right after each `WiFi.mode(<non-OFF>)` on any scan/capture/attack
 * path that tunes to or transmits on an arbitrary target channel. Idempotent and
 * cheap enough for a per-bring-up call (NOT a per-channel-hop call).
 */
inline void applyFullChannelRegulatory() {
    wifi_country_t country = {};
    country.cc[0] = REGULATORY_COUNTRY_CODE[0];
    country.cc[1] = REGULATORY_COUNTRY_CODE[1];
    country.cc[2] = REGULATORY_COUNTRY_CODE[2];
    country.schan = REGULATORY_FIRST_CHANNEL;
    country.nchan = REGULATORY_CHANNEL_COUNT;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);
}

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

inline void applyFullChannelRegulatory() {
    // No-op: no radio on the host build.
}

// Note: enablePromiscuous and filter functions are not available in native builds
// because they require ESP32-specific types. Tests should mock these separately.

} // namespace wifi_utils
} // namespace adversary

#endif
