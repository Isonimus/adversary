/**
 * @file test_time_manager.cpp
 * @brief Unit tests for TimeManager
 */

#include <unity.h>
#include "modules/system/time_manager.h"
#include "hal/storage/sd_manager.h"
#include "core/event_bus.h"
#include <SD.h>
#include <ctime>

#ifndef ESP32
#include "../common/arduino_mocks.h"
#include "../common/time_mocks.h"
#endif

using namespace adversary;

// Mock SD behavior by providing a file with JSON content?
// Or just test the logic that doesn't involve real files if possible.
// TimeManager::loadFromSD uses SD.exists and SD.open.

void setUp(void) {
    SDManager::getInstance().init(); // Set to READY in native test
    TimeManager::getInstance().resetForTest();
}

void tearDown(void) {
}

void test_time_calculation(void) {
    TimeManager& tm = TimeManager::getInstance();
    
    // Set mock millis to a known value
    setMockMillis(10000); // 10 seconds since boot
    
    // Sync from GPS: 2026-02-16 12:00:00
    // struct tm: year is since 1900, mon is 0-11
    uint32_t hour = 12, min = 0, sec = 0;
    uint32_t day = 16, month = 2, year = 2026;
    
    TEST_ASSERT_TRUE(tm.syncFromGPS(hour, min, sec, day, month, year));
    TEST_ASSERT_EQUAL(TimeSource::GPS, tm.getSource());
    
    time_t syncTime = tm.now();
    
    // Advance millis by 5 seconds
    setMockMillis(15000);
    
    time_t nowTime = tm.now();
    TEST_ASSERT_EQUAL(syncTime + 5, nowTime);
}

void test_priority_gps_vs_ntp(void) {
    TimeManager& tm = TimeManager::getInstance();
    
    // We can't easily set NTP source in native without a friend class or reset
    // but let's assume we can trigger a GPS sync when no NTP is present
    
    // If we just synced GPS, another GPS sync should work (updates time)
    TEST_ASSERT_TRUE(tm.syncFromGPS(13, 0, 0, 16, 2, 2026));
    TEST_ASSERT_EQUAL(TimeSource::GPS, tm.getSource());
}

void test_seconds_since_sync(void) {
    TimeManager& tm = TimeManager::getInstance();
    
    setMockMillis(20000);
    tm.syncFromGPS(14, 0, 0, 16, 2, 2026);
    
    setMockMillis(25000);
    TEST_ASSERT_EQUAL(5, tm.getSecondsSinceSync());
}

void test_invalid_gps_time(void) {
    TimeManager& tm = TimeManager::getInstance();
    
    // Year before 2024 should fail
    TEST_ASSERT_FALSE(tm.syncFromGPS(12, 0, 0, 1, 1, 2023));
}

void test_gps_produces_correct_utc_epoch(void) {
    // Regression test: mktime() used to apply local timezone to GPS UTC time.
    // GPS gives pure UTC, so the epoch must match UTC exactly.
    TimeManager& tm = TimeManager::getInstance();
    
    setMockMillis(5000);
    
    // 2026-02-17 10:30:00 UTC
    TEST_ASSERT_TRUE(tm.syncFromGPS(10, 30, 0, 17, 2, 2026));
    
    time_t syncedTime = tm.now();
    
    // Convert back to struct tm using gmtime and verify round-trip
    struct tm* utc = gmtime(&syncedTime);
    TEST_ASSERT_NOT_NULL(utc);
    TEST_ASSERT_EQUAL(2026, utc->tm_year + 1900);
    TEST_ASSERT_EQUAL(2, utc->tm_mon + 1);
    TEST_ASSERT_EQUAL(17, utc->tm_mday);
    TEST_ASSERT_EQUAL(10, utc->tm_hour);
    TEST_ASSERT_EQUAL(30, utc->tm_min);
    TEST_ASSERT_EQUAL(0, utc->tm_sec);
}

