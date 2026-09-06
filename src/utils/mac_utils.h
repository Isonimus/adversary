#pragma once

/**
 * @file mac_utils.h
 * @brief MAC address utility functions
 * 
 * Pure functions for MAC address manipulation
 */

#include <stdint.h>
#include <stdbool.h>
#include <cstddef>
#include <cstdio>

namespace adversary {
namespace utils {

/**
 * @brief MAC address structure
 */
struct MacAddress {
    uint8_t bytes[6];

    /**
     * @brief Check if MAC is all zeros
     */
    bool isZero() const;

    /**
     * @brief Check if MAC is broadcast (FF:FF:FF:FF:FF:FF)
     */
    bool isBroadcast() const;

    /**
     * @brief Check if MAC is multicast
     */
    bool isMulticast() const;

    /**
     * @brief Check if MAC is locally administered (vs universally administered)
     */
    bool isLocallyAdministered() const;

    /**
     * @brief Compare with another MAC address
     */
    bool equals(const MacAddress& other) const;

    /**
     * @brief Comparison operator
     */
    bool operator==(const MacAddress& other) const;
    bool operator!=(const MacAddress& other) const;
};

// Common MAC addresses
extern const MacAddress MAC_BROADCAST;
extern const MacAddress MAC_ZERO;

/**
 * @brief Parse MAC address from string
 * @param macStr MAC string in format "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF"
 * @return Parsed MAC address, zero MAC if invalid
 */
MacAddress parseMacString(const char* macStr);

/**
 * @brief Format MAC address to string
 * @param mac MAC address to format
 * @param outBuffer Output buffer (minimum 18 bytes)
 * @param bufferSize Size of output buffer
 * @param uppercase Use uppercase hex letters
 * @param separator Separator character (':' or '-')
 */
void formatMacAddress(const MacAddress& mac, char* outBuffer, size_t bufferSize,
                      bool uppercase = true, char separator = ':');

/**
 * @brief Format MAC from raw bytes to string (convenience function)
 * @param bytes 6-byte MAC address array
 * @param outBuffer Output buffer (minimum 18 bytes)
 * @param bufferSize Size of output buffer
 */
inline void formatMacBytes(const uint8_t* bytes, char* outBuffer, size_t bufferSize) {
    if (outBuffer != nullptr && bufferSize > 0) {
        snprintf(outBuffer, bufferSize, "%02X:%02X:%02X:%02X:%02X:%02X",
                 bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    }
}

/**
 * @brief Get OUI (first 3 bytes) from MAC
 * @param mac Source MAC address
 * @param outOui Output buffer for OUI (3 bytes)
 */
void getOui(const MacAddress& mac, uint8_t* outOui);

/**
 * @brief Compare MAC OUIs
 * @return true if OUIs match
 */
bool ouiMatches(const MacAddress& mac1, const MacAddress& mac2);

/**
 * @brief Generate random MAC address
 * @param locallyAdministered Set locally administered bit
 * @return Random MAC address
 */
MacAddress generateRandomMac(bool locallyAdministered = true);

/**
 * @brief Copy MAC address from raw bytes
 * @param bytes Source bytes (6 bytes)
 * @return MAC address structure
 */
MacAddress macFromBytes(const uint8_t* bytes);

/**
 * @brief Check if MAC matches a pattern (with wildcards)
 * Wildcard byte is 0xFF
 */
bool macMatchesPattern(const MacAddress& mac, const MacAddress& pattern);

} // namespace utils
} // namespace adversary
