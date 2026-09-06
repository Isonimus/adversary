/**
 * @file test_probe_flood.cpp
 * @brief Unit tests for the Probe Flood Attack module
 */

#include <unity.h>
#include <cstring>
#include <cstdint>

// Mock ESP32 types for native testing
#ifndef ESP32
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif

// Include the header to test types and helpers
#include "../../src/modules/attack/probe_flood.h"

// Include implementation for native testing (no ESP32 libs available)
#include "../../src/modules/attack/probe_flood.cpp"

using namespace adversary;

// ============================================================================
// Test: ProbeFloodMode enum values
// ============================================================================

void test_probe_mode_random_ssids_value() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(ProbeFloodMode::RANDOM_SSIDS));
}

void test_probe_mode_ssid_list_value() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(ProbeFloodMode::SSID_LIST));
}

void test_probe_mode_targeted_value() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(ProbeFloodMode::TARGETED));
}

void test_probe_mode_blank_value() {
    TEST_ASSERT_EQUAL(3, static_cast<int>(ProbeFloodMode::BLANK));
}

// ============================================================================
// Test: getProbeFloodModeName
// ============================================================================

void test_mode_name_random() {
    TEST_ASSERT_EQUAL_STRING("Random", getProbeFloodModeName(ProbeFloodMode::RANDOM_SSIDS));
}

void test_mode_name_custom_list() {
    TEST_ASSERT_EQUAL_STRING("Custom List", getProbeFloodModeName(ProbeFloodMode::SSID_LIST));
}

void test_mode_name_targeted() {
    TEST_ASSERT_EQUAL_STRING("Targeted", getProbeFloodModeName(ProbeFloodMode::TARGETED));
}

void test_mode_name_wildcard() {
    TEST_ASSERT_EQUAL_STRING("Wildcard", getProbeFloodModeName(ProbeFloodMode::BLANK));
}

// ============================================================================
// Test: ProbeFloodState enum values
// ============================================================================

void test_probe_state_idle() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(ProbeFloodState::IDLE));
}

void test_probe_state_running() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(ProbeFloodState::RUNNING));
}

void test_probe_state_paused() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(ProbeFloodState::PAUSED));
}

void test_probe_state_completed() {
    TEST_ASSERT_EQUAL(3, static_cast<int>(ProbeFloodState::COMPLETED));
}

void test_probe_state_error() {
    TEST_ASSERT_EQUAL(4, static_cast<int>(ProbeFloodState::ERROR));
}

// ============================================================================
// Test: ProbeFloodConfig default values
// ============================================================================

void test_config_default_mode() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(ProbeFloodMode::RANDOM_SSIDS, config.mode);
}

void test_config_default_channel() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(0, config.channel);  // 0 = hopping
}

void test_config_default_interval() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(50, config.intervalMs);
}

void test_config_default_randomize_mac() {
    ProbeFloodConfig config;
    TEST_ASSERT_TRUE(config.randomizeMac);
}

void test_config_default_max_probes() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(0, config.maxProbes);  // 0 = infinite
}

void test_config_default_ssid_empty() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(0, config.ssid[0]);
}

void test_config_default_base_mac_locally_administered() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(0x02, config.baseMac[0]);
}

void test_config_default_target_bssid_broadcast() {
    ProbeFloodConfig config;
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, config.targetBssid[i]);
    }
}

void test_config_set_ssid() {
    ProbeFloodConfig config;
    config.setSsid("TestNetwork");
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.ssid);
}

void test_config_set_ssid_truncates_long() {
    ProbeFloodConfig config;
    config.setSsid("This is a very long SSID that exceeds the 32 character limit for WiFi networks");
    TEST_ASSERT_EQUAL(32, strlen(config.ssid));
}

void test_config_set_ssid_null_safe() {
    ProbeFloodConfig config;
    config.ssid[0] = 'X';
    config.setSsid(nullptr);
    TEST_ASSERT_EQUAL('X', config.ssid[0]);
}

