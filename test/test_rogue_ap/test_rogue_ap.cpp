/**
 * @file test_rogue_ap.cpp
 * @brief Unit tests for Rogue AP modules (EvilTwin, KarmaAP, and shared types)
 */

#include <unity.h>
#include "modules/ap/ap_types.h"
#include "modules/ap/arduino_captive.h"
#include "modules/attack/evil_twin.h"
#include "modules/attack/karma_ap.h"

using namespace ap;
using namespace attack;

// ============================================================================
// APConfig Tests (from ap_types.h)
// ============================================================================

void test_ap_config_default_values() {
    APConfig config;
    
    TEST_ASSERT_EQUAL_STRING("", config.ssid);
    TEST_ASSERT_EQUAL_STRING("", config.password);
    TEST_ASSERT_EQUAL(1, config.channel);
    TEST_ASSERT_EQUAL(4, config.maxConnections);
    TEST_ASSERT_FALSE(config.hidden);
    TEST_ASSERT_TRUE(config.isOpen());
}

void test_ap_config_set_ssid() {
    APConfig config;
    config.setSSID("TestNetwork");
    
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.ssid);
}

void test_ap_config_set_password() {
    APConfig config;
    config.setPassword("secret123");
    
    TEST_ASSERT_EQUAL_STRING("secret123", config.password);
    TEST_ASSERT_FALSE(config.isOpen());
}

void test_ap_config_ssid_truncation() {
    APConfig config;
    const char* longSSID = "ThisIsAVeryLongSSIDThatExceedsTheMaximumAllowedLength";
    config.setSSID(longSSID);
    
    // Should be truncated to 32 chars
    TEST_ASSERT_EQUAL(32, strlen(config.ssid));
}

// ============================================================================
// APClient Tests
// ============================================================================

void test_ap_client_default_invalid() {
    APClient client;
    TEST_ASSERT_FALSE(client.isValid());
}

void test_ap_client_with_mac_is_valid() {
    APClient client;
    client.mac[0] = 0xAA;
    TEST_ASSERT_TRUE(client.isValid());
}

// ============================================================================
// CaptivePortalConfig Tests
// ============================================================================

void test_captive_portal_config_defaults() {
    CaptivePortalConfig config;
    
    TEST_ASSERT_EQUAL(PortalPageType::GENERIC_LOGIN, config.pageType);
    TEST_ASSERT_EQUAL_STRING("Network Login", config.customTitle);
    TEST_ASSERT_TRUE(config.logToSD);
}

void test_captive_portal_config_set_title() {
    CaptivePortalConfig config;
    config.setTitle("Corporate Network");
    
    TEST_ASSERT_EQUAL_STRING("Corporate Network", config.customTitle);
}

void test_captive_portal_config_set_success_message() {
    CaptivePortalConfig config;
    config.setSuccessMessage("Welcome to the network!");
    
    TEST_ASSERT_EQUAL_STRING("Welcome to the network!", config.successMessage);
}

// ============================================================================
// CapturedCredential Tests
// ============================================================================

void test_captured_credential_set_values() {
    CapturedCredential cred;
    cred.setUsername("admin");
    cred.setPassword("password123");
    cred.setClientIP("192.168.4.2");
    cred.timestamp = 12345;
    
    TEST_ASSERT_EQUAL_STRING("admin", cred.username);
    TEST_ASSERT_EQUAL_STRING("password123", cred.password);
    TEST_ASSERT_EQUAL_STRING("192.168.4.2", cred.clientIP);
    TEST_ASSERT_EQUAL(12345, cred.timestamp);
}

// ============================================================================
// ArduinoCaptivePortal Tests
// ============================================================================

void test_arduino_captive_portal_initial_state() {
    ArduinoCaptivePortal portal;
    
    TEST_ASSERT_FALSE(portal.isRunning());
    TEST_ASSERT_EQUAL(0, portal.getCredentialCount());
    TEST_ASSERT_EQUAL(0, portal.getRequestCount());
}

void test_arduino_captive_portal_start_stop() {
    ArduinoCaptivePortal portal;
    
    TEST_ASSERT_TRUE(portal.start("Test Portal"));
    TEST_ASSERT_TRUE(portal.isRunning());
    
    portal.stop();
    TEST_ASSERT_FALSE(portal.isRunning());
}

