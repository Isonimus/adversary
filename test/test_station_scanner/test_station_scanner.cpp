/**
 * @file test_station_scanner.cpp
 * @brief Unit tests for station scanner module
 */

#include <unity.h>
#include "modules/wifi/station_scanner.h"

using namespace adversary;

// Test data: simulated client MACs
static const uint8_t TEST_CLIENT1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
static const uint8_t TEST_CLIENT2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
static const uint8_t TEST_CLIENT3[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
static const uint8_t TEST_BSSID1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t TEST_BSSID2[6] = {0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};

// =============================================================================
// DiscoveredStation Tests
// =============================================================================

void test_discovered_station_matches() {
    DiscoveredStation station;
    memcpy(station.mac, TEST_CLIENT1, 6);
    
    TEST_ASSERT_TRUE(station.matches(TEST_CLIENT1));
    TEST_ASSERT_FALSE(station.matches(TEST_CLIENT2));
}

void test_discovered_station_is_for_ap() {
    DiscoveredStation station;
    memcpy(station.bssid, TEST_BSSID1, 6);
    
    TEST_ASSERT_TRUE(station.isForAP(TEST_BSSID1));
    TEST_ASSERT_FALSE(station.isForAP(TEST_BSSID2));
}

// =============================================================================
// StationScanResult Tests
// =============================================================================

void test_station_scan_result_clear() {
    StationScanResult result;
    
    // Add some stations
    result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    result.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    
    TEST_ASSERT_EQUAL(2, result.count);
    
    // Clear
    result.clear();
    
    TEST_ASSERT_EQUAL(0, result.count);
}

void test_station_scan_result_add_new() {
    StationScanResult result;
    
    // Add new station
    bool isNew = result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    
    TEST_ASSERT_TRUE(isNew);
    TEST_ASSERT_EQUAL(1, result.count);
    TEST_ASSERT_TRUE(result.stations[0].matches(TEST_CLIENT1));
    TEST_ASSERT_TRUE(result.stations[0].isForAP(TEST_BSSID1));
    TEST_ASSERT_EQUAL(-50, result.stations[0].rssi);
    TEST_ASSERT_EQUAL(1, result.stations[0].frameCount);
}

void test_station_scan_result_update_existing() {
    StationScanResult result;
    
    // Add station
    result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    
    // Update same station with new RSSI
    bool isNew = result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -45);
    
    TEST_ASSERT_FALSE(isNew);  // Not new, updated existing
    TEST_ASSERT_EQUAL(1, result.count);  // Still only 1 station
    TEST_ASSERT_EQUAL(-45, result.stations[0].rssi);  // RSSI updated
    TEST_ASSERT_EQUAL(2, result.stations[0].frameCount);  // Frame count incremented
}

void test_station_scan_result_multiple_stations() {
    StationScanResult result;
    
    result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    result.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    result.addOrUpdate(TEST_CLIENT3, TEST_BSSID2, -70);
    
    TEST_ASSERT_EQUAL(3, result.count);
}

void test_station_scan_result_count_for_bssid() {
    StationScanResult result;
    
    // Add 2 clients for BSSID1
    result.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    result.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    
    // Add 1 client for BSSID2
    result.addOrUpdate(TEST_CLIENT3, TEST_BSSID2, -70);
    
    TEST_ASSERT_EQUAL(2, result.countForBssid(TEST_BSSID1));
    TEST_ASSERT_EQUAL(1, result.countForBssid(TEST_BSSID2));
}

void test_station_scan_result_max_stations() {
    StationScanResult result;
    
    // Fill to max capacity
    for (size_t i = 0; i < StationScanResult::MAX_STATIONS; i++) {
        uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, (uint8_t)(i >> 8), (uint8_t)(i & 0xFF)};
        result.addOrUpdate(mac, TEST_BSSID1, -50);
    }
    
    TEST_ASSERT_EQUAL(StationScanResult::MAX_STATIONS, result.count);
    
    // Try to add one more - should fail (return false, count unchanged)
    uint8_t extraMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool added = result.addOrUpdate(extraMac, TEST_BSSID1, -50);
    
    TEST_ASSERT_FALSE(added);
    TEST_ASSERT_EQUAL(StationScanResult::MAX_STATIONS, result.count);
}