void test_config_set_base_mac() {
    ProbeFloodConfig config;
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    config.setBaseMac(mac);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, config.baseMac, 6);
}

void test_config_set_base_mac_null_safe() {
    ProbeFloodConfig config;
    config.setBaseMac(nullptr);
    TEST_ASSERT_EQUAL(0x02, config.baseMac[0]);
}

void test_config_set_target_bssid() {
    ProbeFloodConfig config;
    uint8_t bssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    config.setTargetBssid(bssid);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, config.targetBssid, 6);
}

void test_config_has_target_bssid_false_when_broadcast() {
    ProbeFloodConfig config;
    TEST_ASSERT_FALSE(config.hasTargetBssid());
}

void test_config_has_target_bssid_true_when_set() {
    ProbeFloodConfig config;
    uint8_t bssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    config.setTargetBssid(bssid);
    TEST_ASSERT_TRUE(config.hasTargetBssid());
}

void test_config_default_burst_mode_enabled() {
    ProbeFloodConfig config;
    TEST_ASSERT_TRUE(config.burstMode);
}

void test_config_default_burst_count() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(5, config.burstCount);
}

void test_config_default_burst_delay() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(5, config.burstDelayMs);
}

void test_config_default_channel_hop_enabled() {
    ProbeFloodConfig config;
    TEST_ASSERT_TRUE(config.channelHopEnabled);
}

void test_config_default_channel_hop_interval() {
    ProbeFloodConfig config;
    TEST_ASSERT_EQUAL(200, config.channelHopIntervalMs);
}

// ============================================================================
// Test: ProbeFloodStats
// ============================================================================

void test_stats_default_values() {
    ProbeFloodStats stats;
    TEST_ASSERT_EQUAL(0, stats.probesSent);
    TEST_ASSERT_EQUAL(0, stats.ssidsUsed);
    TEST_ASSERT_EQUAL(0, stats.startTime);
    TEST_ASSERT_EQUAL(0, stats.duration);
    TEST_ASSERT_EQUAL(1, stats.currentChannel);
}

void test_stats_reset() {
    ProbeFloodStats stats;
    stats.probesSent = 100;
    stats.ssidsUsed = 8;
    stats.startTime = 12345;
    stats.duration = 5000;
    stats.currentChannel = 6;
    
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.probesSent);
    TEST_ASSERT_EQUAL(0, stats.ssidsUsed);
    TEST_ASSERT_EQUAL(0, stats.startTime);
    TEST_ASSERT_EQUAL(0, stats.duration);
    TEST_ASSERT_EQUAL(1, stats.currentChannel);
}

void test_stats_pps_zero_when_no_duration() {
    ProbeFloodStats stats;
    stats.probesSent = 100;
    stats.duration = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.getProbesPerSecond());
}

void test_stats_pps_calculation() {
    ProbeFloodStats stats;
    stats.probesSent = 100;
    stats.duration = 10000;  // 10 seconds
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, stats.getProbesPerSecond());
}

void test_stats_pps_with_one_second() {
    ProbeFloodStats stats;
    stats.probesSent = 50;
    stats.duration = 1000;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, stats.getProbesPerSecond());
}

// ============================================================================
// Test: buildProbeRequestFrame
// ============================================================================

void test_build_probe_frame_returns_nonzero() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      "TestSSID", srcMac, bssid);
    TEST_ASSERT_GREATER_THAN(0, size);
}

void test_build_probe_frame_minimum_size() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      "Test", srcMac, bssid);
    // Minimum: 24 (header) + 2 (SSID tag) + 4 (ssid) + 10 (rates) = 40
    TEST_ASSERT_GREATER_OR_EQUAL(40, size);
}

void test_build_probe_frame_control_type() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // Frame Control: 0x40 0x00 = Management, Probe Request (subtype 4)
    TEST_ASSERT_EQUAL_HEX8(0x40, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[1]);
}