void test_gps_overrides_stale_sd_cache(void) {
    // Regression: a real GPS fix must override a time already loaded from the SD
    // cache. The GPS re-sync throttle (GPS_RESYNC_INTERVAL_MS) deliberately
    // guards GPS→GPS churn only; an SD_CACHE source is not a GPS source, so the
    // first fix takes over immediately. An earlier version of this test faked the
    // cache with a first GPS sync, which the throttle later (correctly) began to
    // reject — so it must drive the genuine SD_CACHE → GPS path via loadFromSD().
    TimeManager& tm = TimeManager::getInstance();

    // Seed a stale cache exactly as saveToSD() writes it: 2026-02-16 19:00 UTC.
    constexpr time_t STALE_SD_EPOCH = 1771268400;  // 2026-02-16 19:00:00 UTC
    const char* cachePath = "/adversary/config/time_sync.json";
    // mkdir is non-recursive on real SD (and in the host mock), so create the base
    // before the child — the test must not assume /adversary already exists on disk.
    SD.mkdir("/adversary");
    SD.mkdir("/adversary/config");
    File cache = SD.open(cachePath, FILE_WRITE);
    TEST_ASSERT_TRUE(cache);
    cache.print("{\"epoch\":1771268400,\"source\":\"SD\",\"savedAt\":1771268400}");
    cache.close();

    setMockMillis(10000);
    TEST_ASSERT_TRUE(tm.loadFromSD());
    TEST_ASSERT_EQUAL(TimeSource::SD_CACHE, tm.getSource());
    time_t staleTime = tm.now();
    TEST_ASSERT_EQUAL(STALE_SD_EPOCH, staleTime);

    // A GPS fix ~16.5 h later must take over at once, not be throttled away.
    setMockMillis(11000);
    TEST_ASSERT_TRUE(tm.syncFromGPS(11, 30, 0, 17, 2, 2026));  // 2026-02-17 11:30 UTC
    TEST_ASSERT_EQUAL(TimeSource::GPS, tm.getSource());

    time_t correctTime = tm.now();
    long delta = (long)(correctTime - staleTime);
    TEST_ASSERT_GREATER_THAN(59000, delta);  // > 16 h forward jump

    SD.remove(cachePath);
}

void test_now_tracks_elapsed_after_source_change(void) {
    // Test that now() correctly tracks elapsed millis after a GPS sync
    TimeManager& tm = TimeManager::getInstance();
    
    setMockMillis(50000);
    tm.syncFromGPS(12, 0, 0, 17, 2, 2026);  // Feb 17 12:00:00 UTC
    
    time_t t0 = tm.now();
    
    // Advance 120 seconds
    setMockMillis(170000);
    time_t t1 = tm.now();
    
    TEST_ASSERT_EQUAL(120, t1 - t0);
    
    // Verify the absolute time is correct
    struct tm* utc = gmtime(&t1);
    TEST_ASSERT_EQUAL(12, utc->tm_hour);
    TEST_ASSERT_EQUAL(2, utc->tm_min);  // 12:00:00 + 120s = 12:02:00
    TEST_ASSERT_EQUAL(0, utc->tm_sec);
}

// ---------------------------------------------------------------------------
// evaluateNtpPoll: the pure latch-vs-timeout decision behind the decoupled async
// NTP sync (slice-0147). Guards that the WIFI_CONNECTED handler's fire-and-forget
// sync eventually latches or times out, without ever blocking the caller.
// ---------------------------------------------------------------------------

void test_ntp_poll_pending_before_complete_and_before_timeout(void) {
    // Not done yet, still inside the window -> keep waiting (handler already returned).
    TEST_ASSERT_EQUAL(NtpPoll::PENDING, evaluateNtpPoll(false, 5000, 10000));
}

void test_ntp_poll_latches_when_complete(void) {
    // SNTP reports completed within the window -> adopt NTP time.
    TEST_ASSERT_EQUAL(NtpPoll::LATCH, evaluateNtpPoll(true, 300, 10000));
}

void test_ntp_poll_times_out_when_deadline_reached(void) {
    // Deadline reached without completion -> give up (SD cache / GPS tiers hold).
    TEST_ASSERT_EQUAL(NtpPoll::TIMEOUT, evaluateNtpPoll(false, 10000, 10000));
    TEST_ASSERT_EQUAL(NtpPoll::TIMEOUT, evaluateNtpPoll(false, 12000, 10000));
}

void test_ntp_poll_complete_wins_over_timeout(void) {
    // A late completion still beats the SD-cache tier, so completion is checked
    // before the deadline: a sync that finishes exactly as the window closes latches.
    TEST_ASSERT_EQUAL(NtpPoll::LATCH, evaluateNtpPoll(true, 10000, 10000));
    TEST_ASSERT_EQUAL(NtpPoll::LATCH, evaluateNtpPoll(true, 999999, 10000));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_time_calculation);
    RUN_TEST(test_priority_gps_vs_ntp);
    RUN_TEST(test_seconds_since_sync);
    RUN_TEST(test_invalid_gps_time);
    RUN_TEST(test_gps_produces_correct_utc_epoch);
    RUN_TEST(test_gps_overrides_stale_sd_cache);
    RUN_TEST(test_now_tracks_elapsed_after_source_change);

    RUN_TEST(test_ntp_poll_pending_before_complete_and_before_timeout);
    RUN_TEST(test_ntp_poll_latches_when_complete);
    RUN_TEST(test_ntp_poll_times_out_when_deadline_reached);
    RUN_TEST(test_ntp_poll_complete_wins_over_timeout);

    return UNITY_END();
}
