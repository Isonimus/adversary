/**
 * @file test_settings_manager.cpp
 * @brief Unit tests for SettingsManager, WhitelistEntry, and WiFiCredential
 */

#include <unity.h>
#ifndef ESP32
#include "../common/arduino_mocks.h"
#endif
#include "modules/storage/settings_manager.h"
#include <vector>
#include <cstring>

using namespace adversary;

void setUp(void) {
    SettingsManager::getInstance().reset();
    SettingsManager::getInstance().clearWiFiCredentials();
    SettingsManager::getInstance().clearWhitelist();
}

void tearDown(void) {
}

// =============================================================================
// Dirty Flag Tests
// =============================================================================

void test_dirty_flag_logic(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    // reset() marks as dirty
    TEST_ASSERT_TRUE(settings.isDirty());
    
    // Manual mark
    settings.reset(); // Still dirty
    settings.save();  // In native this just sets dirty=false
    TEST_ASSERT_FALSE(settings.isDirty());
    
    settings.markDirty();
    TEST_ASSERT_TRUE(settings.isDirty());
}

// =============================================================================
// WhitelistEntry Logic Tests
// =============================================================================

void test_whitelist_match_bssid_only(void) {
    WhitelistEntry entry;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(entry.bssid, bssid, 6);
    entry.hasBssid = true;
    entry.hasSsid = false;
    
    uint8_t matchBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t wrongBssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    TEST_ASSERT_TRUE(entry.matches(matchBssid, "Anything"));
    TEST_ASSERT_FALSE(entry.matches(wrongBssid, "Anything"));
}

void test_whitelist_match_ssid_only(void) {
    WhitelistEntry entry;
    strncpy(entry.ssid, "TargetSSID", 32);
    entry.hasSsid = true;
    entry.hasBssid = false;
    
    uint8_t dummyBssid[6] = {0};
    
    TEST_ASSERT_TRUE(entry.matches(dummyBssid, "TargetSSID"));
    TEST_ASSERT_FALSE(entry.matches(dummyBssid, "WrongSSID"));
}

void test_whitelist_match_both(void) {
    WhitelistEntry entry;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(entry.bssid, bssid, 6);
    strncpy(entry.ssid, "TargetSSID", 32);
    entry.hasBssid = true;
    entry.hasSsid = true;
    
    uint8_t matchBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t wrongBssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    
    TEST_ASSERT_TRUE(entry.matches(matchBssid, "TargetSSID"));
    TEST_ASSERT_FALSE(entry.matches(wrongBssid, "TargetSSID")); // BSSID wrong
    TEST_ASSERT_FALSE(entry.matches(matchBssid, "WrongSSID"));  // SSID wrong
}

void test_whitelist_no_match(void) {
    WhitelistEntry entry;
    entry.hasBssid = false;
    entry.hasSsid = false;
    
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_FALSE(entry.matches(bssid, "SSID"));
}

// =============================================================================
// SettingsManager Whitelist Management
// =============================================================================

void test_whitelist_management(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    TEST_ASSERT_EQUAL(0, settings.getWhitelist().size());
    
    WhitelistEntry e1;
    strncpy(e1.ssid, "Whitelisted1", 32);
    e1.hasSsid = true;
    
    settings.addToWhitelist(e1);
    TEST_ASSERT_EQUAL(1, settings.getWhitelist().size());
    
    uint8_t dummyBssid[6] = {0};
    TEST_ASSERT_TRUE(settings.isWhitelisted(dummyBssid, "Whitelisted1"));
    TEST_ASSERT_FALSE(settings.isWhitelisted(dummyBssid, "Other"));
    
    settings.removeFromWhitelist(0);
    TEST_ASSERT_EQUAL(0, settings.getWhitelist().size());
    TEST_ASSERT_FALSE(settings.isWhitelisted(dummyBssid, "Whitelisted1"));
}

void test_whitelist_clear(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    WhitelistEntry e;
    e.hasSsid = true;
    strncpy(e.ssid, "Test", 32);
    
    settings.addToWhitelist(e);
    settings.addToWhitelist(e);
    TEST_ASSERT_EQUAL(2, settings.getWhitelist().size());
    
    settings.clearWhitelist();
    TEST_ASSERT_EQUAL(0, settings.getWhitelist().size());
}

