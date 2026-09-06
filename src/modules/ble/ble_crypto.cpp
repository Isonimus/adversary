/**
 * @file ble_crypto.cpp
 * @brief Implementation of BLE crypto helpers using mbedtls
 */

#include "ble_crypto.h"
#ifdef ESP32
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#else
#include <cstdlib>
#endif
#include <cstring>

namespace adversary {
namespace ble {
namespace crypto {

bool sha256(const uint8_t* data, size_t len, uint8_t* output) {
    if (!data || !output) return false;
    
#ifdef ESP32
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    
    int ret = mbedtls_sha256_starts(&ctx, 0); // 0 for SHA-256
    if (ret == 0) {
        ret = mbedtls_sha256_update(&ctx, data, len);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&ctx, output);
    }
    
    mbedtls_sha256_free(&ctx);
    return (ret == 0);
#else
    // Native stub: simple XOR for testing or just zero it
    memset(output, 0, 32);
    return true;
#endif
}

bool aes128_ecb_encrypt(const uint8_t* key, const uint8_t* input, uint8_t* output) {
    if (!key || !input || !output) return false;
    
#ifdef ESP32
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    
    int ret = mbedtls_aes_setkey_enc(&ctx, key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, input, output);
    }
    
    mbedtls_aes_free(&ctx);
    return (ret == 0);
#else
    // Native stub: copy input to output for testing
    memcpy(output, input, 16);
    return true;
#endif
}

bool aes128_ecb_decrypt(const uint8_t* key, const uint8_t* input, uint8_t* output) {
    if (!key || !input || !output) return false;
    
#ifdef ESP32
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    
    int ret = mbedtls_aes_setkey_dec(&ctx, key, 128);
    if (ret == 0) {
        ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, input, output);
    }
    
    mbedtls_aes_free(&ctx);
    return (ret == 0);
#else
    // Native stub: copy input to output for testing
    memcpy(output, input, 16);
    return true;
#endif
}

} // namespace crypto
} // namespace ble
} // namespace adversary