// =============================================================================
// StationScanner Tests
// =============================================================================

void test_station_scanner_singleton() {
    StationScanner& scanner1 = StationScanner::getInstance();
    StationScanner& scanner2 = StationScanner::getInstance();
    
    TEST_ASSERT_EQUAL_PTR(&scanner1, &scanner2);
}

void test_station_scanner_initial_state() {
    StationScanner& scanner = StationScanner::getInstance();
    
    TEST_ASSERT_FALSE(scanner.isScanning());
    TEST_ASSERT_EQUAL(0, scanner.getResults().count);
}

void test_station_scanner_get_stations_for_ap() {
    StationScanner& scanner = StationScanner::getInstance();
    scanner.clearResults();
    
    // Manually add stations to results for testing
    // Note: In real usage, this would be populated by processFrame()
    // For testing, we access via addOrUpdate on the result struct
    auto& results = const_cast<StationScanResult&>(scanner.getResults());
    results.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    results.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    results.addOrUpdate(TEST_CLIENT3, TEST_BSSID2, -70);
    
    // Get stations for BSSID1
    uint8_t macs[2 * 6];
    size_t count = scanner.getStationsForAP(TEST_BSSID1, macs, 2);
    
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_MEMORY(TEST_CLIENT1, macs, 6);
    TEST_ASSERT_EQUAL_MEMORY(TEST_CLIENT2, macs + 6, 6);
}

void test_station_scanner_get_stations_for_ap_limited() {
    StationScanner& scanner = StationScanner::getInstance();
    scanner.clearResults();
    
    auto& results = const_cast<StationScanResult&>(scanner.getResults());
    results.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    results.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    results.addOrUpdate(TEST_CLIENT3, TEST_BSSID1, -70);
    
    // Request only 2 even though 3 exist
    uint8_t macs[2 * 6];
    size_t count = scanner.getStationsForAP(TEST_BSSID1, macs, 2);
    
    TEST_ASSERT_EQUAL(2, count);  // Limited to max requested
}

void test_station_scanner_get_station_count_for_ap() {
    StationScanner& scanner = StationScanner::getInstance();
    scanner.clearResults();
    
    auto& results = const_cast<StationScanResult&>(scanner.getResults());
    results.addOrUpdate(TEST_CLIENT1, TEST_BSSID1, -50);
    results.addOrUpdate(TEST_CLIENT2, TEST_BSSID1, -60);
    results.addOrUpdate(TEST_CLIENT3, TEST_BSSID2, -70);
    
    TEST_ASSERT_EQUAL(2, scanner.getStationCountForAP(TEST_BSSID1));
    TEST_ASSERT_EQUAL(1, scanner.getStationCountForAP(TEST_BSSID2));
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp() {
    // Clear scanner state before each test
    StationScanner::getInstance().clearResults();
}

void tearDown() {
    // Cleanup after each test
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    UNITY_BEGIN();
    
    // DiscoveredStation tests
    RUN_TEST(test_discovered_station_matches);
    RUN_TEST(test_discovered_station_is_for_ap);
    
    // StationScanResult tests
    RUN_TEST(test_station_scan_result_clear);
    RUN_TEST(test_station_scan_result_add_new);
    RUN_TEST(test_station_scan_result_update_existing);
    RUN_TEST(test_station_scan_result_multiple_stations);
    RUN_TEST(test_station_scan_result_count_for_bssid);
    RUN_TEST(test_station_scan_result_max_stations);
    
    // StationScanner tests
    RUN_TEST(test_station_scanner_singleton);
    RUN_TEST(test_station_scanner_initial_state);
    RUN_TEST(test_station_scanner_get_stations_for_ap);
    RUN_TEST(test_station_scanner_get_stations_for_ap_limited);
    RUN_TEST(test_station_scanner_get_station_count_for_ap);
    
    return UNITY_END();
}
