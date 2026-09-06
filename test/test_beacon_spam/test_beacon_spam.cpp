/**
 * @file test_beacon_spam.cpp
 * @brief Unit tests for the Beacon Spam Attack module
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
#include "../../src/modules/attack/beacon_spam.h"

// Include implementation for native testing (no ESP32 libs available)
#include "../../src/modules/attack/beacon_spam.cpp"

using namespace adversary;

// ============================================================================
// Test: BeaconSpamMode enum values
// ============================================================================

void test_beacon_mode_single_ssid_value() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(BeaconSpamMode::SINGLE_SSID));
}

void test_beacon_mode_random_ssids_value() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(BeaconSpamMode::RANDOM_SSIDS));
}

void test_beacon_mode_ssid_list_value() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(BeaconSpamMode::SSID_LIST));
}

void test_beacon_mode_rickroll_value() {
    TEST_ASSERT_EQUAL(3, static_cast<int>(BeaconSpamMode::RICKROLL));
}

void test_beacon_mode_funny_value() {
    TEST_ASSERT_EQUAL(4, static_cast<int>(BeaconSpamMode::FUNNY));
}

void test_beacon_mode_offensive_value() {
    TEST_ASSERT_EQUAL(5, static_cast<int>(BeaconSpamMode::OFFENSIVE));
}

// ============================================================================
// Test: getBeaconSpamModeName
// ============================================================================

void test_mode_name_single_ssid() {
    TEST_ASSERT_EQUAL_STRING("Single SSID", getBeaconSpamModeName(BeaconSpamMode::SINGLE_SSID));
}

void test_mode_name_random() {
    TEST_ASSERT_EQUAL_STRING("Random", getBeaconSpamModeName(BeaconSpamMode::RANDOM_SSIDS));
}

void test_mode_name_custom_list() {
    TEST_ASSERT_EQUAL_STRING("Custom List", getBeaconSpamModeName(BeaconSpamMode::SSID_LIST));
}

void test_mode_name_rickroll() {
    TEST_ASSERT_EQUAL_STRING("Rick Roll", getBeaconSpamModeName(BeaconSpamMode::RICKROLL));
}

void test_mode_name_funny() {
    TEST_ASSERT_EQUAL_STRING("Funny", getBeaconSpamModeName(BeaconSpamMode::FUNNY));
}

void test_mode_name_offensive() {
    TEST_ASSERT_EQUAL_STRING("Offensive", getBeaconSpamModeName(BeaconSpamMode::OFFENSIVE));
}

// ============================================================================
// Test: BeaconSpamState enum values
// ============================================================================

void test_beacon_state_idle() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(BeaconSpamState::IDLE));
}

void test_beacon_state_running() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(BeaconSpamState::RUNNING));
}

void test_beacon_state_paused() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(BeaconSpamState::PAUSED));
}

void test_beacon_state_completed() {
    TEST_ASSERT_EQUAL(3, static_cast<int>(BeaconSpamState::COMPLETED));
}

void test_beacon_state_error() {
    TEST_ASSERT_EQUAL(4, static_cast<int>(BeaconSpamState::ERROR));
}

// ============================================================================
// Test: BeaconSpamConfig default values
// ============================================================================

void test_config_default_mode() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(BeaconSpamMode::RICKROLL, config.mode);
}

void test_config_default_channel() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(1, config.channel);
}

void test_config_default_interval() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(100, config.intervalMs);
}

void test_config_default_randomize_bssid() {
    BeaconSpamConfig config;
    TEST_ASSERT_TRUE(config.randomizeBssid);
}

void test_config_default_encrypted() {
    BeaconSpamConfig config;
    TEST_ASSERT_FALSE(config.encryptedNetwork);
}

void test_config_default_max_beacons() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(0, config.maxBeacons);  // 0 = infinite
}

void test_config_default_ssid_empty() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(0, config.ssid[0]);
}

void test_config_default_base_bssid_locally_administered() {
    BeaconSpamConfig config;
    // First byte should have locally administered bit set (0x02)
    TEST_ASSERT_EQUAL(0x02, config.baseBssid[0]);
}

void test_config_set_ssid() {
    BeaconSpamConfig config;
    config.setSsid("TestNetwork");
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.ssid);
}

void test_config_set_ssid_truncates_long() {
    BeaconSpamConfig config;
    config.setSsid("This is a very long SSID that exceeds the 32 character limit for WiFi networks");
    TEST_ASSERT_EQUAL(32, strlen(config.ssid));
}

void test_config_set_ssid_null_safe() {
    BeaconSpamConfig config;
    config.ssid[0] = 'X';
    config.setSsid(nullptr);
    // Should not crash, SSID unchanged
    TEST_ASSERT_EQUAL('X', config.ssid[0]);
}

void test_config_set_base_bssid() {
    BeaconSpamConfig config;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    config.setBaseBssid(bssid);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, config.baseBssid, 6);
}

void test_config_set_base_bssid_null_safe() {
    BeaconSpamConfig config;
    config.setBaseBssid(nullptr);
    // Should not crash, first byte should still be 0x02
    TEST_ASSERT_EQUAL(0x02, config.baseBssid[0]);
}

void test_config_default_burst_mode_enabled() {
    BeaconSpamConfig config;
    TEST_ASSERT_TRUE(config.burstMode);
}

void test_config_default_burst_delay() {
    BeaconSpamConfig config;
    TEST_ASSERT_EQUAL(10, config.burstDelayMs);
}

// ============================================================================
// Test: BeaconSpamStats
// ============================================================================

void test_stats_default_values() {
    BeaconSpamStats stats;
    TEST_ASSERT_EQUAL(0, stats.beaconsSent);
    TEST_ASSERT_EQUAL(0, stats.ssidsUsed);
    TEST_ASSERT_EQUAL(0, stats.startTime);
    TEST_ASSERT_EQUAL(0, stats.duration);
}

void test_stats_reset() {
    BeaconSpamStats stats;
    stats.beaconsSent = 100;
    stats.ssidsUsed = 8;
    stats.startTime = 12345;
    stats.duration = 5000;
    
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.beaconsSent);
    TEST_ASSERT_EQUAL(0, stats.ssidsUsed);
    TEST_ASSERT_EQUAL(0, stats.startTime);
    TEST_ASSERT_EQUAL(0, stats.duration);
}

void test_stats_pps_zero_when_no_duration() {
    BeaconSpamStats stats;
    stats.beaconsSent = 100;
    stats.duration = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.getBeaconsPerSecond());
}

void test_stats_pps_calculation() {
    BeaconSpamStats stats;
    stats.beaconsSent = 100;
    stats.duration = 10000;  // 10 seconds
    // Should be 10 beacons per second
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, stats.getBeaconsPerSecond());
}

void test_stats_pps_with_one_second() {
    BeaconSpamStats stats;
    stats.beaconsSent = 50;
    stats.duration = 1000;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, stats.getBeaconsPerSecond());
}

// ============================================================================
// Test: getSSIDList function
// ============================================================================

void test_get_ssid_list_rickroll_not_empty() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::RICKROLL);
    TEST_ASSERT_GREATER_THAN(0, list.size());
}

void test_get_ssid_list_rickroll_has_8_lines() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::RICKROLL);
    TEST_ASSERT_EQUAL(8, list.size());
}

void test_get_ssid_list_rickroll_first_line() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::RICKROLL);
    TEST_ASSERT_EQUAL_STRING("Never Gonna Give You Up", list[0].c_str());
}

void test_get_ssid_list_funny_not_empty() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::FUNNY);
    TEST_ASSERT_GREATER_THAN(0, list.size());
}

void test_get_ssid_list_funny_has_many() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::FUNNY);
    TEST_ASSERT_GREATER_OR_EQUAL(20, list.size());  // At least 20 funny SSIDs
}

void test_get_ssid_list_offensive_not_empty() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::OFFENSIVE);
    TEST_ASSERT_GREATER_THAN(0, list.size());
}

void test_get_ssid_list_single_returns_empty() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::SINGLE_SSID);
    TEST_ASSERT_EQUAL(0, list.size());
}

void test_get_ssid_list_random_returns_empty() {
    std::vector<std::string> list = getSSIDList(BeaconSpamMode::RANDOM_SSIDS);
    TEST_ASSERT_EQUAL(0, list.size());
}

// ============================================================================
// Test: buildBeaconFrame
// ============================================================================

void test_build_beacon_frame_returns_nonzero() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               "TestSSID", bssid, 6, false);
    TEST_ASSERT_GREATER_THAN(0, size);
}

void test_build_beacon_frame_minimum_size() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               "Test", bssid, 1, false);
    // Minimum: 24 (header) + 12 (fixed) + 2 (SSID tag) + 4 (ssid) + rates + DS param
    TEST_ASSERT_GREATER_OR_EQUAL(50, size);
}

void test_build_beacon_frame_control_type() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Frame Control: 0x80 0x00 = Management, Beacon
    TEST_ASSERT_EQUAL_HEX8(0x80, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[1]);
}

void test_build_beacon_frame_dest_broadcast() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Destination should be broadcast (FF:FF:FF:FF:FF:FF)
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buffer[4 + i]);
    }
}

void test_build_beacon_frame_source_is_bssid() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Source address (offset 10) should be BSSID
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, &buffer[10], 6);
}

void test_build_beacon_frame_bssid_correct() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // BSSID (offset 16) should match
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, &buffer[16], 6);
}

void test_build_beacon_frame_beacon_interval() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Beacon interval at offset 32 (after 24-byte header + 8-byte timestamp)
    // Should be 0x64 0x00 = 100 TUs (~102.4ms)
    TEST_ASSERT_EQUAL_HEX8(0x64, buffer[32]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[33]);
}

void test_build_beacon_frame_capability_ess() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Capability info at offset 34 - ESS bit should be set (0x01)
    TEST_ASSERT_BITS(0x01, 0x01, buffer[34]);
}

void test_build_beacon_frame_capability_privacy_off() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Privacy bit (0x10) should NOT be set for open network
    TEST_ASSERT_BITS_LOW(0x10, buffer[34]);
}

void test_build_beacon_frame_capability_privacy_on() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, true);
    
    // Privacy bit (0x10) SHOULD be set for encrypted network
    TEST_ASSERT_BITS(0x10, 0x10, buffer[34]);
}

void test_build_beacon_frame_ssid_tag() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // SSID IE at offset 36 - Tag ID should be 0x00 (SSID)
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[36]);
    // Length should be 4 for "Test"
    TEST_ASSERT_EQUAL(4, buffer[37]);
}

void test_build_beacon_frame_ssid_content() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // SSID content at offset 38
    TEST_ASSERT_EQUAL('T', buffer[38]);
    TEST_ASSERT_EQUAL('e', buffer[39]);
    TEST_ASSERT_EQUAL('s', buffer[40]);
    TEST_ASSERT_EQUAL('t', buffer[41]);
}

void test_build_beacon_frame_supported_rates_tag() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // Supported Rates tag (0x01) should be present after SSID
    // SSID ends at 38 + 4 = 42, so Supported Rates tag at 42
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[42]);
    TEST_ASSERT_EQUAL(8, buffer[43]);  // 8 supported rates
}

void test_build_beacon_frame_ds_param_tag() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 6, false);
    
    // DS Parameter Set (tag 0x03) should be present
    // After SSID (42) + Supported Rates (10) = 52
    TEST_ASSERT_EQUAL_HEX8(0x03, buffer[52]);
    TEST_ASSERT_EQUAL(1, buffer[53]);   // Length = 1
    TEST_ASSERT_EQUAL(6, buffer[54]);   // Channel 6
}

void test_build_beacon_frame_channel_1() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 1, false);
    
    // DS Parameter channel should be 1
    TEST_ASSERT_EQUAL(1, buffer[54]);
}

void test_build_beacon_frame_channel_11() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), "Test", bssid, 11, false);
    
    TEST_ASSERT_EQUAL(11, buffer[54]);
}

void test_build_beacon_frame_encrypted_larger() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t sizeOpen = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                                    "Test", bssid, 1, false);
    size_t sizeEncrypted = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                                         "Test", bssid, 1, true);
    
    // Encrypted frame should be larger (includes RSN IE)
    TEST_ASSERT_GREATER_THAN(sizeOpen, sizeEncrypted);
}

void test_build_beacon_frame_rsn_ie_present() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                                "Test", bssid, 1, true);
    
    // Search for RSN tag (0x30) in the frame
    bool foundRsn = false;
    for (size_t i = 36; i < size - 1; i++) {
        if (buffer[i] == 0x30 && buffer[i+1] == 0x14) {  // RSN tag with length 20
            foundRsn = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(foundRsn);
}

void test_build_beacon_frame_buffer_too_small() {
    uint8_t buffer[50];  // Too small
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               "Test", bssid, 1, false);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_beacon_frame_null_ssid() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               nullptr, bssid, 1, false);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_beacon_frame_null_bssid() {
    uint8_t buffer[256];
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               "Test", nullptr, 1, false);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_beacon_frame_null_buffer() {
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    size_t size = BeaconSpam::buildBeaconFrame(nullptr, 256, "Test", bssid, 1, false);
    TEST_ASSERT_EQUAL(0, size);
}

void test_build_beacon_frame_long_ssid() {
    uint8_t buffer[256];
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const char* longSsid = "This is a very long SSID that exceeds 32 chars";
    
    size_t size = BeaconSpam::buildBeaconFrame(buffer, sizeof(buffer), 
                                               longSsid, bssid, 1, false);
    
    // Should succeed but truncate SSID to 32 chars
    TEST_ASSERT_GREATER_THAN(0, size);
    TEST_ASSERT_EQUAL(32, buffer[37]);  // SSID length capped at 32
}

// ============================================================================
// Test: BeaconSpam singleton
// ============================================================================

void test_singleton_returns_same_instance() {
    BeaconSpam& instance1 = BeaconSpam::getInstance();
    BeaconSpam& instance2 = BeaconSpam::getInstance();
    TEST_ASSERT_EQUAL(&instance1, &instance2);
}

void test_singleton_initial_state_idle() {
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.stop();  // Ensure clean state
    TEST_ASSERT_EQUAL(BeaconSpamState::IDLE, bs.getState());
}

void test_singleton_not_running_initially() {
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.stop();
    TEST_ASSERT_FALSE(bs.isRunning());
}

void test_singleton_not_paused_initially() {
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.stop();
    TEST_ASSERT_FALSE(bs.isPaused());
}

// ============================================================================
// Main - Run all tests
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // BeaconSpamMode enum tests
    RUN_TEST(test_beacon_mode_single_ssid_value);
    RUN_TEST(test_beacon_mode_random_ssids_value);
    RUN_TEST(test_beacon_mode_ssid_list_value);
    RUN_TEST(test_beacon_mode_rickroll_value);
    RUN_TEST(test_beacon_mode_funny_value);
    RUN_TEST(test_beacon_mode_offensive_value);
    
    // Mode name tests
    RUN_TEST(test_mode_name_single_ssid);
    RUN_TEST(test_mode_name_random);
    RUN_TEST(test_mode_name_custom_list);
    RUN_TEST(test_mode_name_rickroll);
    RUN_TEST(test_mode_name_funny);
    RUN_TEST(test_mode_name_offensive);
    
    // BeaconSpamState enum tests
    RUN_TEST(test_beacon_state_idle);
    RUN_TEST(test_beacon_state_running);
    RUN_TEST(test_beacon_state_paused);
    RUN_TEST(test_beacon_state_completed);
    RUN_TEST(test_beacon_state_error);
    
    // BeaconSpamConfig tests
    RUN_TEST(test_config_default_mode);
    RUN_TEST(test_config_default_channel);
    RUN_TEST(test_config_default_interval);
    RUN_TEST(test_config_default_randomize_bssid);
    RUN_TEST(test_config_default_encrypted);
    RUN_TEST(test_config_default_max_beacons);
    RUN_TEST(test_config_default_ssid_empty);
    RUN_TEST(test_config_default_base_bssid_locally_administered);
    RUN_TEST(test_config_set_ssid);
    RUN_TEST(test_config_set_ssid_truncates_long);
    RUN_TEST(test_config_set_ssid_null_safe);
    RUN_TEST(test_config_set_base_bssid);
    RUN_TEST(test_config_set_base_bssid_null_safe);
    RUN_TEST(test_config_default_burst_mode_enabled);
    RUN_TEST(test_config_default_burst_delay);
    
    // BeaconSpamStats tests
    RUN_TEST(test_stats_default_values);
    RUN_TEST(test_stats_reset);
    RUN_TEST(test_stats_pps_zero_when_no_duration);
    RUN_TEST(test_stats_pps_calculation);
    RUN_TEST(test_stats_pps_with_one_second);
    
    // getSSIDList tests
    RUN_TEST(test_get_ssid_list_rickroll_not_empty);
    RUN_TEST(test_get_ssid_list_rickroll_has_8_lines);
    RUN_TEST(test_get_ssid_list_rickroll_first_line);
    RUN_TEST(test_get_ssid_list_funny_not_empty);
    RUN_TEST(test_get_ssid_list_funny_has_many);
    RUN_TEST(test_get_ssid_list_offensive_not_empty);
    RUN_TEST(test_get_ssid_list_single_returns_empty);
    RUN_TEST(test_get_ssid_list_random_returns_empty);
    
    // buildBeaconFrame tests
    RUN_TEST(test_build_beacon_frame_returns_nonzero);
    RUN_TEST(test_build_beacon_frame_minimum_size);
    RUN_TEST(test_build_beacon_frame_control_type);
    RUN_TEST(test_build_beacon_frame_dest_broadcast);
    RUN_TEST(test_build_beacon_frame_source_is_bssid);
    RUN_TEST(test_build_beacon_frame_bssid_correct);
    RUN_TEST(test_build_beacon_frame_beacon_interval);
    RUN_TEST(test_build_beacon_frame_capability_ess);
    RUN_TEST(test_build_beacon_frame_capability_privacy_off);
    RUN_TEST(test_build_beacon_frame_capability_privacy_on);
    RUN_TEST(test_build_beacon_frame_ssid_tag);
    RUN_TEST(test_build_beacon_frame_ssid_content);
    RUN_TEST(test_build_beacon_frame_supported_rates_tag);
    RUN_TEST(test_build_beacon_frame_ds_param_tag);
    RUN_TEST(test_build_beacon_frame_channel_1);
    RUN_TEST(test_build_beacon_frame_channel_11);
    RUN_TEST(test_build_beacon_frame_encrypted_larger);
    RUN_TEST(test_build_beacon_frame_rsn_ie_present);
    RUN_TEST(test_build_beacon_frame_buffer_too_small);
    RUN_TEST(test_build_beacon_frame_null_ssid);
    RUN_TEST(test_build_beacon_frame_null_bssid);
    RUN_TEST(test_build_beacon_frame_null_buffer);
    RUN_TEST(test_build_beacon_frame_long_ssid);
    
    // Singleton tests
    RUN_TEST(test_singleton_returns_same_instance);
    RUN_TEST(test_singleton_initial_state_idle);
    RUN_TEST(test_singleton_not_running_initially);
    RUN_TEST(test_singleton_not_paused_initially);
    
    return UNITY_END();
}
