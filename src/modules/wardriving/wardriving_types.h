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
 * @brief Observation source / WiGLE "Type" column discriminator.
 *
 * A wardriving row is either an 802.11 AP or a Bluetooth-LE advertiser; both flow
 * through the same dedup/GPS/CSV pipeline (ADR-0060), differing only in the scan
 * source and three row fields (AuthMode, Channel, Type). Classic BT (WiGLE `BT`) is
 * not represented — NimBLE scans BLE only (slice-0061).
 */
enum class ObservationType : uint8_t {
    WIFI = 0,   ///< 802.11 access point   -> WiGLE Type column "WIFI"
    BLE  = 1,   ///< Bluetooth-LE advertiser -> WiGLE Type column "BLE"
};

/**
 * @brief Wardriving radio mode (persisted as WirelessSettings::wardrivingRadioMode).
 *
 * Which radios a session scans. WiFi and BLE run concurrently via ESP-IDF software
 * coexistence in BOTH mode (ADR-0060); there is no application-level time-slicing.
 * WIFI_ONLY is the default so the stock experience and full AP yield are unchanged
 * and the BLE controller's ~58.6 KB is never paid unless opted into.
 */
enum class RadioMode : uint8_t {
    WIFI_ONLY = 0,  ///< WiFi active scan only (default)
    BLE_ONLY  = 1,  ///< BLE scan only
    BOTH      = 2,  ///< WiFi + BLE concurrently (coexistence)
};

/// Number of selectable radio modes, so the cycle/clamp logic below never hard-codes
/// the enum cardinality.
constexpr uint8_t RADIO_MODE_COUNT = 3;

/**
 * @brief Advance to the next radio mode, wrapping after the last one.
 *
 * Drives the wardriving idle screen's per-run quick-config (ADR-0111), which cycles the
 * mode with a single key. A stored value outside [0, RADIO_MODE_COUNT) (corrupt settings)
 * resolves to WIFI_ONLY rather than propagating the bad value, so the selector always
 * recovers to a valid state instead of advancing from garbage.
 */
constexpr RadioMode nextRadioMode(RadioMode current) {
    const uint8_t raw = static_cast<uint8_t>(current);
    return raw >= RADIO_MODE_COUNT - 1 ? RadioMode::WIFI_ONLY
                                       : static_cast<RadioMode>(raw + 1);
}

/**
 * @brief Single network entry with GPS data
 */
struct NetworkEntry {
    char ssid[33];              ///< Network SSID / BLE device name (null-terminated)
    uint8_t bssid[6];           ///< MAC address / BLE BD_ADDR (deduplication key)
    int8_t rssi;                ///< Best signal strength seen (dBm)
    uint8_t channel;            ///< WiFi channel (1-14); 0 for BLE (WiGLE convention)
    uint8_t encryptionType;     ///< Security type (from WiFi.h constants); unused for BLE
    ObservationType type;       ///< Observation source (selects the WiGLE row shape)
    
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
        , type(ObservationType::WIFI)
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