void test_build_probe_frame_dest_broadcast() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // Destination should be broadcast (FF:FF:FF:FF:FF:FF)
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buffer[4 + i]);
    }
}

void test_build_probe_frame_source_mac() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // Source address (offset 10)
    TEST_ASSERT_EQUAL_HEX8_ARRAY(srcMac, &buffer[10], 6);
}

void test_build_probe_frame_bssid_correct() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // BSSID (offset 16)
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, &buffer[16], 6);
}

void test_build_probe_frame_ssid_tag() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // SSID IE at offset 24 - Tag ID should be 0x00 (SSID)
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[24]);
    // Length should be 4 for "Test"
    TEST_ASSERT_EQUAL(4, buffer[25]);
}

void test_build_probe_frame_ssid_content() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // SSID content at offset 26
    TEST_ASSERT_EQUAL('T', buffer[26]);
    TEST_ASSERT_EQUAL('e', buffer[27]);
    TEST_ASSERT_EQUAL('s', buffer[28]);
    TEST_ASSERT_EQUAL('t', buffer[29]);
}

void test_build_probe_frame_wildcard_ssid() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      nullptr, srcMac, bssid);
    
    // Should succeed with empty SSID
    TEST_ASSERT_GREATER_THAN(0, size);
    // SSID length should be 0
    TEST_ASSERT_EQUAL(0, buffer[25]);
}

void test_build_probe_frame_supported_rates_tag() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), "Test", srcMac, bssid);
    
    // Supported Rates tag (0x01) after SSID
    // SSID ends at 26 + 4 = 30
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[30]);
    TEST_ASSERT_EQUAL(8, buffer[31]);  // 8 supported rates
}

void test_build_probe_frame_null_buffer() {
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(nullptr, 128, "Test", srcMac, bssid);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_probe_frame_null_src_mac() {
    uint8_t buffer[128];
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      "Test", nullptr, bssid);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_probe_frame_null_bssid_uses_broadcast() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      "Test", srcMac, nullptr);
    
    // Should succeed
    TEST_ASSERT_GREATER_THAN(0, size);
    // BSSID should be broadcast
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buffer[16 + i]);
    }
}

void test_build_probe_frame_buffer_too_small() {
    uint8_t buffer[20];  // Too small
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      "Test", srcMac, bssid);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_probe_frame_long_ssid() {
    uint8_t buffer[128];
    uint8_t srcMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t bssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const char* longSsid = "This is a very long SSID that exceeds 32 chars";
    
    size_t size = ProbeFlood::buildProbeRequestFrame(buffer, sizeof(buffer), 
                                                      longSsid, srcMac, bssid);
    
    // Should succeed but truncate SSID to 32 chars
    TEST_ASSERT_GREATER_THAN(0, size);
    TEST_ASSERT_EQUAL(32, buffer[25]);
}

// ============================================================================
// Test: ProbeFlood singleton
// ============================================================================

void test_singleton_returns_same_instance() {
    ProbeFlood& instance1 = ProbeFlood::getInstance();
    ProbeFlood& instance2 = ProbeFlood::getInstance();
    TEST_ASSERT_EQUAL(&instance1, &instance2);
}

void test_singleton_initial_state_idle() {
    ProbeFlood& pf = ProbeFlood::getInstance();
    pf.stop();  // Ensure clean state
    TEST_ASSERT_EQUAL(ProbeFloodState::IDLE, pf.getState());
}

void test_singleton_not_running_initially() {
    ProbeFlood& pf = ProbeFlood::getInstance();
    pf.stop();
    TEST_ASSERT_FALSE(pf.isRunning());
}

void test_singleton_not_paused_initially() {
    ProbeFlood& pf = ProbeFlood::getInstance();
    pf.stop();
    TEST_ASSERT_FALSE(pf.isPaused());
}

