/**
 * @file test_wifi_password.cpp
 * @brief Unit tests for the WPA2-safe WiFi password generator (slice-0036).
 */

#include <unity.h>
#include <cstring>
#include "utils/wifi_password.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// Every char produced must belong to the unambiguous alphabet (no O/0/I/1/l).
static bool inAlphabet(char c) {
    return std::strchr(WIFI_PASSWORD_ALPHABET, c) != nullptr && c != '\0';
}

// --- Success: shape of the generated key ------------------------------------

void test_produces_requested_length_and_nul() {
    char out[16];
    uint8_t entropy[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    TEST_ASSERT_TRUE(fillWifiPassword(out, sizeof(out), 12, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_size_t(12, std::strlen(out));
    TEST_ASSERT_EQUAL_CHAR('\0', out[12]);
}

void test_all_chars_in_alphabet() {
    char out[16];
    // Spread across the byte range so masking to 5 bits is exercised.
    uint8_t entropy[12] = {0, 31, 32, 63, 64, 127, 128, 200, 255, 15, 16, 17};
    TEST_ASSERT_TRUE(fillWifiPassword(out, sizeof(out), 12, entropy, sizeof(entropy)));
    for (size_t i = 0; i < 12; ++i) {
        TEST_ASSERT_TRUE(inAlphabet(out[i]));
    }
}

// --- Determinism & mapping --------------------------------------------------

void test_deterministic_same_entropy_same_output() {
    char a[16], b[16];
    uint8_t entropy[12] = {5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110};
    TEST_ASSERT_TRUE(fillWifiPassword(a, sizeof(a), 12, entropy, sizeof(entropy)));
    TEST_ASSERT_TRUE(fillWifiPassword(b, sizeof(b), 12, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_STRING(a, b);
}

void test_mapping_matches_alphabet_index() {
    // byte & 31 selects the alphabet index; 32 wraps to 0, 33 to 1, etc.
    char out[8];
    uint8_t entropy[4] = {0, 1, 32, 33};
    TEST_ASSERT_TRUE(fillWifiPassword(out, sizeof(out), 4, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_CHAR(WIFI_PASSWORD_ALPHABET[0], out[0]);
    TEST_ASSERT_EQUAL_CHAR(WIFI_PASSWORD_ALPHABET[1], out[1]);
    TEST_ASSERT_EQUAL_CHAR(WIFI_PASSWORD_ALPHABET[0], out[2]);
    TEST_ASSERT_EQUAL_CHAR(WIFI_PASSWORD_ALPHABET[1], out[3]);
}

void test_distinct_entropy_distinct_output() {
    char a[16], b[16];
    uint8_t e1[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t e2[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    TEST_ASSERT_TRUE(fillWifiPassword(a, sizeof(a), 12, e1, sizeof(e1)));
    TEST_ASSERT_TRUE(fillWifiPassword(b, sizeof(b), 12, e2, sizeof(e2)));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(a, b));
}

// --- Error paths: never emit a short/empty key silently ---------------------

void test_rejects_buffer_too_small() {
    char out[12];  // exactly charCount, no room for NUL
    uint8_t entropy[12] = {0};
    TEST_ASSERT_FALSE(fillWifiPassword(out, 12, 12, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);  // left empty, not partially filled
}

void test_rejects_insufficient_entropy() {
    char out[16];
    uint8_t entropy[8] = {0};
    TEST_ASSERT_FALSE(fillWifiPassword(out, sizeof(out), 12, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

void test_rejects_zero_charcount() {
    char out[16];
    uint8_t entropy[4] = {0};
    TEST_ASSERT_FALSE(fillWifiPassword(out, sizeof(out), 0, entropy, sizeof(entropy)));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

void test_rejects_null_pointers() {
    char out[16];
    uint8_t entropy[12] = {0};
    TEST_ASSERT_FALSE(fillWifiPassword(nullptr, sizeof(out), 12, entropy, sizeof(entropy)));
    TEST_ASSERT_FALSE(fillWifiPassword(out, sizeof(out), 12, nullptr, 12));
}

// --- Default configuration sanity -------------------------------------------

void test_default_charcount_is_wpa2_valid() {
    // The shipped key length must sit inside WPA2-PSK's 8..63 range.
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(8, kDashboardApPasswordChars);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(63, kDashboardApPasswordChars);
}

void test_alphabet_is_32_unambiguous_symbols() {
    TEST_ASSERT_EQUAL_size_t(32, std::strlen(WIFI_PASSWORD_ALPHABET));
    TEST_ASSERT_NULL(std::strchr(WIFI_PASSWORD_ALPHABET, 'O'));
    TEST_ASSERT_NULL(std::strchr(WIFI_PASSWORD_ALPHABET, '0'));
    TEST_ASSERT_NULL(std::strchr(WIFI_PASSWORD_ALPHABET, 'I'));
    TEST_ASSERT_NULL(std::strchr(WIFI_PASSWORD_ALPHABET, '1'));
    TEST_ASSERT_NULL(std::strchr(WIFI_PASSWORD_ALPHABET, 'l'));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_produces_requested_length_and_nul);
    RUN_TEST(test_all_chars_in_alphabet);
    RUN_TEST(test_deterministic_same_entropy_same_output);
    RUN_TEST(test_mapping_matches_alphabet_index);
    RUN_TEST(test_distinct_entropy_distinct_output);
    RUN_TEST(test_rejects_buffer_too_small);
    RUN_TEST(test_rejects_insufficient_entropy);
    RUN_TEST(test_rejects_zero_charcount);
    RUN_TEST(test_rejects_null_pointers);
    RUN_TEST(test_default_charcount_is_wpa2_valid);
    RUN_TEST(test_alphabet_is_32_unambiguous_symbols);
    return UNITY_END();
}
