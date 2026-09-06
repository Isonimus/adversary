/**
 * @file test_karma_ap.cpp
 * @brief Comprehensive unit tests for the Karma AP Attack module
 * 
 * Tests cover:
 * - KarmaState enum values
 * - KarmaStats struct and getDuration()
 * - ProbeRequest struct matching functions
 * - Lifecycle methods (startListening, startActive, stop)
 * - Probe handling and tracking
 * - SSID rotation logic
 * - Cooldown management
 * - Stats tracking
 */

#include <unity.h>
#include <cstring>
#include <cstdint>
#include <vector>
#include <functional>

// Mock ESP32 types and Arduino for native testing
#ifndef ESP32

// Define UNIT_TEST for the implementation
#define UNIT_TEST 1

// Include centralized test mocks
#include "../common/time_mocks.h"
#include "../common/arduino_mocks.h"
#include "../common/esp32_mocks.h"

#endif

// Include header only - implementation will be linked separately
#include "../../src/modules/attack/karma_ap.h"

using namespace attack;

// ============================================================================
// Test Setup/Teardown
// ============================================================================

void setUp(void) {
    setMockMillis(1000);
}

void tearDown(void) {
}

// ============================================================================
// Enum Tests
// ============================================================================

void test_karma_state_enum_values() {
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(KarmaState::IDLE));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(KarmaState::LISTENING));
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(KarmaState::ACTIVE));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(KarmaState::ERROR));
}

// ============================================================================
// Struct Tests
// ============================================================================

void test_probe_request_defaults() {
    ProbeRequest probe;
    
    // Check MAC is zeroed
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, probe.clientMAC[i]);
    }
    
    TEST_ASSERT_EQUAL_STRING("", probe.ssid);
    TEST_ASSERT_EQUAL_INT8(0, probe.rssi);
    TEST_ASSERT_EQUAL_UINT32(0, probe.timestamp);
    TEST_ASSERT_EQUAL_UINT16(1, probe.count);
}

void test_probe_request_matches_mac() {
    ProbeRequest probe;
    uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    memcpy(probe.clientMAC, mac1, 6);
    
    TEST_ASSERT_TRUE(probe.matchesMAC(mac1));
    TEST_ASSERT_FALSE(probe.matchesMAC(mac2));
}

void test_probe_request_matches_ssid() {
    ProbeRequest probe;
    strcpy(probe.ssid, "TestNetwork");
    
    TEST_ASSERT_TRUE(probe.matchesSSID("TestNetwork"));
    TEST_ASSERT_FALSE(probe.matchesSSID("OtherNetwork"));
    TEST_ASSERT_FALSE(probe.matchesSSID(""));
}

void test_karma_stats_defaults() {
    KarmaStats stats;
    
    TEST_ASSERT_EQUAL_UINT32(0, stats.probesCaptured);
    TEST_ASSERT_EQUAL_UINT32(0, stats.uniqueClients);
    TEST_ASSERT_EQUAL_UINT32(0, stats.uniqueSSIDs);
    TEST_ASSERT_EQUAL_UINT32(0, stats.beaconsSent);
    TEST_ASSERT_EQUAL_UINT32(0, stats.clientsConnected);
    TEST_ASSERT_EQUAL_UINT32(0, stats.credentialsCaptured);
    TEST_ASSERT_EQUAL_UINT32(0, stats.startTime);
}

void test_karma_stats_duration() {
    KarmaStats stats;
    
    // No start time = 0 duration
    TEST_ASSERT_EQUAL_UINT32(0, stats.getDuration());
    
    // With start time
    setMockMillis(5000);
    stats.startTime = 2000;
    TEST_ASSERT_EQUAL_UINT32(3000, stats.getDuration());
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

void test_karma_ap_construction() {
    KarmaAP karma;
    
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::IDLE), static_cast<int>(karma.getState()));
    TEST_ASSERT_FALSE(karma.isRunning());
    TEST_ASSERT_EQUAL_UINT32(0, karma.getStats().probesCaptured);
}

