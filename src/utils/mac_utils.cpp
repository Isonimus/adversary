/**
 * @file mac_utils.cpp
 * @brief MAC address utility functions implementation
 */

#include "mac_utils.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace adversary {
namespace utils {

// Common MAC addresses
const MacAddress MAC_BROADCAST = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
const MacAddress MAC_ZERO = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

bool MacAddress::isZero() const {
    for (int i = 0; i < 6; i++) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

bool MacAddress::isBroadcast() const {
    for (int i = 0; i < 6; i++) {
        if (bytes[i] != 0xFF) return false;
    }
    return true;
}

bool MacAddress::isMulticast() const {
    // Multicast bit is the LSB of the first byte
    return (bytes[0] & 0x01) != 0;
}

bool MacAddress::isLocallyAdministered() const {
    // Locally administered bit is the second-least significant bit of the first byte
    return (bytes[0] & 0x02) != 0;
}

bool MacAddress::equals(const MacAddress& other) const {
    return memcmp(bytes, other.bytes, 6) == 0;
}

bool MacAddress::operator==(const MacAddress& other) const {
    return equals(other);
}

bool MacAddress::operator!=(const MacAddress& other) const {
    return !equals(other);
}

/**
 * @brief Convert hex character to nibble value
 */
static int hexCharToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

MacAddress parseMacString(const char* macStr) {
    MacAddress result = MAC_ZERO;
    
    if (macStr == nullptr) {
        return result;
    }

    size_t len = strlen(macStr);
    
    // Expected format: "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF" (17 chars)
    // Or compact: "AABBCCDDEEFF" (12 chars)
    
    if (len == 17) {
        // Standard format with separators
        char sep = macStr[2];
        if (sep != ':' && sep != '-') {
            return result;  // Invalid separator
        }

        int byteIndex = 0;
        for (size_t i = 0; i < len && byteIndex < 6; i += 3) {
            int high = hexCharToNibble(macStr[i]);
            int low = hexCharToNibble(macStr[i + 1]);
            
            if (high < 0 || low < 0) {
                return MAC_ZERO;  // Invalid hex character
            }
            
            result.bytes[byteIndex++] = (high << 4) | low;
        }
    } else if (len == 12) {
        // Compact format without separators
        for (int i = 0; i < 6; i++) {
            int high = hexCharToNibble(macStr[i * 2]);
            int low = hexCharToNibble(macStr[i * 2 + 1]);
            
            if (high < 0 || low < 0) {
                return MAC_ZERO;
            }
            
            result.bytes[i] = (high << 4) | low;
        }
    }
    // Invalid length returns zero MAC

    return result;
}

void formatMacAddress(const MacAddress& mac, char* outBuffer, size_t bufferSize,
                      bool uppercase, char separator) {
    if (outBuffer == nullptr || bufferSize < 18) {
        return;
    }

    const char* format = uppercase ? "%02X%c%02X%c%02X%c%02X%c%02X%c%02X" 
                                   : "%02x%c%02x%c%02x%c%02x%c%02x%c%02x";
    
    snprintf(outBuffer, bufferSize, format,
             mac.bytes[0], separator,
             mac.bytes[1], separator,
             mac.bytes[2], separator,
             mac.bytes[3], separator,
             mac.bytes[4], separator,
             mac.bytes[5]);
}

void getOui(const MacAddress& mac, uint8_t* outOui) {
    if (outOui == nullptr) return;
    memcpy(outOui, mac.bytes, 3);
}

bool ouiMatches(const MacAddress& mac1, const MacAddress& mac2) {
    return memcmp(mac1.bytes, mac2.bytes, 3) == 0;
}

MacAddress generateRandomMac(bool locallyAdministered) {
    MacAddress result;
    
    // Generate random bytes
    for (int i = 0; i < 6; i++) {
        result.bytes[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
    
    // Clear multicast bit (unicast address)
    result.bytes[0] &= 0xFE;
    
    // Set locally administered bit if requested
    if (locallyAdministered) {
        result.bytes[0] |= 0x02;
    } else {
        result.bytes[0] &= 0xFD;
    }
    
    return result;
}

MacAddress macFromBytes(const uint8_t* bytes) {
    MacAddress result;
    if (bytes != nullptr) {
        memcpy(result.bytes, bytes, 6);
    } else {
        result = MAC_ZERO;
    }
    return result;
}

bool macMatchesPattern(const MacAddress& mac, const MacAddress& pattern) {
    for (int i = 0; i < 6; i++) {
        // 0xFF is wildcard
        if (pattern.bytes[i] != 0xFF && pattern.bytes[i] != mac.bytes[i]) {
            return false;
        }
    }
    return true;
}

} // namespace utils
} // namespace adversary
