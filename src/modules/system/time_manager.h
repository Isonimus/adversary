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

/// Outcome of one non-blocking poll of an in-flight async NTP sync.
enum class NtpPoll : uint8_t {
    PENDING,  ///< SNTP not done yet and still within the timeout window
    LATCH,    ///< SNTP reports completed: adopt the NTP time
    TIMEOUT   ///< Deadline passed without completion: give up (lower tiers hold)
};

/**
 * @brief Pure latch-vs-timeout decision for a polled async NTP sync.
 *
 * Split out from the SNTP client (which is ESP-only) so the decoupled sync state
 * machine is unit-testable off-device (slice-0147). A completed sync latches even
 * past the deadline: a late success still beats the SD-cache tier.
 *
 * @param syncComplete  sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED
 * @param elapsedMs     millis() since the sync was started
 * @param timeoutMs     give-up window
 */
inline NtpPoll evaluateNtpPoll(bool syncComplete, uint32_t elapsedMs, uint32_t timeoutMs) {
    if (syncComplete) return NtpPoll::LATCH;
    if (elapsedMs >= timeoutMs) return NtpPoll::TIMEOUT;
    return NtpPoll::PENDING;
}

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
     * @brief Start an NTP sync without blocking (fire-and-forget).
     *
     * Kicks off the async SNTP client and returns immediately; update() latches
     * the result on a later main-loop tick. The WIFI_CONNECTED handler uses this
     * so event-bus dispatch is never stalled by the sync (slice-0147). The
     * blocking syncFromNTP() stays for callers that must have the clock set before
     * proceeding (TLS upload time gate, manual "Sync Now").
     *
     * @param server NTP server address
     * @param tzOffsetH Timezone offset in hours
     */
    void beginNtpSync(const char* server = "pool.ntp.org", int8_t tzOffsetH = 0);

    /**
     * @brief Main-loop tick: poll an in-flight async NTP sync (non-blocking).
     *
     * No-op unless a beginNtpSync() is pending. Latches a completed sync or
     * abandons it once the timeout elapses; never blocks. Must be called on the
     * same thread as every other TimeManager state write (the main loop).
     */
    void update();

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

    // --- Async (non-blocking) NTP sync, all touched only on the main loop ---
    static constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;  ///< give up after 10 s
    static constexpr uint32_t NTP_POLL_INTERVAL_MS = 100;   ///< blocking-path poll step

    void startSntp_(const char* server, int8_t tzOffsetH);  ///< configTime + arm pending
    void latchNtpSuccess_();                                 ///< adopt NTP time, persist, clear pending

    time_t syncedEpoch_;      ///< Epoch at moment of sync
    uint32_t syncMillis_;      ///< millis() at moment of sync
    TimeSource source_;        ///< Primary time source

    bool ntpPending_ = false;      ///< a beginNtpSync() is in flight
    uint32_t ntpStartMillis_ = 0;  ///< millis() when the current sync started
    time_t ntpPreTime_ = 0;        ///< clock before sync (for the delta log)
};

} // namespace adversary