void test_arduino_captive_portal_clear_credentials() {
    ArduinoCaptivePortal portal;
    portal.start("Test Portal");
    
    // Initially empty
    TEST_ASSERT_EQUAL(0, portal.getCredentialCount());
    
    // Clear should work even when empty
    portal.clearCredentials();
    TEST_ASSERT_EQUAL(0, portal.getCredentialCount());
    
    portal.stop();
}

// ============================================================================
// EvilTwinStats Tests
// ============================================================================

void test_evil_twin_stats_default_zero() {
    EvilTwinStats stats;
    
    TEST_ASSERT_EQUAL(0, stats.clientsConnected);
    TEST_ASSERT_EQUAL(0, stats.totalConnections);
    TEST_ASSERT_EQUAL(0, stats.credentialsCaptured);
    TEST_ASSERT_EQUAL(0, stats.dnsQueries);
    TEST_ASSERT_EQUAL(0, stats.httpRequests);
    TEST_ASSERT_EQUAL(0, stats.deauthsSent);
    TEST_ASSERT_EQUAL(0, stats.startTime);
}

// ============================================================================
// EvilTwin Tests
// ============================================================================

void test_evil_twin_initial_state() {
    EvilTwin et;
    
    TEST_ASSERT_EQUAL(EvilTwinState::IDLE, et.getState());
    TEST_ASSERT_FALSE(et.isRunning());
}

void test_evil_twin_set_target() {
    EvilTwin et;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    et.setTarget("TargetNetwork", bssid, 11);
    
    TEST_ASSERT_EQUAL_STRING("TargetNetwork", et.getTargetSSID());
    TEST_ASSERT_EQUAL(11, et.getTargetChannel());
}

void test_evil_twin_deauth_settings() {
    EvilTwin et;
    
    et.setDeauthEnabled(true, 3000);
    // Just ensure no crash
    TEST_ASSERT_TRUE(true);
    
    et.setDeauthEnabled(false, 0);
    TEST_ASSERT_TRUE(true);
}

void test_evil_twin_portal_config() {
    EvilTwin et;
    CaptivePortalConfig config;
    config.setTitle("Company WiFi");
    config.pageType = PortalPageType::CORPORATE;
    
    et.setPortalConfig(config);
    // Just ensure no crash
    TEST_ASSERT_TRUE(true);
}

// ============================================================================
// ProbeRequest Tests
// ============================================================================

void test_probe_request_default_values() {
    ProbeRequest probe;
    
    TEST_ASSERT_EQUAL_STRING("", probe.ssid);
    TEST_ASSERT_EQUAL(0, probe.rssi);
    TEST_ASSERT_EQUAL(0, probe.timestamp);
    TEST_ASSERT_EQUAL(1, probe.count);
}

