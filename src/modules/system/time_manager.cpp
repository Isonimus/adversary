/**
 * @file time_manager.cpp
 * @brief Tiered time management implementation
 */

#include "time_manager.h"
#include "hal/storage/sd_manager.h"

#ifdef ESP32
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <sys/time.h>
#include <lwip/apps/sntp.h>
#include "core/event_bus.h"
#include "core/event_data.h"
#include "core/event_types.h"
#else
#include "../../../test/common/arduino_mocks.h"
#include <sys/time.h>
#include <ctime>
#include <ArduinoJson.h>
#include <SD.h>
#include <core/event_bus.h>
#include <core/event_types.h>
#include <core/event_data.h>
#endif

namespace adversary {

TimeManager& TimeManager::getInstance() {
    static TimeManager instance;
    return instance;
}

TimeManager::TimeManager()
    : syncedEpoch_(0)
    , syncMillis_(0)
    , source_(TimeSource::NONE) {
    
    // Auto-sync from NTP when WiFi connects
    EventBus::getInstance().subscribe(EventType::WIFI_CONNECTED, [this](const EventData& e) {
        (void)e;
        this->syncFromNTP();
    });
}

bool TimeManager::syncFromNTP(const char* server, int8_t tzOffsetH) {
    Serial.printf("[TIME] Attempting NTP sync with %s (TZ: %d)\n", server, tzOffsetH);

#ifdef ESP32
    // Record pre-NTP time to detect actual change
    time_t preNtpTime = time(nullptr);
    
    // Set timezone and server - this starts SNTP async. Pass two redundant
    // fallbacks so a single unreachable server doesn't fail the sync (the
    // time-domain twin of the DNS retry on the upload path).
    configTime(tzOffsetH * 3600, 0, server, "time.google.com", "time.cloudflare.com");

    // Wait for ACTUAL NTP sync by detecting time change.
    // If SD cache set the clock, time(nullptr) is already > 1B but stale.
    // We need to wait until time(nullptr) actually jumps from NTP response.
    int retries = 100;  // 10 seconds max
    bool synced = false;
    
    while (retries-- > 0) {
        delay(100);
        
        time_t currentTime = time(nullptr);
        
        // NTP synced if time jumped significantly from pre-NTP value
        // (more than the ~10 seconds we've been waiting)
        long delta = (long)(currentTime - preNtpTime);
        if (delta > 30 || delta < -30) {
            synced = true;
            break;
        }
        
        // Also handle the case where SD cache was never loaded (time was < 1B)
        if (preNtpTime < 1000000000 && currentTime > 1000000000) {
            synced = true;
            break;
        }
    }

    if (synced) {
        syncedEpoch_ = time(nullptr);
        syncMillis_ = millis();
        source_ = TimeSource::NTP;
        Serial.printf("[TIME] NTP Sync SUCCESS: %ld (was %ld, delta=%lds)\n", 
                      (long)syncedEpoch_, (long)preNtpTime, 
                      (long)(syncedEpoch_ - preNtpTime));
        saveToSD();
        return true;
    }
#endif

    Serial.println("[TIME] NTP Sync FAILED");
    return false;
}

bool TimeManager::syncFromGPS(uint8_t hour, uint8_t min, uint8_t sec,
                             uint8_t day, uint8_t month, uint16_t year) {
    // NTP is higher priority and more accurate (usually) than GPS NMEA pulse-less arrival
    if (source_ == TimeSource::NTP) {
        return false;
    }

    // Throttle GPS re-sync. A valid fix delivers RMC ~1Hz, but this used to
    // re-sync (and saveToSD() + churn the C environment via setenv) on EVERY
    // sentence — thousands of SD writes + heap allocations per hour with a fix,
    // which slowly bled the heap overnight. Once synced from GPS the clock tracks
    // via millis(); a periodic correction is plenty.
    static constexpr uint32_t GPS_RESYNC_INTERVAL_MS = 10UL * 60UL * 1000UL;  // 10 min
    if (source_ == TimeSource::GPS && (millis() - syncMillis_) < GPS_RESYNC_INTERVAL_MS) {
        return false;
    }

    // Calculate UTC epoch WITHOUT using mktime (which applies local timezone)
    // mktime() interprets struct tm as local time, but GPS gives pure UTC
    // Using timegm() equivalent to avoid timezone corruption
    struct tm gpsTime = {0};
    gpsTime.tm_year = year - 1900;
    gpsTime.tm_mon = month - 1;
    gpsTime.tm_mday = day;
    gpsTime.tm_hour = hour;
    gpsTime.tm_min = min;
    gpsTime.tm_sec = sec;
    gpsTime.tm_isdst = 0;

    // Save current TZ, force UTC, call mktime, restore TZ (portable timegm()).
    // Copy the old TZ into a local buffer first: getenv() returns a pointer into
    // the environment that the intervening setenv() can reallocate, so using it
    // afterwards would be a use-after-free.
    const char* curTZ = getenv("TZ");
    char savedTZ[48];
    bool hadTZ = (curTZ != nullptr);
    if (hadTZ) {
        strncpy(savedTZ, curTZ, sizeof(savedTZ) - 1);
        savedTZ[sizeof(savedTZ) - 1] = '\0';
    }
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&gpsTime);
    if (hadTZ) {
        setenv("TZ", savedTZ, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    
    // Sanity check year (must be at least 2024)
    if (year < 2024 || epoch < 1000000000) {
        return false;
    }

    // Set POSIX system clock via settimeofday
#ifdef ESP32
    struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
    settimeofday(&tv, nullptr);
#endif

    syncedEpoch_ = epoch;
    syncMillis_ = millis();
    source_ = TimeSource::GPS;

    Serial.printf("[TIME] GPS Sync SUCCESS: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, min, sec);
    
    saveToSD();
    return true;
}

bool TimeManager::loadFromSD() {
    if (!SDManager::getInstance().isReady()) {
        return false;
    }

    const char* path = "/adversary/config/time_sync.json";
    if (!SD.exists(path)) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("[TIME] Failed to parse time_sync.json");
        return false;
    }

    time_t cachedEpoch = doc["epoch"] | 0;
    
    // Only load if cached epoch is somewhat realistic (> Jan 2024)
    if (cachedEpoch > 1704067200) {
        // If we already have a better source (NTP/GPS), don't overwrite with old cache
        if (source_ == TimeSource::NTP || source_ == TimeSource::GPS) {
            return false;
        }

        syncedEpoch_ = cachedEpoch;
        syncMillis_ = millis();
        source_ = TimeSource::SD_CACHE;

#ifdef ESP32
        struct timeval tv = {.tv_sec = cachedEpoch, .tv_usec = 0};
        settimeofday(&tv, nullptr);
#endif

        Serial.printf("[TIME] Loaded from SD cache: %ld\n", (long)cachedEpoch);
        return true;
    }

    return false;
}

bool TimeManager::saveToSD() {
    if (!SDManager::getInstance().isReady() || source_ == TimeSource::NONE) {
        return false;
    }

    // Ensure directory exists
    if (!SD.exists("/adversary/config")) {
        SD.mkdir("/adversary/config");
    }

    File file = SD.open("/adversary/config/time_sync.json", FILE_WRITE);
    if (!file) {
        return false;
    }

    JsonDocument doc;
    doc["epoch"] = now();
    doc["source"] = getSourceName();
    doc["savedAt"] = time(nullptr);

    if (serializeJson(doc, file) == 0) {
        Serial.println("[TIME] Failed to write time_sync.json");
        file.close();
        return false;
    }

    file.close();
    return true;
}

time_t TimeManager::now() const {
    if (source_ == TimeSource::NONE) {
#ifdef ESP32
        return time(nullptr); // Fallback to system clock (likely 0)
#else
        return 0;
#endif
    }
    
    uint32_t elapsedSec = (millis() - syncMillis_) / 1000;
    return syncedEpoch_ + elapsedSec;
}

const char* TimeManager::getSourceName() const {
    switch (source_) {
        case TimeSource::NTP:      return "NTP";
        case TimeSource::GPS:      return "GPS";
        case TimeSource::SD_CACHE: return "SD";
        case TimeSource::NONE:     return "None";
        default:                   return "Unknown";
    }
}

uint32_t TimeManager::getSecondsSinceSync() const {
    if (source_ == TimeSource::NONE) return 0;
    return (millis() - syncMillis_) / 1000;
}

void TimeManager::resetForTest() {
    syncedEpoch_ = 0;
    syncMillis_ = 0;
    source_ = TimeSource::NONE;
}

} // namespace adversary
