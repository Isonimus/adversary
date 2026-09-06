/**
 * @file wardriving_types.h
 * @brief Wardriving data structures
 * 
 * Data types for wardriving session management and network logging.
 */

#pragma once

#include <cstdint>
#include <cstring>

#if defined(ARDUINO) || defined(UNIT_TEST)
#include <Arduino.h>
#endif

namespace adversary {
namespace wardriving {

/**
 * @brief Single network entry with GPS data
 */
struct NetworkEntry {
    char ssid[33];              ///< Network SSID (null-terminated)
    uint8_t bssid[6];           ///< MAC address (deduplication key)
    int8_t rssi;                ///< Best signal strength seen (dBm)
    uint8_t channel;            ///< WiFi channel (1-14)
    uint8_t encryptionType;     ///< Security type (from WiFi.h constants)
    
    // GPS data (from first sighting with valid fix)
    double latitude;            ///< Decimal degrees (-90 to +90)
    double longitude;           ///< Decimal degrees (-180 to +180)
    float altitude;             ///< Meters above sea level
    uint8_t satellites;         ///< GPS satellites at capture time
    
    uint32_t firstSeenMs;       ///< millis() when first detected
    uint32_t lastSeenMs;        ///< millis() when last detected
    uint16_t seenCount;         ///< Number of times seen in scans
    bool hasGPS;                ///< GPS data is valid
    
    NetworkEntry() 
        : rssi(-100)
        , channel(0)
        , encryptionType(0)
        , latitude(0.0)
        , longitude(0.0)
        , altitude(0.0f)
        , satellites(0)
        , firstSeenMs(0)
        , lastSeenMs(0)
        , seenCount(0)
        , hasGPS(false)
    {
        memset(ssid, 0, sizeof(ssid));
        memset(bssid, 0, sizeof(bssid));
    }
};

/**
 * @brief Active wardriving session state
 */
struct WardrivingSession {
    char sessionId[20];         ///< Format: "WD_YYYYMMDD_HHMMSS"
    uint32_t startTimeMs;       ///< millis() at session start
    uint32_t endTimeMs;         ///< millis() at session end (0 if active)
    uint32_t networksFound;     ///< Total unique networks
    uint32_t networksWithGPS;   ///< Networks tagged with GPS
    float distanceTraveledKm;   ///< Calculated from GPS deltas
    
    WardrivingSession() 
        : startTimeMs(0)
        , endTimeMs(0)
        , networksFound(0)
        , networksWithGPS(0)
        , distanceTraveledKm(0.0f)
    {
        memset(sessionId, 0, sizeof(sessionId));
    }
    
    /**
     * @brief Get session duration in seconds
     * @return Duration (0 if not started, or active duration if running)
     */
    uint32_t getDurationSeconds() const {
        if (startTimeMs == 0) return 0;
        uint32_t endMs = (endTimeMs == 0) ? millis() : endTimeMs;
        return (endMs - startTimeMs) / 1000;
    }
    
    /**
     * @brief Check if session is active
     */
    bool isActive() const {
        return startTimeMs > 0 && endTimeMs == 0;
    }
};

} // namespace wardriving
} // namespace adversary
