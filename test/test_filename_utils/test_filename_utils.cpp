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

// Return a pointer to the basename stem (after last '/', before last '.') copied into `out`.
static void stemOf(const char* path, char* out, size_t outLen) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n >= outLen) n = outLen - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

// ADR-0141: the canonical .22000 path is sanitized and carries NO per-record suffix.
// A clean SSID must map to exactly "<dir>/<ssid>.22000" (the bug was a "_{bssid}" suffix
// on the PMKID producer that the pwncrack sync never reconstructed, so it never uploaded).
void test_hc22000_path_no_suffix() {
    char path[80];
    getHc22000Path("Home", path, sizeof(path));
    const char* base = strrchr(path, '/');
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_EQUAL_STRING("/Home.22000", base);  // no "_ABCDEF", no sanitizing of a clean name
}

void test_hc22000_filename() {
    char name[48];
    getHc22000Filename("Home", name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("Home.22000", name);
}

// The invariant the three .22000 producers (4-way save, PMKID save, captures sync) depend on:
// the .22000 stem is byte-identical to the .pcap stem for the same SSID, including sanitization.
void test_hc22000_stem_matches_pcap_stem() {
    const char* ssids[] = {"Home", "My:SSID?", "  spaced  ", "a/b\\c*d"};
    for (unsigned i = 0; i < sizeof(ssids) / sizeof(ssids[0]); ++i) {
        char pcapPath[80], hcPath[80], pcapStem[48], hcStem[48];
        getHandshakePath(ssids[i], pcapPath, sizeof(pcapPath));
        getHc22000Path(ssids[i], hcPath, sizeof(hcPath));
        stemOf(pcapPath, pcapStem, sizeof(pcapStem));
        stemOf(hcPath, hcStem, sizeof(hcStem));
        TEST_ASSERT_EQUAL_STRING(pcapStem, hcStem);
    }
}

void test_hc22000_path_hidden_ssid() {
    char path[80];
    getHc22000Path("???", path, sizeof(path));
    const char* base = strrchr(path, '/');
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_EQUAL_STRING("/hidden.22000", base);
}

void test_hc22000_path_rejects_small_buffer() {
    char path[8] = "SENTIN";  // deliberately < 64: builder must refuse, not overflow
    getHc22000Path("Home", path, sizeof(path));
    TEST_ASSERT_EQUAL_STRING("", path);
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
    RUN_TEST(test_hc22000_path_no_suffix);
    RUN_TEST(test_hc22000_filename);
    RUN_TEST(test_hc22000_stem_matches_pcap_stem);
    RUN_TEST(test_hc22000_path_hidden_ssid);
    RUN_TEST(test_hc22000_path_rejects_small_buffer);
    return UNITY_END();
}
