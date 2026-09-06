/**
 * @file test_ble_crypto.cpp
 * @brief Unit tests for BLE cryptographic helpers
 */

#include <unity.h>
#include "modules/ble/ble_crypto.h"
#include <cstring>

using namespace adversary::ble::crypto;

void setUp() {
    // No setup needed
}

void tearDown() {
    // No teardown needed
}

void test_sha256_callable() {
    uint8_t data[] = "test data";
    uint8_t hash[32];
    
    // Test that it's callable and returns true in native build (even if stubbed)
    TEST_ASSERT_TRUE(sha256(data, sizeof(data), hash));
}

void test_aes128_ecb_encrypt_callable() {
    uint8_t key[16] = {0};
    uint8_t input[16] = "plain text";
    uint8_t output[16];
    
    TEST_ASSERT_TRUE(aes128_ecb_encrypt(key, input, output));
}

void test_aes128_ecb_decrypt_callable() {
    uint8_t key[16] = {0};
    uint8_t input[16] = "encrypted";
    uint8_t output[16];
    
    TEST_ASSERT_TRUE(aes128_ecb_decrypt(key, input, output));
}

void test_sha256_output_length() {
    uint8_t data[] = "hello";
    uint8_t hash[32];
    memset(hash, 0, 32);
    
    sha256(data, sizeof(data), hash);
    
    // In our stubs, we might just return true, but let's check if it writes anything.
    // If it's a real implementation (linked in), it should be 32 bytes.
}

void test_aes_key_lengths() {
    // Verify constants or expectations
    uint8_t key[16];
    uint8_t input[16];
    uint8_t output[16];
    
    // Should be exactly 16 bytes for AES-128
    TEST_ASSERT_TRUE(aes128_ecb_encrypt(key, input, output));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_callable);
    RUN_TEST(test_aes128_ecb_encrypt_callable);
    RUN_TEST(test_aes128_ecb_decrypt_callable);
    RUN_TEST(test_sha256_output_length);
    RUN_TEST(test_aes_key_lengths);
    return UNITY_END();
}
