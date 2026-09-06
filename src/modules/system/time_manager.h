#pragma once

/**
 * @file time_manager.h
 * @brief Tiered time management for devices without RTC
 */

#include <stdint.h>
#include <time.h>
#include <string>

namespace adversary {

/**
 * @brief Available sources for time synchronization
 */
enum class TimeSource : uint8_t {
    NONE,       ///< No sync yet (epoch 0)
    SD_CACHE,   ///< Loaded from last saved time on SD
    GPS,        ///< Synced from GPS NMEA
    NTP         ///< Synced from NTP server
};

/**
 * @brief Singleton for managing system time and synchronization
 * 
 * Manages a tiered hierarchy of time sources (NTP > GPS > SD > Boot).
 * Updates POSIX system clock via settimeofday() and configTime().
 */
class TimeManager {
public:
    /**
     * @brief Get singleton instance
     */
    static TimeManager& getInstance();

    /**
     * @brief Attempt NTP synchronization
     * 
     * Requires active WiFi connection. Sets POSIX system clock.
     * 
     * @param server NTP server address
     * @param tzOffsetH Timezone offset in hours
     * @return true if sync successful
     */
    bool syncFromNTP(const char* server = "pool.ntp.org", int8_t tzOffsetH = 0);

    /**
     * @brief Synchronize from GPS date/time
     * 
     * @param hour UTC hour (0-23)
     * @param min UTC minute (0-59)
     * @param sec UTC second (0-59)
     * @param day UTC day (1-31)
     * @param month UTC month (1-12)
     * @param year UTC year (e.g., 2026)
     * @return true if sync accepted (not overridden by higher source)
     */
    bool syncFromGPS(uint8_t hour, uint8_t min, uint8_t sec,
                     uint8_t day, uint8_t month, uint16_t year);

    /**
     * @brief Load last known time from SD card
     * 
     * @return true if valid time was loaded
     */
    bool loadFromSD();

    /**
     * @brief Save current time state to SD card
     * 
     * @return true if successfully saved
     */
    bool saveToSD();

    /**
     * @brief Get current best-effort Unix timestamp
     * 
     * Uses synced base + millis() offset.
     * @return epoch timestamp (seconds)
     */
    time_t now() const;

    /**
     * @brief Check if time has been synchronized
     * 
     * @return true if source is GPS, NTP, or valid SD_CACHE
     */
    bool isSynced() const { return source_ != TimeSource::NONE; }

    /**
     * @brief Get current active time source
     * 
     * @return TimeSource
     */
    TimeSource getSource() const { return source_; }

    /**
     * @brief Get human-readable source name
     * 
     * @return const char* (e.g., "NTP", "GPS", "SD", "None")
     */
    const char* getSourceName() const;

    /**
     * @brief Get seconds elapsed since last successful sync
     * 
     * @return Seconds since sync
     */
    uint32_t getSecondsSinceSync() const;

    /**
     * @brief Reset state (for unit tests only)
     */
    void resetForTest();

private:
    TimeManager();
    ~TimeManager() = default;

    // Prevent copying
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    time_t syncedEpoch_;      ///< Epoch at moment of sync
    uint32_t syncMillis_;      ///< millis() at moment of sync
    TimeSource source_;        ///< Primary time source
};

} // namespace adversary
