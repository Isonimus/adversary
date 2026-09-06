/**
 * @file test_handshake_capture.cpp
 * @brief Unit tests for the Handshake Capture module
 * 
 * Tests the data structures and enums used by HandshakeCapture.
 * The actual HandshakeCapture class requires ESP32 WiFi APIs and 
 * can only be tested on device.
 */

#include <unity.h>
#include "modules/capture/handshake_capture.h"
#include <cstring>

using namespace adversary;

// Test fixture setup/teardown
void setUp(void) {
    // No singleton access in native tests
}

void tearDown(void) {
    // Cleanup after each test
}

// ==================== HandshakeState Tests ====================

void test_state_idle_value(void) {
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(HandshakeState::IDLE));
}

void test_state_waiting_value(void) {
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(HandshakeState::WAITING));
}

void test_state_got_msg1_value(void) {
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(HandshakeState::GOT_MSG1));
}

void test_state_got_msg2_value(void) {
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(HandshakeState::GOT_MSG2));
}

void test_state_got_msg3_value(void) {
    TEST_ASSERT_EQUAL(4, static_cast<uint8_t>(HandshakeState::GOT_MSG3));
}

void test_state_complete_value(void) {
    TEST_ASSERT_EQUAL(6, static_cast<uint8_t>(HandshakeState::COMPLETE));
}

void test_state_crackable_value(void) {
    TEST_ASSERT_EQUAL(5, static_cast<uint8_t>(HandshakeState::CRACKABLE));
}

void test_state_timeout_value(void) {
    TEST_ASSERT_EQUAL(7, static_cast<uint8_t>(HandshakeState::TIMEOUT));
}

void test_state_error_value(void) {
    TEST_ASSERT_EQUAL(8, static_cast<uint8_t>(HandshakeState::ERROR));
}

// ==================== CapturedHandshake Tests ====================

void test_captured_handshake_default_constructor(void) {
    CapturedHandshake hs;
    hs.reset();
    
    // Check SSID is empty
    TEST_ASSERT_EQUAL_STRING("", hs.ssid);
    
    // Check channel defaults to 0
    TEST_ASSERT_EQUAL(0, hs.channel);
    
    // Check all message flags are false
    TEST_ASSERT_FALSE(hs.hasMsg1);
    TEST_ASSERT_FALSE(hs.hasMsg2);
    TEST_ASSERT_FALSE(hs.hasMsg3);
    TEST_ASSERT_FALSE(hs.hasMsg4);
}

void test_captured_handshake_bssids_initialized_to_zero(void) {
    CapturedHandshake hs;
    hs.reset();
    
    uint8_t zeroBssid[6] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_MEMORY(zeroBssid, hs.apBssid, 6);
    TEST_ASSERT_EQUAL_MEMORY(zeroBssid, hs.clientMac, 6);
}

void test_captured_handshake_set_ssid(void) {
    CapturedHandshake hs;
    hs.reset();
    strncpy(hs.ssid, "TestNetwork", sizeof(hs.ssid) - 1);
    hs.ssid[sizeof(hs.ssid) - 1] = '\0';
    
    TEST_ASSERT_EQUAL_STRING("TestNetwork", hs.ssid);
}

void test_captured_handshake_set_channel(void) {
    CapturedHandshake hs;
    hs.reset();
    hs.channel = 6;
    
    TEST_ASSERT_EQUAL(6, hs.channel);
}

void test_captured_handshake_set_message_flags(void) {
    CapturedHandshake hs;
    hs.reset();
    
    hs.hasMsg1 = true;
    TEST_ASSERT_TRUE(hs.hasMsg1);
    TEST_ASSERT_FALSE(hs.hasMsg2);
    
    hs.hasMsg2 = true;
    TEST_ASSERT_TRUE(hs.hasMsg1);
    TEST_ASSERT_TRUE(hs.hasMsg2);
    TEST_ASSERT_FALSE(hs.hasMsg3);
    
    hs.hasMsg3 = true;
    TEST_ASSERT_TRUE(hs.hasMsg3);
    TEST_ASSERT_FALSE(hs.hasMsg4);
    
    hs.hasMsg4 = true;
    TEST_ASSERT_TRUE(hs.hasMsg4);
}

