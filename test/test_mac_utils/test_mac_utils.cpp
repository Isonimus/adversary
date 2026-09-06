/**
 * @file test_mac_utils.cpp
 * @brief Unit tests for MAC address utilities
 */

#include <unity.h>
#include <cstring>
#include "utils/mac_utils.h"

using namespace adversary::utils;

void setUp() {
    // Runs before each test
}

void tearDown() {
    // Runs after each test
}

// ===========================================
// MacAddress structure tests
// ===========================================

void test_MacAddress_isZero_withZeroMac() {
    MacAddress mac = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    TEST_ASSERT_TRUE(mac.isZero());
}

void test_MacAddress_isZero_withNonZeroMac() {
    MacAddress mac = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
    TEST_ASSERT_FALSE(mac.isZero());
}

void test_MacAddress_isBroadcast_withBroadcast() {
    MacAddress mac = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    TEST_ASSERT_TRUE(mac.isBroadcast());
}

void test_MacAddress_isBroadcast_withNonBroadcast() {
    MacAddress mac = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE}};
    TEST_ASSERT_FALSE(mac.isBroadcast());
}

void test_MacAddress_isMulticast_withMulticast() {
    MacAddress mac = {{0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}};  // IPv4 multicast
    TEST_ASSERT_TRUE(mac.isMulticast());
}

void test_MacAddress_isMulticast_withUnicast() {
    MacAddress mac = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    TEST_ASSERT_FALSE(mac.isMulticast());
}

void test_MacAddress_isLocallyAdministered_true() {
    MacAddress mac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x00}};  // LA bit set
    TEST_ASSERT_TRUE(mac.isLocallyAdministered());
}

void test_MacAddress_isLocallyAdministered_false() {
    MacAddress mac = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};  // UA address
    TEST_ASSERT_FALSE(mac.isLocallyAdministered());
}

void test_MacAddress_equals_sameMac() {
    MacAddress mac1 = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    MacAddress mac2 = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    TEST_ASSERT_TRUE(mac1.equals(mac2));
    TEST_ASSERT_TRUE(mac1 == mac2);
}

void test_MacAddress_equals_differentMac() {
    MacAddress mac1 = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    MacAddress mac2 = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00}};
    TEST_ASSERT_FALSE(mac1.equals(mac2));
    TEST_ASSERT_TRUE(mac1 != mac2);
}

// ===========================================
// parseMacString tests
// ===========================================

void test_parseMacString_validColonSeparated() {
    MacAddress result = parseMacString("AA:BB:CC:DD:EE:FF");
    
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, result.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, result.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, result.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, result.bytes[5]);
}

void test_parseMacString_validDashSeparated() {
    MacAddress result = parseMacString("AA-BB-CC-DD-EE-FF");
    
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, result.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, result.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, result.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, result.bytes[5]);
}

void test_parseMacString_validLowercase() {
    MacAddress result = parseMacString("aa:bb:cc:dd:ee:ff");
    
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, result.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, result.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, result.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, result.bytes[5]);
}

void test_parseMacString_validCompact() {
    MacAddress result = parseMacString("AABBCCDDEEFF");
    
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, result.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, result.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, result.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, result.bytes[5]);
}

void test_parseMacString_invalidTooShort() {
    MacAddress result = parseMacString("AA:BB:CC");
    TEST_ASSERT_TRUE(result.isZero());
}

void test_parseMacString_invalidCharacters() {
    MacAddress result = parseMacString("GG:HH:II:JJ:KK:LL");
    TEST_ASSERT_TRUE(result.isZero());
}

void test_parseMacString_nullInput() {
    MacAddress result = parseMacString(nullptr);
    TEST_ASSERT_TRUE(result.isZero());
}

void test_parseMacString_emptyString() {
    MacAddress result = parseMacString("");
    TEST_ASSERT_TRUE(result.isZero());
}

// ===========================================
// formatMacAddress tests
// ===========================================

void test_formatMacAddress_uppercase() {
    MacAddress mac = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    char buffer[18];
    
    formatMacAddress(mac, buffer, sizeof(buffer), true, ':');
    
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", buffer);
}

void test_formatMacAddress_lowercase() {
    MacAddress mac = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    char buffer[18];
    
    formatMacAddress(mac, buffer, sizeof(buffer), false, ':');
    
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", buffer);
}

void test_formatMacAddress_dashSeparator() {
    MacAddress mac = {{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}};
    char buffer[18];
    
    formatMacAddress(mac, buffer, sizeof(buffer), true, '-');
    
    TEST_ASSERT_EQUAL_STRING("01-02-03-04-05-06", buffer);
}

// ===========================================
// OUI functions tests
// ===========================================