void test_probe_request_matches_mac() {
    ProbeRequest probe;
    probe.clientMAC[0] = 0xAA;
    probe.clientMAC[1] = 0xBB;
    probe.clientMAC[2] = 0xCC;
    probe.clientMAC[3] = 0xDD;
    probe.clientMAC[4] = 0xEE;
    probe.clientMAC[5] = 0xFF;
    
    uint8_t testMAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_TRUE(probe.matchesMAC(testMAC));
    
    uint8_t otherMAC[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    TEST_ASSERT_FALSE(probe.matchesMAC(otherMAC));
}

void test_probe_request_matches_ssid() {
    ProbeRequest probe;
    strncpy(probe.ssid, "HomeNetwork", sizeof(probe.ssid));
    
    TEST_ASSERT_TRUE(probe.matchesSSID("HomeNetwork"));
    TEST_ASSERT_FALSE(probe.matchesSSID("OtherNetwork"));
}

// ============================================================================
// KarmaStats Tests
// ============================================================================

void test_karma_stats_default_zero() {
    KarmaStats stats;
    
    TEST_ASSERT_EQUAL(0, stats.probesCaptured);
    TEST_ASSERT_EQUAL(0, stats.uniqueClients);
    TEST_ASSERT_EQUAL(0, stats.uniqueSSIDs);
    TEST_ASSERT_EQUAL(0, stats.beaconsSent);
    TEST_ASSERT_EQUAL(0, stats.clientsConnected);
    TEST_ASSERT_EQUAL(0, stats.credentialsCaptured);
    TEST_ASSERT_EQUAL(0, stats.startTime);
}

// ============================================================================
// KarmaAP Tests
// ============================================================================

void test_karma_ap_initial_state() {
    KarmaAP karma;
    
    TEST_ASSERT_EQUAL(KarmaState::IDLE, karma.getState());
    TEST_ASSERT_FALSE(karma.isRunning());
    TEST_ASSERT_TRUE(karma.getProbes().empty());
}

void test_karma_ap_start_listening() {
    KarmaAP karma;
    
    TEST_ASSERT_TRUE(karma.startListening());
    TEST_ASSERT_EQUAL(KarmaState::LISTENING, karma.getState());
    TEST_ASSERT_TRUE(karma.isRunning());
    
    karma.stop();
    TEST_ASSERT_EQUAL(KarmaState::IDLE, karma.getState());
}

void test_karma_ap_start_active() {
    KarmaAP karma;
    CaptivePortalConfig config;
    config.setTitle("Free WiFi");
    
    TEST_ASSERT_TRUE(karma.startActive(config));
    TEST_ASSERT_EQUAL(KarmaState::ACTIVE, karma.getState());
    TEST_ASSERT_TRUE(karma.isRunning());
    
    karma.stop();
    TEST_ASSERT_EQUAL(KarmaState::IDLE, karma.getState());
}

void test_karma_ap_set_limits() {
    KarmaAP karma;
    
    karma.setMaxProbes(50);
    karma.setMaxSSIDs(10);
    // Just ensure no crash
    TEST_ASSERT_TRUE(true);
}

void test_karma_ap_clear_probes() {
    KarmaAP karma;
    karma.startListening();
    
    karma.clearProbes();
    TEST_ASSERT_TRUE(karma.getProbes().empty());
    
    karma.stop();
}

void test_karma_ap_get_current_ssid() {
    KarmaAP karma;
    CaptivePortalConfig config;
    karma.startActive(config);
    
    // getCurrentSSID() should return a valid pointer (may be empty string in test stub)
    const char* ssid = karma.getCurrentSSID();
    TEST_ASSERT_NOT_NULL(ssid);
    // Note: In UNIT_TEST mode, SSID may be empty since WiFi.softAP is stubbed
    
    karma.stop();
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp(void) {
    // Reset before each test
}

void tearDown(void) {
    // Cleanup after each test
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // APConfig
    RUN_TEST(test_ap_config_default_values);
    RUN_TEST(test_ap_config_set_ssid);
    RUN_TEST(test_ap_config_set_password);
    RUN_TEST(test_ap_config_ssid_truncation);
    
    // APClient
    RUN_TEST(test_ap_client_default_invalid);
    RUN_TEST(test_ap_client_with_mac_is_valid);
    
    // CaptivePortalConfig
    RUN_TEST(test_captive_portal_config_defaults);
    RUN_TEST(test_captive_portal_config_set_title);
    RUN_TEST(test_captive_portal_config_set_success_message);
    
    // CapturedCredential
    RUN_TEST(test_captured_credential_set_values);
    
    // ArduinoCaptivePortal
    RUN_TEST(test_arduino_captive_portal_initial_state);
    RUN_TEST(test_arduino_captive_portal_start_stop);
    RUN_TEST(test_arduino_captive_portal_clear_credentials);
    
    // EvilTwinStats
    RUN_TEST(test_evil_twin_stats_default_zero);
    
    // EvilTwin
    RUN_TEST(test_evil_twin_initial_state);
    RUN_TEST(test_evil_twin_set_target);
    RUN_TEST(test_evil_twin_deauth_settings);
    RUN_TEST(test_evil_twin_portal_config);
    
    // ProbeRequest
    RUN_TEST(test_probe_request_default_values);
    RUN_TEST(test_probe_request_matches_mac);
    RUN_TEST(test_probe_request_matches_ssid);
    
    // KarmaStats
    RUN_TEST(test_karma_stats_default_zero);
    
    // KarmaAP
    RUN_TEST(test_karma_ap_initial_state);
    RUN_TEST(test_karma_ap_start_listening);
    RUN_TEST(test_karma_ap_start_active);
    RUN_TEST(test_karma_ap_set_limits);
    RUN_TEST(test_karma_ap_clear_probes);
    RUN_TEST(test_karma_ap_get_current_ssid);
    
    return UNITY_END();
}