void test_captured_handshake_is_valid(void) {
    CapturedHandshake hs;
    hs.reset();
    
    TEST_ASSERT_FALSE(hs.isValid());
    
    hs.hasMsg1 = true;
    TEST_ASSERT_FALSE(hs.isValid());
    
    hs.hasMsg2 = true;
    TEST_ASSERT_FALSE(hs.isValid());
    
    hs.hasMsg3 = true;
    TEST_ASSERT_FALSE(hs.isValid());
    
    hs.hasMsg4 = true;
    TEST_ASSERT_TRUE(hs.isValid());
}

void test_captured_handshake_is_minimum_valid(void) {
    CapturedHandshake hs;
    hs.reset();
    
    // Neither MSG2 nor any of MSG1/MSG3
    TEST_ASSERT_FALSE(hs.isMinimumValid());
    
    // Only MSG1 - not enough (need MSG2)
    hs.hasMsg1 = true;
    TEST_ASSERT_FALSE(hs.isMinimumValid());
    
    // MSG1 + MSG2 = minimum valid
    hs.hasMsg2 = true;
    TEST_ASSERT_TRUE(hs.isMinimumValid());
    
    // Reset and try MSG2 + MSG3
    hs.reset();
    hs.hasMsg2 = true;
    hs.hasMsg3 = true;
    TEST_ASSERT_TRUE(hs.isMinimumValid());
}

// ==================== HandshakeCaptureConfig Tests ====================

void test_config_default_constructor(void) {
    HandshakeCaptureConfig config;
    
    TEST_ASSERT_EQUAL(1, config.channel);
    TEST_ASSERT_TRUE(config.autoDeauth);
    TEST_ASSERT_EQUAL(5, config.deauthCount);
    TEST_ASSERT_EQUAL(500, config.deauthInterval);
    TEST_ASSERT_EQUAL(60000, config.timeoutMs);
    TEST_ASSERT_TRUE(config.captureAllClients);
}

void test_config_set_values(void) {
    HandshakeCaptureConfig config;
    
    config.channel = 11;
    config.autoDeauth = false;
    config.deauthCount = 10;
    config.deauthInterval = 1000;
    config.timeoutMs = 30000;
    
    TEST_ASSERT_EQUAL(11, config.channel);
    TEST_ASSERT_FALSE(config.autoDeauth);
    TEST_ASSERT_EQUAL(10, config.deauthCount);
    TEST_ASSERT_EQUAL(1000, config.deauthInterval);
    TEST_ASSERT_EQUAL(30000, config.timeoutMs);
}

void test_config_has_valid_target_empty(void) {
    HandshakeCaptureConfig config;
    TEST_ASSERT_FALSE(config.hasValidTarget());
}

void test_config_has_valid_target_set(void) {
    HandshakeCaptureConfig config;
    
    config.targetBssid[0] = 0xAA;
    config.targetBssid[1] = 0xBB;
    config.targetBssid[2] = 0xCC;
    config.targetBssid[3] = 0xDD;
    config.targetBssid[4] = 0xEE;
    config.targetBssid[5] = 0xFF;
    
    TEST_ASSERT_TRUE(config.hasValidTarget());
}

void test_config_set_target_ssid(void) {
    HandshakeCaptureConfig config;
    
    strncpy(config.targetSsid, "MyNetwork", sizeof(config.targetSsid) - 1);
    config.targetSsid[sizeof(config.targetSsid) - 1] = '\0';
    
    TEST_ASSERT_EQUAL_STRING("MyNetwork", config.targetSsid);
}

// ==================== HandshakeStats Tests ====================

void test_stats_default_values(void) {
    HandshakeStats stats;
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.eapolFrames);
    TEST_ASSERT_EQUAL(0, stats.msg1Count);
    TEST_ASSERT_EQUAL(0, stats.msg2Count);
    TEST_ASSERT_EQUAL(0, stats.msg3Count);
    TEST_ASSERT_EQUAL(0, stats.msg4Count);
    TEST_ASSERT_EQUAL(0, stats.deauthsSent);
}

