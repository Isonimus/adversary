/**
 * @file ble_crypto.h
 * @brief Cryptographic helpers for BLE protocols (AES, SHA)
 */

#pragma once

#include <cstdint>
#include <vector>

namespace adversary {
namespace ble {
namespace crypto {

/**
 * @brief Compute SHA-256 hash
 * @param data Input data
 * @param len Input length
 * @param output Output buffer (must be at least 32 bytes)
 * @return true if successful
 */
bool sha256(const uint8_t* data, size_t len, uint8_t* output);

/**
 * @brief AES-128 ECB encryption
 * @param key 16-byte key
 * @param input 16-byte input block
 * @param output 16-byte output block
 * @return true if successful
 */
bool aes128_ecb_encrypt(const uint8_t* key, const uint8_t* input, uint8_t* output);

/**
 * @brief AES-128 ECB decryption
 * @param key 16-byte key
 * @param input 16-byte input block
 * @param output 16-byte output block
 * @return true if successful
 */
bool aes128_ecb_decrypt(const uint8_t* key, const uint8_t* input, uint8_t* output);

} // namespace crypto
} // namespace ble
} // namespace adversary