void test_start_listening_from_idle() {
    KarmaAP karma;
    
    bool result = karma.startListening();
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::LISTENING), static_cast<int>(karma.getState()));
    TEST_ASSERT_TRUE(karma.isRunning());
    TEST_ASSERT_NOT_EQUAL(0, karma.getStats().startTime);
}

void test_start_active_from_idle() {
    KarmaAP karma;
    ap::CaptivePortalConfig config;
    config.enableCaptivePortal = true;
    
    bool result = karma.startActive(config);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::ACTIVE), static_cast<int>(karma.getState()));
    TEST_ASSERT_TRUE(karma.isRunning());
}

void test_stop_from_listening() {
    KarmaAP karma;
    karma.startListening();
    
    karma.stop();
    
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::IDLE), static_cast<int>(karma.getState()));
    TEST_ASSERT_FALSE(karma.isRunning());
}

void test_stop_from_active() {
    KarmaAP karma;
    ap::CaptivePortalConfig config;
    karma.startActive(config);
    
    karma.stop();
    
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::IDLE), static_cast<int>(karma.getState()));
    TEST_ASSERT_FALSE(karma.isRunning());
}

void test_stop_when_already_idle() {
    KarmaAP karma;
    
    karma.stop();  // Should not crash
    
    TEST_ASSERT_EQUAL_INT(static_cast<int>(KarmaState::IDLE), static_cast<int>(karma.getState()));
}

// ============================================================================
// Probe Handling Tests
// ============================================================================

void test_handle_probe_new_request() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "TestNetwork", -50);
    
    TEST_ASSERT_EQUAL_UINT32(1, karma.getStats().probesCaptured);
    TEST_ASSERT_EQUAL_size_t(1, karma.getProbes().size());
    
    const ProbeRequest& probe = karma.getProbes()[0];
    TEST_ASSERT_TRUE(probe.matchesMAC(mac));
    TEST_ASSERT_TRUE(probe.matchesSSID("TestNetwork"));
    TEST_ASSERT_EQUAL_INT8(-50, probe.rssi);
    TEST_ASSERT_EQUAL_UINT16(1, probe.count);
}

void test_handle_probe_duplicate_increments_count() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "TestNetwork", -50);
    karma.handleProbe(mac, "TestNetwork", -45);
    
    TEST_ASSERT_EQUAL_UINT32(2, karma.getStats().probesCaptured);
    TEST_ASSERT_EQUAL_size_t(1, karma.getProbes().size());
    TEST_ASSERT_EQUAL_UINT16(2, karma.getProbes()[0].count);
    TEST_ASSERT_EQUAL_INT8(-45, karma.getProbes()[0].rssi);  // Updated RSSI
}

void test_handle_probe_different_ssid_same_client() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "Network1", -50);
    karma.handleProbe(mac, "Network2", -55);
    
    TEST_ASSERT_EQUAL_UINT32(2, karma.getStats().probesCaptured);
    TEST_ASSERT_EQUAL_size_t(2, karma.getProbes().size());
}

void test_clear_probes() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "Test", -50);
    
    karma.clearProbes();
    
    TEST_ASSERT_EQUAL_size_t(0, karma.getProbes().size());
}

void test_should_ignore_empty_ssid() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "", -50);
    
    // Should be ignored
    TEST_ASSERT_EQUAL_UINT32(0, karma.getStats().probesCaptured);
    TEST_ASSERT_EQUAL_size_t(0, karma.getProbes().size());
}

void test_should_ignore_short_ssid() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "A", -50);
    
    // Should be ignored (too short)
    TEST_ASSERT_EQUAL_UINT32(0, karma.getStats().probesCaptured);
    TEST_ASSERT_EQUAL_size_t(0, karma.getProbes().size());
}

void test_should_ignore_broadcast_ssid() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "Broadcast", -50);
    
    // Should be ignored
    TEST_ASSERT_EQUAL_size_t(0, karma.getProbes().size());
}

// ============================================================================
// SSID Tracking Tests
// ============================================================================

void test_get_unique_ssids_empty() {
    KarmaAP karma;
    karma.startListening();
    
    auto ssids = karma.getUniqueSSIDs();
    
    TEST_ASSERT_EQUAL_size_t(0, ssids.size());
}