void test_stats_increment(void) {
    HandshakeStats stats;
    stats.reset();
    
    stats.eapolFrames = 100;
    stats.msg1Count = 5;
    stats.msg2Count = 4;
    stats.msg3Count = 3;
    stats.msg4Count = 2;
    stats.deauthsSent = 10;
    
    TEST_ASSERT_EQUAL(100, stats.eapolFrames);
    TEST_ASSERT_EQUAL(5, stats.msg1Count);
    TEST_ASSERT_EQUAL(4, stats.msg2Count);
    TEST_ASSERT_EQUAL(3, stats.msg3Count);
    TEST_ASSERT_EQUAL(2, stats.msg4Count);
    TEST_ASSERT_EQUAL(10, stats.deauthsSent);
}

void test_stats_reset(void) {
    HandshakeStats stats;
    stats.eapolFrames = 100;
    stats.msg1Count = 5;
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.eapolFrames);
    TEST_ASSERT_EQUAL(0, stats.msg1Count);
}

// ==================== EAPOLKeyInfo Tests ====================

void test_eapol_key_info_default(void) {
    EAPOLKeyInfo info;
    memset(&info, 0, sizeof(info));
    
    TEST_ASSERT_FALSE(info.pairwise);
    TEST_ASSERT_FALSE(info.install);
    TEST_ASSERT_FALSE(info.keyAck);
    TEST_ASSERT_FALSE(info.keyMIC);
    TEST_ASSERT_FALSE(info.secure);
    TEST_ASSERT_FALSE(info.error);
    TEST_ASSERT_FALSE(info.request);
    TEST_ASSERT_FALSE(info.encrypted);
}

void test_eapol_key_info_set_values(void) {
    EAPOLKeyInfo info;
    memset(&info, 0, sizeof(info));
    
    info.pairwise = true;
    info.keyAck = true;
    info.keyMIC = false;
    
    TEST_ASSERT_TRUE(info.pairwise);
    TEST_ASSERT_TRUE(info.keyAck);
    TEST_ASSERT_FALSE(info.keyMIC);
}

// ==================== Key Info Bit Parsing Tests ====================

// M1: ACK=1, MIC=0, Install=0, Secure=0
// M2: ACK=0, MIC=1, Install=0, Secure=0
// M3: ACK=1, MIC=1, Install=1, Secure=1
// M4: ACK=0, MIC=1, Install=0, Secure=1

void test_identify_m1_key_info(void) {
    // M1 characteristics: ACK bit set, MIC bit clear
    // Key info bits: 0x008A (typical M1)
    uint16_t keyInfo = 0x008A;
    
    bool hasAck = (keyInfo & 0x0080) != 0;
    bool hasMic = (keyInfo & 0x0100) != 0;
    bool hasInstall = (keyInfo & 0x0040) != 0;
    bool isSecure = (keyInfo & 0x0200) != 0;
    
    TEST_ASSERT_TRUE(hasAck);
    TEST_ASSERT_FALSE(hasMic);
    TEST_ASSERT_FALSE(hasInstall);
    TEST_ASSERT_FALSE(isSecure);
}

void test_identify_m2_key_info(void) {
    // M2 characteristics: MIC bit set, ACK bit clear
    // Key info bits: 0x010A (typical M2)
    uint16_t keyInfo = 0x010A;
    
    bool hasAck = (keyInfo & 0x0080) != 0;
    bool hasMic = (keyInfo & 0x0100) != 0;
    bool hasInstall = (keyInfo & 0x0040) != 0;
    bool isSecure = (keyInfo & 0x0200) != 0;
    
    TEST_ASSERT_FALSE(hasAck);
    TEST_ASSERT_TRUE(hasMic);
    TEST_ASSERT_FALSE(hasInstall);
    TEST_ASSERT_FALSE(isSecure);
}

void test_identify_m3_key_info(void) {
    // M3 characteristics: ACK, MIC, Install, Secure all set
    // Key info bits: 0x13CA (typical M3)
    uint16_t keyInfo = 0x13CA;
    
    bool hasAck = (keyInfo & 0x0080) != 0;
    bool hasMic = (keyInfo & 0x0100) != 0;
    bool hasInstall = (keyInfo & 0x0040) != 0;
    bool isSecure = (keyInfo & 0x0200) != 0;
    
    TEST_ASSERT_TRUE(hasAck);
    TEST_ASSERT_TRUE(hasMic);
    TEST_ASSERT_TRUE(hasInstall);
    TEST_ASSERT_TRUE(isSecure);
}