// =============================================================================
// WiFi Credential Management
// =============================================================================

void test_add_remove_credentials(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    TEST_ASSERT_EQUAL(0, settings.getSavedCredentials().size());
    
    settings.addWiFiCredential("Network1", "pass1");
    TEST_ASSERT_EQUAL(1, settings.getSavedCredentials().size());
    TEST_ASSERT_EQUAL_STRING("Network1", settings.getSavedCredentials()[0].ssid);
    TEST_ASSERT_EQUAL_STRING("pass1", settings.getSavedCredentials()[0].password);
    
    settings.addWiFiCredential("Network2", "pass2");
    TEST_ASSERT_EQUAL(2, settings.getSavedCredentials().size());
    
    // Duplicate SSID updates password
    settings.addWiFiCredential("Network1", "newpass1");
    TEST_ASSERT_EQUAL(2, settings.getSavedCredentials().size());
    TEST_ASSERT_EQUAL_STRING("newpass1", settings.getPasswordForSSID("Network1"));
    
    settings.removeWiFiCredential(0);
    TEST_ASSERT_EQUAL(1, settings.getSavedCredentials().size());
    TEST_ASSERT_EQUAL_STRING("Network2", settings.getSavedCredentials()[0].ssid);
    
    settings.clearWiFiCredentials();
    TEST_ASSERT_EQUAL(0, settings.getSavedCredentials().size());
}

void test_password_lookup_edge_cases(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    settings.addWiFiCredential("Home", "home123");
    
    TEST_ASSERT_EQUAL_STRING("home123", settings.getPasswordForSSID("Home"));
    TEST_ASSERT_NULL(settings.getPasswordForSSID("Starbucks"));
    TEST_ASSERT_NULL(settings.getPasswordForSSID(nullptr));
    
    TEST_ASSERT_TRUE(settings.hasCredentialForSSID("Home"));
    TEST_ASSERT_FALSE(settings.hasCredentialForSSID("CoffeeShop"));
}

// =============================================================================
// API Key Tests
// =============================================================================

void test_api_key_management(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    TEST_ASSERT_FALSE(settings.hasWpaSecKey());
    
    settings.setWpaSecKey("0123456789abcdef0123456789abcdef");
    TEST_ASSERT_TRUE(settings.hasWpaSecKey());
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", settings.getWpaSecKey());
    
    settings.setWpaSecKey(nullptr);
    TEST_ASSERT_FALSE(settings.hasWpaSecKey());
    TEST_ASSERT_EQUAL_STRING("", settings.getWpaSecKey());
}

void test_wigle_key_management(void) {
    SettingsManager& settings = SettingsManager::getInstance();
    
    TEST_ASSERT_FALSE(settings.hasWigleKey());
    
    settings.setWigleKey("YXBpX3VzZXI6YXBpX3Rva2Vu"); // api_user:api_token
    TEST_ASSERT_TRUE(settings.hasWigleKey());
    TEST_ASSERT_EQUAL_STRING("YXBpX3VzZXI6YXBpX3Rva2Vu", settings.getWigleKey());
    
    settings.setWigleKey(nullptr);
    TEST_ASSERT_FALSE(settings.hasWigleKey());
    TEST_ASSERT_EQUAL_STRING("", settings.getWigleKey());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Dirty Flag
    RUN_TEST(test_dirty_flag_logic);
    
    // Whitelist Entry Logic
    RUN_TEST(test_whitelist_match_bssid_only);
    RUN_TEST(test_whitelist_match_ssid_only);
    RUN_TEST(test_whitelist_match_both);
    RUN_TEST(test_whitelist_no_match);
    
    // SettingsManager Whitelist
    RUN_TEST(test_whitelist_management);
    RUN_TEST(test_whitelist_clear);
    
    // WiFi Credentials
    RUN_TEST(test_add_remove_credentials);
    RUN_TEST(test_password_lookup_edge_cases);
    
    // API Keys
    RUN_TEST(test_api_key_management);
    RUN_TEST(test_wigle_key_management);
    
    return UNITY_END();
}
