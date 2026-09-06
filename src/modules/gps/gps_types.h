#pragma once

/**
 * @file gps_types.h
 * @brief GPS data type definitions
 * 
 * Data structures for GPS coordinates, velocity, and parsed data.
 */

#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../../../test/common/arduino_mocks.h"
#endif

namespace adversary {
namespace gps {

/**
 * @brief GPS coordinate information
 */
struct GPSCoordinate {
    double latitude;          ///< Latitude in decimal degrees (-90 to +90)
    double longitude;         ///< Longitude in decimal degrees (-180 to +180)
    float altitude;           ///< Altitude in meters above sea level
    uint8_t satellites;       ///< Number of satellites in use
    uint8_t fixQuality;       ///< Fix quality: 0=invalid, 1=GPS fix, 2=DGPS fix
    float hdop;               ///< Horizontal dilution of precision
    uint32_t timestamp;       ///< Unix timestamp (from RTC or GPS time)
    uint8_t hour, minute, second; ///< UTC time
    uint8_t day, month;       ///< UTC date
    uint16_t year;            ///< UTC year
    bool valid;               ///< True if fix is valid
    
    GPSCoordinate()
        : latitude(0.0)
        , longitude(0.0)
        , altitude(0.0f)
        , satellites(0)
        , fixQuality(0)
        , hdop(99.9f)
        , timestamp(0)
        , hour(0), minute(0), second(0)
        , day(0), month(0), year(0)
        , valid(false) {}
};

/**
 * @brief GPS velocity information
 */
struct GPSVelocity {
    float speedKmh;           ///< Speed in kilometers per hour
    float courseTrue;         ///< True course in degrees (0-360)
    bool valid;               ///< True if velocity data is valid
    
    GPSVelocity()
        : speedKmh(0.0f)
        , courseTrue(0.0f)
        , valid(false) {}
};

/**
 * @brief Complete GPS data
 */
struct GPSData {
    GPSCoordinate coordinate; ///< Position information
    GPSVelocity velocity;     ///< Movement information
    uint32_t lastUpdateMs;    ///< millis() timestamp of last update
    
    GPSData()
        : lastUpdateMs(0) {}
    
    /**
     * @brief Check if data is recent and valid
     * @param maxAgeMs Maximum age in milliseconds
     * @return true if data is valid and recent
     */
    bool isValid(uint32_t maxAgeMs = 5000) const {
        if (!coordinate.valid) return false;
        uint32_t age = millis() - lastUpdateMs;
        return age < maxAgeMs;
    }
};

} // namespace gps
} // namespace adversary