void test_identify_m4_key_info(void) {
    // M4 characteristics: MIC and Secure set, ACK clear
    // Key info bits: 0x030A (typical M4)
    uint16_t keyInfo = 0x030A;
    
    bool hasAck = (keyInfo & 0x0080) != 0;
    bool hasMic = (keyInfo & 0x0100) != 0;
    bool hasInstall = (keyInfo & 0x0040) != 0;
    bool isSecure = (keyInfo & 0x0200) != 0;
    
    TEST_ASSERT_FALSE(hasAck);
    TEST_ASSERT_TRUE(hasMic);
    TEST_ASSERT_FALSE(hasInstall);
    TEST_ASSERT_TRUE(isSecure);
}

// ==================== Handshake Validation Tests ====================

void test_handshake_complete_requires_all_messages(void) {
    CapturedHandshake hs;
    hs.reset();
    
    // Not complete without any messages
    bool complete = hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4;
    TEST_ASSERT_FALSE(complete);
    
    // Not complete with only MSG1
    hs.hasMsg1 = true;
    complete = hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4;
    TEST_ASSERT_FALSE(complete);
    
    // Not complete with MSG1, MSG2
    hs.hasMsg2 = true;
    complete = hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4;
    TEST_ASSERT_FALSE(complete);
    
    // Not complete with MSG1, MSG2, MSG3
    hs.hasMsg3 = true;
    complete = hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4;
    TEST_ASSERT_FALSE(complete);
    
    // Complete with all four
    hs.hasMsg4 = true;
    complete = hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3 && hs.hasMsg4;
    TEST_ASSERT_TRUE(complete);
}

void test_partial_handshake_m1_m2_usable(void) {
    // M1 + M2 is often enough for cracking with hashcat
    CapturedHandshake hs;
    hs.reset();
    hs.hasMsg1 = true;
    hs.hasMsg2 = true;
    
    bool partialUsable = hs.hasMsg1 && hs.hasMsg2;
    TEST_ASSERT_TRUE(partialUsable);
}

// ==================== HandshakeMessage Tests ====================

void test_handshake_message_unknown_value(void) {
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(HandshakeMessage::UNKNOWN));
}

void test_handshake_message_msg1_value(void) {
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(HandshakeMessage::MSG1));
}

void test_handshake_message_msg2_value(void) {
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(HandshakeMessage::MSG2));
}

void test_handshake_message_msg3_value(void) {
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(HandshakeMessage::MSG3));
}

void test_handshake_message_msg4_value(void) {
    TEST_ASSERT_EQUAL(4, static_cast<uint8_t>(HandshakeMessage::MSG4));
}

// ==================== Auto Hunt Mode Tests ====================
// Tests for network attempt tracking used in Auto Hunt feature

// Simulated attempt tracking (mirrors HandshakeScreen logic)
static const size_t MAX_ATTEMPTED = 32;

struct AttemptTracker {
    uint8_t attemptedBssids[MAX_ATTEMPTED][6];
    size_t attemptedCount = 0;
    
    void reset() {
        attemptedCount = 0;
        memset(attemptedBssids, 0, sizeof(attemptedBssids));
    }
    
    bool wasAttempted(const uint8_t* bssid) const {
        for (size_t i = 0; i < attemptedCount; i++) {
            if (memcmp(attemptedBssids[i], bssid, 6) == 0) {
                return true;
            }
        }
        return false;
    }
    
    void markAttempted(const uint8_t* bssid) {
        if (attemptedCount < MAX_ATTEMPTED) {
            memcpy(attemptedBssids[attemptedCount], bssid, 6);
            attemptedCount++;
        }
    }
};

void test_auto_hunt_attempt_tracker_starts_empty(void) {
    AttemptTracker tracker;
    tracker.reset();
    
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_FALSE(tracker.wasAttempted(bssid));
    TEST_ASSERT_EQUAL(0, tracker.attemptedCount);
}

void test_auto_hunt_mark_attempted(void) {
    AttemptTracker tracker;
    tracker.reset();
    
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    tracker.markAttempted(bssid);
    
    TEST_ASSERT_TRUE(tracker.wasAttempted(bssid));
    TEST_ASSERT_EQUAL(1, tracker.attemptedCount);
}