void test_get_unique_ssids_sorted_by_rssi() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    karma.handleProbe(mac1, "WeakNet", -80);
    karma.handleProbe(mac2, "StrongNet", -40);
    karma.handleProbe(mac1, "MediumNet", -60);
    
    auto ssids = karma.getUniqueSSIDs();
    
    TEST_ASSERT_EQUAL_size_t(3, ssids.size());
    // Sorted by RSSI (strongest first)
    TEST_ASSERT_EQUAL_STRING("StrongNet", ssids[0]);
    TEST_ASSERT_EQUAL_STRING("MediumNet", ssids[1]);
    TEST_ASSERT_EQUAL_STRING("WeakNet", ssids[2]);
}

void test_get_unique_clients() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    karma.handleProbe(mac1, "Network1", -50);
    karma.handleProbe(mac1, "Network2", -50);
    karma.handleProbe(mac2, "Network1", -60);
    
    auto clients = karma.getUniqueClients();
    
    TEST_ASSERT_EQUAL_size_t(2, clients.size());
}

// ============================================================================
// Stats Tracking Tests
// ============================================================================

void test_update_stats_unique_counts() {
    KarmaAP karma;
    karma.startListening();
    
    uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t mac2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    karma.handleProbe(mac1, "Network1", -50);
    karma.handleProbe(mac1, "Network2", -55);
    karma.handleProbe(mac2, "Network1", -60);
    
    karma.update();  // Updates stats
    
    const KarmaStats& stats = karma.getStats();
    TEST_ASSERT_EQUAL_UINT32(3, stats.probesCaptured);
    TEST_ASSERT_EQUAL_UINT32(2, stats.uniqueClients);
    TEST_ASSERT_EQUAL_UINT32(2, stats.uniqueSSIDs);
}

// ============================================================================
// Cooldown Management Tests
// ============================================================================

void test_ssid_cooldown_not_on_cooldown_initially() {
    KarmaAP karma;
    
    TEST_ASSERT_FALSE(karma.isSSIDOnCooldown("TestNetwork"));
}

void test_ssid_cooldown_after_marking() {
    KarmaAP karma;
    setMockMillis(1000);
    
    karma.markSSIDTried("TestNetwork");
    
    // Immediately after, should be on cooldown
    TEST_ASSERT_TRUE(karma.isSSIDOnCooldown("TestNetwork"));
}

void test_ssid_cooldown_expires() {
    KarmaAP karma;
    setMockMillis(1000);
    
    karma.markSSIDTried("TestNetwork");
    
    // Fast forward past cooldown period (default 5 minutes in tests)
    setMockMillis(1000 + (6 * 60 * 1000));
    
    TEST_ASSERT_FALSE(karma.isSSIDOnCooldown("TestNetwork"));
}

void test_clear_ssid_cooldown() {
    KarmaAP karma;
    setMockMillis(1000);
    
    karma.markSSIDTried("TestNetwork");
    TEST_ASSERT_TRUE(karma.isSSIDOnCooldown("TestNetwork"));
    
    karma.clearSSIDCooldown("TestNetwork");
    TEST_ASSERT_FALSE(karma.isSSIDOnCooldown("TestNetwork"));
}

// ============================================================================
// Configuration Tests
// ============================================================================

void test_set_max_probes() {
    KarmaAP karma;
    karma.setMaxProbes(50);
    
    karma.startListening();
    
    // Add 60 probes
    for (int i = 0; i < 60; i++) {
        uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, (uint8_t)i};
        char ssid[33];
        sprintf(ssid, "Network%d", i);
        karma.handleProbe(mac, ssid, -50);
    }
    
    // Should cap at 50
    TEST_ASSERT_EQUAL_size_t(50, karma.getProbes().size());
}

void test_set_max_ssids() {
    KarmaAP karma;
    karma.setMaxSSIDs(10);
    
    karma.startListening();
    
    // Add 15 unique SSIDs
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    for (int i = 0; i < 15; i++) {
        char ssid[33];
        sprintf(ssid, "Network%d", i);
        karma.handleProbe(mac, ssid, -50);
    }
    
    auto ssids = karma.getUniqueSSIDs();
    // Should cap at 10
    TEST_ASSERT_EQUAL_size_t(10, ssids.size());
}

