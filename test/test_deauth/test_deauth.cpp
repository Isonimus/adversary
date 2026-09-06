/**
 * @file test_deauth.cpp
 * @brief Unit tests for the Deauth Attack module
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
#include "../../src/modules/attack/deauth.h"

// Include implementation for native testing (no ESP32 libs available)
#include "../../src/modules/attack/deauth.cpp"

using namespace adversary;

// ============================================================================
// Test: DeauthReason enum values
// ============================================================================

void test_deauth_reason_unspecified_value() {
    TEST_ASSERT_EQUAL(1, static_cast<uint16_t>(DeauthReason::UNSPECIFIED));
}

void test_deauth_reason_leaving_value() {
    TEST_ASSERT_EQUAL(3, static_cast<uint16_t>(DeauthReason::DEAUTH_LEAVING));
}

void test_deauth_reason_inactivity_value() {
    TEST_ASSERT_EQUAL(4, static_cast<uint16_t>(DeauthReason::DISASSOC_DUE_TO_INACTIVITY));
}

void test_deauth_reason_class2_value() {
    TEST_ASSERT_EQUAL(6, static_cast<uint16_t>(DeauthReason::CLASS2_FRAME_FROM_NONAUTH));
}

void test_deauth_reason_class3_value() {
    TEST_ASSERT_EQUAL(7, static_cast<uint16_t>(DeauthReason::CLASS3_FRAME_FROM_NONASSOC));
}

void test_deauth_reason_mic_failure_value() {
    TEST_ASSERT_EQUAL(14, static_cast<uint16_t>(DeauthReason::MIC_FAILURE));
}

void test_deauth_reason_handshake_timeout_value() {
    TEST_ASSERT_EQUAL(15, static_cast<uint16_t>(DeauthReason::HANDSHAKE_TIMEOUT));
}

// ============================================================================
// Test: getDeauthReasonString
// ============================================================================

void test_reason_string_unspecified() {
    TEST_ASSERT_EQUAL_STRING("Unspecified", getDeauthReasonString(DeauthReason::UNSPECIFIED));
}

void test_reason_string_leaving() {
    TEST_ASSERT_EQUAL_STRING("Deauth - leaving", getDeauthReasonString(DeauthReason::DEAUTH_LEAVING));
}

void test_reason_string_inactivity() {
    TEST_ASSERT_EQUAL_STRING("Inactivity", getDeauthReasonString(DeauthReason::DISASSOC_DUE_TO_INACTIVITY));
}

void test_reason_string_mic_failure() {
    TEST_ASSERT_EQUAL_STRING("MIC failure", getDeauthReasonString(DeauthReason::MIC_FAILURE));
}

void test_reason_string_auth_failed() {
    TEST_ASSERT_EQUAL_STRING("Auth failed", getDeauthReasonString(DeauthReason::AUTH_FAILED));
}

void test_reason_string_unknown() {
    // Cast an invalid value to test unknown handling
    TEST_ASSERT_EQUAL_STRING("Unknown", getDeauthReasonString(static_cast<DeauthReason>(255)));
}

// ============================================================================
// Test: DeauthTargetType enum values
// ============================================================================

void test_target_type_single_client() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(DeauthTargetType::SINGLE_CLIENT));
}

void test_target_type_all_clients() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(DeauthTargetType::ALL_CLIENTS));
}

void test_target_type_ap_only() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(DeauthTargetType::AP_ONLY));
}

// ============================================================================
// Test: DeauthConfig
// ============================================================================

void test_config_default_values() {
    DeauthConfig config;
    TEST_ASSERT_EQUAL(DeauthTargetType::ALL_CLIENTS, config.targetType);
    TEST_ASSERT_EQUAL(DeauthReason::DEAUTH_LEAVING, config.reason);
    TEST_ASSERT_EQUAL(0, config.packetCount);  // Infinite
    TEST_ASSERT_EQUAL(100, config.delayMs);
    TEST_ASSERT_EQUAL(1, config.channel);
    TEST_ASSERT_TRUE(config.sendDisassoc);
}

void test_config_default_bssid_zero() {
    DeauthConfig config;
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL(0, config.apBssid[i]);
    }
}

void test_config_default_client_broadcast() {
    DeauthConfig config;
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL(0xFF, config.clientMac[i]);
    }
}

void test_config_has_valid_bssid_false_when_zero() {
    DeauthConfig config;
    TEST_ASSERT_FALSE(config.hasValidBssid());
}

void test_config_has_valid_bssid_true_when_set() {
    DeauthConfig config;
    uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    config.setApBssid(bssid);
    TEST_ASSERT_TRUE(config.hasValidBssid());
}

void test_config_set_ap_bssid() {
    DeauthConfig config;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    config.setApBssid(bssid);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, config.apBssid, 6);
}

void test_config_set_client_mac() {
    DeauthConfig config;
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    config.setClientMac(mac);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, config.clientMac, 6);
}

void test_config_set_bssid_null_safe() {
    DeauthConfig config;
    config.setApBssid(nullptr);  // Should not crash
    // BSSID should remain zero
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL(0, config.apBssid[i]);
    }
}

// ============================================================================
// Test: DeauthStats
// ============================================================================

void test_stats_default_values() {
    DeauthStats stats;
    TEST_ASSERT_EQUAL(0, stats.packetsSent);
    TEST_ASSERT_EQUAL(0, stats.deauthSent);
    TEST_ASSERT_EQUAL(0, stats.disassocSent);
    TEST_ASSERT_EQUAL(0, stats.errors);
    TEST_ASSERT_EQUAL(0, stats.startTime);
}

void test_stats_reset() {
    DeauthStats stats;
    stats.packetsSent = 100;
    stats.deauthSent = 50;
    stats.disassocSent = 50;
    stats.errors = 5;
    stats.startTime = 12345;
    
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.packetsSent);
    TEST_ASSERT_EQUAL(0, stats.deauthSent);
    TEST_ASSERT_EQUAL(0, stats.disassocSent);
    TEST_ASSERT_EQUAL(0, stats.errors);
    TEST_ASSERT_EQUAL(0, stats.startTime);
}

void test_stats_duration_zero_when_not_started() {
    DeauthStats stats;
    TEST_ASSERT_EQUAL(0, stats.getDurationSeconds());
}

void test_stats_pps_zero_when_no_duration() {
    DeauthStats stats;
    stats.packetsSent = 100;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.getPacketsPerSecond());
}

// ============================================================================
// Test: DeauthState enum values
// ============================================================================

void test_deauth_state_idle() {
    TEST_ASSERT_EQUAL(0, static_cast<int>(DeauthState::IDLE));
}

void test_deauth_state_running() {
    TEST_ASSERT_EQUAL(1, static_cast<int>(DeauthState::RUNNING));
}

void test_deauth_state_paused() {
    TEST_ASSERT_EQUAL(2, static_cast<int>(DeauthState::PAUSED));
}

void test_deauth_state_completed() {
    TEST_ASSERT_EQUAL(3, static_cast<int>(DeauthState::COMPLETED));
}

void test_deauth_state_error() {
    TEST_ASSERT_EQUAL(4, static_cast<int>(DeauthState::ERROR));
}

// ============================================================================
// Test: 802.11 Frame Constants
// ============================================================================

void test_frame_type_mgmt_value() {
    TEST_ASSERT_EQUAL(0x00, Deauth80211::FC_TYPE_MGMT);
}

void test_frame_subtype_deauth_value() {
    // Deauth: subtype 12 (1100) -> 0xC0 in frame control byte
    TEST_ASSERT_EQUAL(0xC0, Deauth80211::FC_SUBTYPE_DEAUTH);
}

void test_frame_subtype_disassoc_value() {
    // Disassoc: subtype 10 (1010) -> 0xA0 in frame control byte
    TEST_ASSERT_EQUAL(0xA0, Deauth80211::FC_SUBTYPE_DISASSOC);
}

void test_frame_mac_header_size() {
    TEST_ASSERT_EQUAL(24, Deauth80211::MAC_HEADER_SIZE);
}

void test_frame_reason_code_size() {
    TEST_ASSERT_EQUAL(2, Deauth80211::REASON_CODE_SIZE);
}

void test_frame_deauth_frame_size() {
    TEST_ASSERT_EQUAL(26, Deauth80211::DEAUTH_FRAME_SIZE);
}

void test_broadcast_mac_all_ff() {
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL(0xFF, Deauth80211::BROADCAST_MAC[i]);
    }
}

// ============================================================================
// Test: buildDeauthFrame
// ============================================================================

void test_build_deauth_frame_returns_correct_size() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    size_t len = DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                                 dest, src, bssid, 
                                                 DeauthReason::DEAUTH_LEAVING);
    TEST_ASSERT_EQUAL(26, len);
}

void test_build_deauth_frame_frame_control() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::DEAUTH_LEAVING);
    
    // Frame control byte 0: subtype (deauth = 0xC0)
    TEST_ASSERT_EQUAL(0xC0, buffer[0]);
    // Frame control byte 1: flags = 0
    TEST_ASSERT_EQUAL(0x00, buffer[1]);
}

void test_build_deauth_frame_dest_address() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::DEAUTH_LEAVING);
    
    // Destination address at bytes 4-9
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dest, &buffer[4], 6);
}

void test_build_deauth_frame_src_address() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::DEAUTH_LEAVING);
    
    // Source address at bytes 10-15
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, &buffer[10], 6);
}

void test_build_deauth_frame_bssid() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t bssid[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::DEAUTH_LEAVING);
    
    // BSSID at bytes 16-21
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, &buffer[16], 6);
}

void test_build_deauth_frame_reason_code() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::DEAUTH_LEAVING);
    
    // Reason code at bytes 24-25 (little endian)
    // DEAUTH_LEAVING = 3
    TEST_ASSERT_EQUAL(0x03, buffer[24]);  // Low byte
    TEST_ASSERT_EQUAL(0x00, buffer[25]);  // High byte
}

void test_build_deauth_frame_reason_code_mic_failure() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                    dest, src, bssid, 
                                    DeauthReason::MIC_FAILURE);
    
    // MIC_FAILURE = 14
    TEST_ASSERT_EQUAL(0x0E, buffer[24]);
    TEST_ASSERT_EQUAL(0x00, buffer[25]);
}

void test_build_deauth_frame_buffer_too_small() {
    uint8_t buffer[10];  // Too small
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    size_t len = DeauthAttack::buildDeauthFrame(buffer, sizeof(buffer), 
                                                 dest, src, bssid, 
                                                 DeauthReason::DEAUTH_LEAVING);
    TEST_ASSERT_EQUAL(0, len);
}

// ============================================================================
// Test: buildDisassocFrame
// ============================================================================

void test_build_disassoc_frame_returns_correct_size() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    size_t len = DeauthAttack::buildDisassocFrame(buffer, sizeof(buffer), 
                                                   dest, src, bssid, 
                                                   DeauthReason::DEAUTH_LEAVING);
    TEST_ASSERT_EQUAL(26, len);
}

void test_build_disassoc_frame_frame_control() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    DeauthAttack::buildDisassocFrame(buffer, sizeof(buffer), 
                                      dest, src, bssid, 
                                      DeauthReason::DEAUTH_LEAVING);
    
    // Frame control byte 0: subtype (disassoc = 0xA0)
    TEST_ASSERT_EQUAL(0xA0, buffer[0]);
    // Frame control byte 1: flags = 0
    TEST_ASSERT_EQUAL(0x00, buffer[1]);
}

void test_build_disassoc_frame_addresses() {
    uint8_t buffer[128];
    uint8_t dest[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t bssid[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    
    DeauthAttack::buildDisassocFrame(buffer, sizeof(buffer), 
                                      dest, src, bssid, 
                                      DeauthReason::DEAUTH_LEAVING);
    
    // Check all three addresses
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dest, &buffer[4], 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, &buffer[10], 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bssid, &buffer[16], 6);
}

void test_build_disassoc_frame_buffer_too_small() {
    uint8_t buffer[10];  // Too small
    uint8_t dest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    size_t len = DeauthAttack::buildDisassocFrame(buffer, sizeof(buffer), 
                                                   dest, src, bssid, 
                                                   DeauthReason::DEAUTH_LEAVING);
    TEST_ASSERT_EQUAL(0, len);
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // DeauthReason enum values
    RUN_TEST(test_deauth_reason_unspecified_value);
    RUN_TEST(test_deauth_reason_leaving_value);
    RUN_TEST(test_deauth_reason_inactivity_value);
    RUN_TEST(test_deauth_reason_class2_value);
    RUN_TEST(test_deauth_reason_class3_value);
    RUN_TEST(test_deauth_reason_mic_failure_value);
    RUN_TEST(test_deauth_reason_handshake_timeout_value);
    
    // Reason string tests
    RUN_TEST(test_reason_string_unspecified);
    RUN_TEST(test_reason_string_leaving);
    RUN_TEST(test_reason_string_inactivity);
    RUN_TEST(test_reason_string_mic_failure);
    RUN_TEST(test_reason_string_auth_failed);
    RUN_TEST(test_reason_string_unknown);
    
    // Target type enum
    RUN_TEST(test_target_type_single_client);
    RUN_TEST(test_target_type_all_clients);
    RUN_TEST(test_target_type_ap_only);
    
    // DeauthConfig tests
    RUN_TEST(test_config_default_values);
    RUN_TEST(test_config_default_bssid_zero);
    RUN_TEST(test_config_default_client_broadcast);
    RUN_TEST(test_config_has_valid_bssid_false_when_zero);
    RUN_TEST(test_config_has_valid_bssid_true_when_set);
    RUN_TEST(test_config_set_ap_bssid);
    RUN_TEST(test_config_set_client_mac);
    RUN_TEST(test_config_set_bssid_null_safe);
    
    // DeauthStats tests
    RUN_TEST(test_stats_default_values);
    RUN_TEST(test_stats_reset);
    RUN_TEST(test_stats_duration_zero_when_not_started);
    RUN_TEST(test_stats_pps_zero_when_no_duration);
    
    // DeauthState enum
    RUN_TEST(test_deauth_state_idle);
    RUN_TEST(test_deauth_state_running);
    RUN_TEST(test_deauth_state_paused);
    RUN_TEST(test_deauth_state_completed);
    RUN_TEST(test_deauth_state_error);
    
    // 802.11 frame constants
    RUN_TEST(test_frame_type_mgmt_value);
    RUN_TEST(test_frame_subtype_deauth_value);
    RUN_TEST(test_frame_subtype_disassoc_value);
    RUN_TEST(test_frame_mac_header_size);
    RUN_TEST(test_frame_reason_code_size);
    RUN_TEST(test_frame_deauth_frame_size);
    RUN_TEST(test_broadcast_mac_all_ff);
    
    // buildDeauthFrame tests
    RUN_TEST(test_build_deauth_frame_returns_correct_size);
    RUN_TEST(test_build_deauth_frame_frame_control);
    RUN_TEST(test_build_deauth_frame_dest_address);
    RUN_TEST(test_build_deauth_frame_src_address);
    RUN_TEST(test_build_deauth_frame_bssid);
    RUN_TEST(test_build_deauth_frame_reason_code);
    RUN_TEST(test_build_deauth_frame_reason_code_mic_failure);
    RUN_TEST(test_build_deauth_frame_buffer_too_small);
    
    // buildDisassocFrame tests
    RUN_TEST(test_build_disassoc_frame_returns_correct_size);
    RUN_TEST(test_build_disassoc_frame_frame_control);
    RUN_TEST(test_build_disassoc_frame_addresses);
    RUN_TEST(test_build_disassoc_frame_buffer_too_small);
    
    return UNITY_END();
}