void test_auto_hunt_multiple_networks(void) {
    AttemptTracker tracker;
    tracker.reset();
    
    uint8_t bssid1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t bssid2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t bssid3[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    
    tracker.markAttempted(bssid1);
    tracker.markAttempted(bssid2);
    
    TEST_ASSERT_TRUE(tracker.wasAttempted(bssid1));
    TEST_ASSERT_TRUE(tracker.wasAttempted(bssid2));
    TEST_ASSERT_FALSE(tracker.wasAttempted(bssid3));
    TEST_ASSERT_EQUAL(2, tracker.attemptedCount);
}

void test_auto_hunt_reset_clears_attempts(void) {
    AttemptTracker tracker;
    tracker.reset();
    
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    tracker.markAttempted(bssid);
    TEST_ASSERT_TRUE(tracker.wasAttempted(bssid));
    
    tracker.reset();
    TEST_ASSERT_FALSE(tracker.wasAttempted(bssid));
    TEST_ASSERT_EQUAL(0, tracker.attemptedCount);
}

void test_auto_hunt_max_capacity(void) {
    AttemptTracker tracker;
    tracker.reset();
    
    // Fill to capacity
    for (size_t i = 0; i < MAX_ATTEMPTED; i++) {
        uint8_t bssid[6] = {0, 0, 0, 0, 0, (uint8_t)i};
        tracker.markAttempted(bssid);
    }
    
    TEST_ASSERT_EQUAL(MAX_ATTEMPTED, tracker.attemptedCount);
    
    // Try to add one more (should not increase count)
    uint8_t extraBssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    tracker.markAttempted(extraBssid);
    TEST_ASSERT_EQUAL(MAX_ATTEMPTED, tracker.attemptedCount);
}

// ==================== Test Runner ====================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // State enum tests
    RUN_TEST(test_state_idle_value);
    RUN_TEST(test_state_waiting_value);
    RUN_TEST(test_state_got_msg1_value);
    RUN_TEST(test_state_got_msg2_value);
    RUN_TEST(test_state_got_msg3_value);
    RUN_TEST(test_state_crackable_value);
    RUN_TEST(test_state_complete_value);
    RUN_TEST(test_state_timeout_value);
    RUN_TEST(test_state_error_value);
    
    // CapturedHandshake struct tests
    RUN_TEST(test_captured_handshake_default_constructor);
    RUN_TEST(test_captured_handshake_bssids_initialized_to_zero);
    RUN_TEST(test_captured_handshake_set_ssid);
    RUN_TEST(test_captured_handshake_set_channel);
    RUN_TEST(test_captured_handshake_set_message_flags);
    RUN_TEST(test_captured_handshake_is_valid);
    RUN_TEST(test_captured_handshake_is_minimum_valid);
    
    // Config tests
    RUN_TEST(test_config_default_constructor);
    RUN_TEST(test_config_set_values);
    RUN_TEST(test_config_has_valid_target_empty);
    RUN_TEST(test_config_has_valid_target_set);
    RUN_TEST(test_config_set_target_ssid);
    
    // Stats tests
    RUN_TEST(test_stats_default_values);
    RUN_TEST(test_stats_increment);
    RUN_TEST(test_stats_reset);
    
    // EAPOL key info tests
    RUN_TEST(test_eapol_key_info_default);
    RUN_TEST(test_eapol_key_info_set_values);
    
    // Key info parsing tests
    RUN_TEST(test_identify_m1_key_info);
    RUN_TEST(test_identify_m2_key_info);
    RUN_TEST(test_identify_m3_key_info);
    RUN_TEST(test_identify_m4_key_info);
    
    // Handshake validation tests
    RUN_TEST(test_handshake_complete_requires_all_messages);
    RUN_TEST(test_partial_handshake_m1_m2_usable);
    
    // HandshakeMessage tests
    RUN_TEST(test_handshake_message_unknown_value);
    RUN_TEST(test_handshake_message_msg1_value);
    RUN_TEST(test_handshake_message_msg2_value);
    RUN_TEST(test_handshake_message_msg3_value);
    RUN_TEST(test_handshake_message_msg4_value);
    
    // Auto Hunt mode tests
    RUN_TEST(test_auto_hunt_attempt_tracker_starts_empty);
    RUN_TEST(test_auto_hunt_mark_attempted);
    RUN_TEST(test_auto_hunt_multiple_networks);
    RUN_TEST(test_auto_hunt_reset_clears_attempts);
    RUN_TEST(test_auto_hunt_max_capacity);
    
    return UNITY_END();
}