// ============================================================================
// Callback Tests
// ============================================================================

// NOTE: test_probe_callback_triggered removed — onProbeRequest API was replaced by EventBus

// ============================================================================
// SSID Rotation Tests
// ============================================================================

void test_karma_ssid_rotation_cycles_captured_ssids() {
    KarmaAP karma;
    karma.startActive(ap::CaptivePortalConfig());
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "Net1", -40);
    karma.handleProbe(mac, "Net2", -50);
    karma.handleProbe(mac, "Net3", -60);
    
    // Initial SSID (generic)
    TEST_ASSERT_EQUAL_STRING("FreeWiFi", karma.getCurrentSSID());
    
    // First rotation -> picks index (0+1)%3 = 1 ("Net2")
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net2", karma.getCurrentSSID());
    TEST_ASSERT_EQUAL_UINT32(1, karma.getStats().beaconsSent);
    
    // Second rotation -> picks index (1+1)%3 = 2 ("Net3")
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net3", karma.getCurrentSSID());
    
    // Third rotation -> picks index (2+1)%3 = 0 ("Net1")
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net1", karma.getCurrentSSID());
    
    // Fourth rotation -> wraps back to Net1
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net1", karma.getCurrentSSID());
}

void test_karma_ssid_rotation_skips_cooldown() {
    KarmaAP karma;
    karma.startActive(ap::CaptivePortalConfig());
    
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    karma.handleProbe(mac, "Net1", -40);
    karma.handleProbe(mac, "Net2", -50);
    
    // Mark Net1 as tried (on cooldown)
    karma.markSSIDTried("Net1");
    
    // Rotate -> should skip Net1 and pick Net2
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net2", karma.getCurrentSSID());
    
    // Mark Net2 as tried
    karma.markSSIDTried("Net2");
    
    // Rotate -> everything on cooldown, should stay on Net2
    karma.rotateSSID();
    TEST_ASSERT_EQUAL_STRING("Net2", karma.getCurrentSSID());
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Enum tests
    RUN_TEST(test_karma_state_enum_values);
    
    // Struct tests
    RUN_TEST(test_probe_request_defaults);
    RUN_TEST(test_probe_request_matches_mac);
    RUN_TEST(test_probe_request_matches_ssid);
    RUN_TEST(test_karma_stats_defaults);
    RUN_TEST(test_karma_stats_duration);
    
    // Lifecycle tests
    RUN_TEST(test_karma_ap_construction);
    RUN_TEST(test_start_listening_from_idle);
    RUN_TEST(test_start_active_from_idle);
    RUN_TEST(test_stop_from_listening);
    RUN_TEST(test_stop_from_active);
    RUN_TEST(test_stop_when_already_idle);
    
    // Probe handling tests
    RUN_TEST(test_handle_probe_new_request);
    RUN_TEST(test_handle_probe_duplicate_increments_count);
    RUN_TEST(test_handle_probe_different_ssid_same_client);
    RUN_TEST(test_clear_probes);
    RUN_TEST(test_should_ignore_empty_ssid);
    RUN_TEST(test_should_ignore_short_ssid);
    RUN_TEST(test_should_ignore_broadcast_ssid);
    
    // SSID tracking tests
    RUN_TEST(test_get_unique_ssids_empty);
    RUN_TEST(test_get_unique_ssids_sorted_by_rssi);
    RUN_TEST(test_get_unique_clients);
    
    // Stats tests
    RUN_TEST(test_update_stats_unique_counts);
    
    // Cooldown tests
    RUN_TEST(test_ssid_cooldown_not_on_cooldown_initially);
    RUN_TEST(test_ssid_cooldown_after_marking);
    RUN_TEST(test_ssid_cooldown_expires);
    RUN_TEST(test_clear_ssid_cooldown);
    
    // Configuration tests
    RUN_TEST(test_set_max_probes);
    RUN_TEST(test_set_max_ssids);
    
    // Rotation tests
    RUN_TEST(test_karma_ssid_rotation_cycles_captured_ssids);
    RUN_TEST(test_karma_ssid_rotation_skips_cooldown);
    
    return UNITY_END();
}
