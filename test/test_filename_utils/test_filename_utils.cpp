/**
 * @file test_filename_utils.cpp
 * @brief Unit tests for filename and SSID sanitization utilities
 */

#include <unity.h>
#include "utils/filename_utils.h"
#include <cstring>

using namespace adversary::filename_utils;

void setUp() {
    // No setup needed
}

void tearDown() {
}

void test_sanitize_ssid_basic() {
    char output[32];
    sanitizeSSID("TestSSID", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("TestSSID", output);
}

void test_sanitize_ssid_illegal_chars() {
    char output[32];
    // Replaces / \ : * ? " < > | with _
    // NOTE: Trailing underscores are trimmed by the implementation
    sanitizeSSID("My:SSID?", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("My_SSID", output);
}

void test_sanitize_ssid_leading_trailing_spaces() {
    char output[32];
    sanitizeSSID("  My SSID  ", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("My SSID", output);
}

void test_sanitize_ssid_hidden() {
    // Hidden SSIDs (empty or all spaces/illegal) should return "hidden"
    char output[32];
    sanitizeSSID("   ", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("hidden", output);
    
    sanitizeSSID("???", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("hidden", output);
}

void test_sanitize_ssid_max_length() {
    char output[32];
    const char* longSSID = "ThisIsAVeryLongSSIDThatExceedsTheLimitOf32Chars";
    sanitizeSSID(longSSID, output, sizeof(output));
    // sizeof(output) is 32, so max string len is 31
    TEST_ASSERT_EQUAL(31, strlen(output)); 
    TEST_ASSERT_EQUAL_STRING("ThisIsAVeryLongSSIDThatExceedsT", output);
}

void test_sanitize_ssid_edge_cases() {
    char output[32];
    
    // Multiple separators and spaces
    sanitizeSSID("My..SSID  !!", output, sizeof(output));
    // Based on implementation, dots might be kept or replaced. 
    // Usually only / \ : * ? " < > | are replaced.
    // Let's verify literal behavior.
    TEST_ASSERT_EQUAL_STRING("My..SSID  !!", output);
    
    // Leading dots (often hidden files in unix, should be fine for SSIDs)
    sanitizeSSID(".hidden_ssid", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING(".hidden_ssid", output);
}

void test_path_helpers() {
    char path[64];
    getHandshakePath("MyNetwork", path, sizeof(path));
    // Assumes SD_HANDSHAKES_PATH is defined (usually /adversary/captures)
    // Check if it contains the sanitized name and extension
    TEST_ASSERT_NOT_NULL(strstr(path, "MyNetwork.pcap"));
    
    getMetadataPath("MyNetwork", path, sizeof(path));
    TEST_ASSERT_NOT_NULL(strstr(path, "MyNetwork.json"));
}

void test_path_helpers_long_ssid() {
    char path[128];
    const char* longSSID = "WiFi_Network_With_A_Very_Very_Very_Long_Name_That_Should_Be_Truncated";
    
    getHandshakePath(longSSID, path, sizeof(path));
    // Ensure it still ends in .pcap and doesn't overflow
    TEST_ASSERT_NOT_NULL(strstr(path, ".pcap"));
    TEST_ASSERT_TRUE(strlen(path) < sizeof(path));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_ssid_basic);
    RUN_TEST(test_sanitize_ssid_illegal_chars);
    RUN_TEST(test_sanitize_ssid_leading_trailing_spaces);
    RUN_TEST(test_sanitize_ssid_hidden);
    RUN_TEST(test_sanitize_ssid_max_length);
    RUN_TEST(test_sanitize_ssid_edge_cases);
    RUN_TEST(test_path_helpers);
    RUN_TEST(test_path_helpers_long_ssid);
    return UNITY_END();
}