void test_getOui_extractsCorrectly() {
    MacAddress mac = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    uint8_t oui[3];
    
    getOui(mac, oui);
    
    TEST_ASSERT_EQUAL_UINT8(0x00, oui[0]);
    TEST_ASSERT_EQUAL_UINT8(0x11, oui[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22, oui[2]);
}

void test_ouiMatches_sameOui() {
    MacAddress mac1 = {{0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC}};
    MacAddress mac2 = {{0x00, 0x11, 0x22, 0xDD, 0xEE, 0xFF}};
    
    TEST_ASSERT_TRUE(ouiMatches(mac1, mac2));
}

void test_ouiMatches_differentOui() {
    MacAddress mac1 = {{0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC}};
    MacAddress mac2 = {{0x00, 0x11, 0x33, 0xAA, 0xBB, 0xCC}};
    
    TEST_ASSERT_FALSE(ouiMatches(mac1, mac2));
}

// ===========================================
// generateRandomMac tests
// ===========================================

void test_generateRandomMac_locallyAdministered() {
    MacAddress mac = generateRandomMac(true);
    
    TEST_ASSERT_TRUE(mac.isLocallyAdministered());
    TEST_ASSERT_FALSE(mac.isMulticast());  // Should be unicast
}

void test_generateRandomMac_universallyAdministered() {
    MacAddress mac = generateRandomMac(false);
    
    TEST_ASSERT_FALSE(mac.isLocallyAdministered());
    TEST_ASSERT_FALSE(mac.isMulticast());
}

// ===========================================
// macFromBytes tests
// ===========================================

void test_macFromBytes_validInput() {
    uint8_t bytes[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    MacAddress mac = macFromBytes(bytes);
    
    TEST_ASSERT_EQUAL_UINT8(0x11, mac.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, mac.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0x33, mac.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x44, mac.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0x55, mac.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0x66, mac.bytes[5]);
}

void test_macFromBytes_nullInput() {
    MacAddress mac = macFromBytes(nullptr);
    TEST_ASSERT_TRUE(mac.isZero());
}

// ===========================================
// macMatchesPattern tests
// ===========================================

void test_macMatchesPattern_exactMatch() {
    MacAddress mac = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    MacAddress pattern = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    
    TEST_ASSERT_TRUE(macMatchesPattern(mac, pattern));
}

void test_macMatchesPattern_wildcardMatch() {
    MacAddress mac = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    MacAddress pattern = {{0xAA, 0xBB, 0xCC, 0xFF, 0xFF, 0xFF}};  // Last 3 bytes wildcard
    
    TEST_ASSERT_TRUE(macMatchesPattern(mac, pattern));
}

void test_macMatchesPattern_noMatch() {
    MacAddress mac = {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    MacAddress pattern = {{0x11, 0x22, 0x33, 0xFF, 0xFF, 0xFF}};
    
    TEST_ASSERT_FALSE(macMatchesPattern(mac, pattern));
}

// ===========================================
// Expanded tests (Workstream 5)
// ===========================================

void test_formatMacBytes_truncation() {
    uint8_t bytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char buffer[10];  // Too small for full "AA:BB:CC:DD:EE:FF" (17 chars + null)
    
    // Should not crash and should null-terminate accurately if implementation is safe
    formatMacBytes(bytes, buffer, sizeof(buffer));
    // Implementation uses snprintf with bufferSize, so it should truncate correctly
    TEST_ASSERT_EQUAL_UINT(9, strlen(buffer));
}

void test_generateRandomMac_uniqueness() {
    MacAddress mac1 = generateRandomMac(true);
    MacAddress mac2 = generateRandomMac(true);
    
    // Highly unlikely to be equal
    TEST_ASSERT_FALSE(mac1 == mac2);
}

void test_parseMacString_mixed_separators() {
    // Some parsers allow mixed separators, let's see if ours does
    MacAddress result = parseMacString("AA:BB-CC:DD-EE:FF");
    
    // If it doesn't support mixed, it might return zero or partial. 
    // The implementation uses a simple loop or sscanf usually.
    // Let's verify current behavior.
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
}

// ===========================================
// Test Runner
// ===========================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // MacAddress structure tests
    RUN_TEST(test_MacAddress_isZero_withZeroMac);
    RUN_TEST(test_MacAddress_isZero_withNonZeroMac);
    RUN_TEST(test_MacAddress_isBroadcast_withBroadcast);
    RUN_TEST(test_MacAddress_isBroadcast_withNonBroadcast);
    RUN_TEST(test_MacAddress_isMulticast_withMulticast);
    RUN_TEST(test_MacAddress_isMulticast_withUnicast);
    RUN_TEST(test_MacAddress_isLocallyAdministered_true);
    RUN_TEST(test_MacAddress_isLocallyAdministered_false);
    RUN_TEST(test_MacAddress_equals_sameMac);
    RUN_TEST(test_MacAddress_equals_differentMac);
    
    // parseMacString tests
    RUN_TEST(test_parseMacString_validColonSeparated);
    RUN_TEST(test_parseMacString_validDashSeparated);
    RUN_TEST(test_parseMacString_validLowercase);
    RUN_TEST(test_parseMacString_validCompact);
    RUN_TEST(test_parseMacString_invalidTooShort);
    RUN_TEST(test_parseMacString_invalidCharacters);
    RUN_TEST(test_parseMacString_nullInput);
    RUN_TEST(test_parseMacString_emptyString);
    
    // formatMacAddress tests
    RUN_TEST(test_formatMacAddress_uppercase);
    RUN_TEST(test_formatMacAddress_lowercase);
    RUN_TEST(test_formatMacAddress_dashSeparator);
    
    // OUI tests
    RUN_TEST(test_getOui_extractsCorrectly);
    RUN_TEST(test_ouiMatches_sameOui);
    RUN_TEST(test_ouiMatches_differentOui);
    
    // Random MAC tests
    RUN_TEST(test_generateRandomMac_locallyAdministered);
    RUN_TEST(test_generateRandomMac_universallyAdministered);
    
    // macFromBytes tests
    RUN_TEST(test_macFromBytes_validInput);
    RUN_TEST(test_macFromBytes_nullInput);
    
    // Pattern matching tests
    RUN_TEST(test_macMatchesPattern_exactMatch);
    RUN_TEST(test_macMatchesPattern_wildcardMatch);
    RUN_TEST(test_macMatchesPattern_noMatch);
    
    // Expanded tests
    RUN_TEST(test_formatMacBytes_truncation);
    RUN_TEST(test_generateRandomMac_uniqueness);
    RUN_TEST(test_parseMacString_mixed_separators);
    
    return UNITY_END();
}