// ============================================================================
// Main - Run all tests
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // ProbeFloodMode enum tests
    RUN_TEST(test_probe_mode_random_ssids_value);
    RUN_TEST(test_probe_mode_ssid_list_value);
    RUN_TEST(test_probe_mode_targeted_value);
    RUN_TEST(test_probe_mode_blank_value);
    
    // Mode name tests
    RUN_TEST(test_mode_name_random);
    RUN_TEST(test_mode_name_custom_list);
    RUN_TEST(test_mode_name_targeted);
    RUN_TEST(test_mode_name_wildcard);
    
    // ProbeFloodState enum tests
    RUN_TEST(test_probe_state_idle);
    RUN_TEST(test_probe_state_running);
    RUN_TEST(test_probe_state_paused);
    RUN_TEST(test_probe_state_completed);
    RUN_TEST(test_probe_state_error);
    
    // ProbeFloodConfig tests
    RUN_TEST(test_config_default_mode);
    RUN_TEST(test_config_default_channel);
    RUN_TEST(test_config_default_interval);
    RUN_TEST(test_config_default_randomize_mac);
    RUN_TEST(test_config_default_max_probes);
    RUN_TEST(test_config_default_ssid_empty);
    RUN_TEST(test_config_default_base_mac_locally_administered);
    RUN_TEST(test_config_default_target_bssid_broadcast);
    RUN_TEST(test_config_set_ssid);
    RUN_TEST(test_config_set_ssid_truncates_long);
    RUN_TEST(test_config_set_ssid_null_safe);
    RUN_TEST(test_config_set_base_mac);
    RUN_TEST(test_config_set_base_mac_null_safe);
    RUN_TEST(test_config_set_target_bssid);
    RUN_TEST(test_config_has_target_bssid_false_when_broadcast);
    RUN_TEST(test_config_has_target_bssid_true_when_set);
    RUN_TEST(test_config_default_burst_mode_enabled);
    RUN_TEST(test_config_default_burst_count);
    RUN_TEST(test_config_default_burst_delay);
    RUN_TEST(test_config_default_channel_hop_enabled);
    RUN_TEST(test_config_default_channel_hop_interval);
    
    // ProbeFloodStats tests
    RUN_TEST(test_stats_default_values);
    RUN_TEST(test_stats_reset);
    RUN_TEST(test_stats_pps_zero_when_no_duration);
    RUN_TEST(test_stats_pps_calculation);
    RUN_TEST(test_stats_pps_with_one_second);
    
    // buildProbeRequestFrame tests
    RUN_TEST(test_build_probe_frame_returns_nonzero);
    RUN_TEST(test_build_probe_frame_minimum_size);
    RUN_TEST(test_build_probe_frame_control_type);
    RUN_TEST(test_build_probe_frame_dest_broadcast);
    RUN_TEST(test_build_probe_frame_source_mac);
    RUN_TEST(test_build_probe_frame_bssid_correct);
    RUN_TEST(test_build_probe_frame_ssid_tag);
    RUN_TEST(test_build_probe_frame_ssid_content);
    RUN_TEST(test_build_probe_frame_wildcard_ssid);
    RUN_TEST(test_build_probe_frame_supported_rates_tag);
    RUN_TEST(test_build_probe_frame_null_buffer);
    RUN_TEST(test_build_probe_frame_null_src_mac);
    RUN_TEST(test_build_probe_frame_null_bssid_uses_broadcast);
    RUN_TEST(test_build_probe_frame_buffer_too_small);
    RUN_TEST(test_build_probe_frame_long_ssid);
    
    // Singleton tests
    RUN_TEST(test_singleton_returns_same_instance);
    RUN_TEST(test_singleton_initial_state_idle);
    RUN_TEST(test_singleton_not_running_initially);
    RUN_TEST(test_singleton_not_paused_initially);
    
    return UNITY_END();
}
