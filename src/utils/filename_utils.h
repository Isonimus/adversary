/**
 * @file filename_utils.h
 * @brief SSID sanitization and path utilities for handshake files
 * 
 * Provides consistent sanitization for both saving AND lookup.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "config/config.h"

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#endif

namespace adversary::filename_utils {

static constexpr size_t MAX_FILENAME_LEN = 32;

/**
 * @brief Sanitize SSID for use as filename (FAT32 compatible)
 * 
 * Replaces: / \ : * ? " < > | and control chars with underscore
 */
inline void sanitizeSSID(const char* ssid, char* output, size_t outputLen) {
    if (!ssid || !output || outputLen == 0) {
        if (output && outputLen > 0) output[0] = '\0';
        return;
    }
    
    size_t outIdx = 0;
    size_t maxLen = (outputLen - 1 < MAX_FILENAME_LEN) ? outputLen - 1 : MAX_FILENAME_LEN;
    
    // Skip leading spaces
    size_t startIdx = 0;
    while (ssid[startIdx] == ' ') startIdx++;
    
    for (size_t i = startIdx; ssid[i] != '\0' && outIdx < maxLen; i++) {
        char c = ssid[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || 
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c < 0x20) {
            output[outIdx++] = '_';
        } else {
            output[outIdx++] = c;
        }
    }
    
    // Trim trailing spaces/underscores
    while (outIdx > 0 && (output[outIdx - 1] == ' ' || output[outIdx - 1] == '_')) {
        outIdx--;
    }
    
    // Handle empty result (hidden SSID)
    if (outIdx == 0) {
        strncpy(output, "hidden", outputLen - 1);
        output[outputLen - 1] = '\0';
        return;
    }
    
    output[outIdx] = '\0';
}

/**
 * @brief Get full path for handshake .pcap file
 */
inline void getHandshakePath(const char* ssid, char* output, size_t outputLen) {
    if (!ssid || !output || outputLen < 64) {
        if (output && outputLen > 0) output[0] = '\0';
        return;
    }
    
    char sanitized[MAX_FILENAME_LEN + 1];
    sanitizeSSID(ssid, sanitized, sizeof(sanitized));
    snprintf(output, outputLen, "%s/%s.pcap", config::SD_HANDSHAKES_PATH, sanitized);
}

/**
 * @brief Get full path for metadata .json file
 */
inline void getMetadataPath(const char* ssid, char* output, size_t outputLen) {
    if (!ssid || !output || outputLen < 64) {
        if (output && outputLen > 0) output[0] = '\0';
        return;
    }
    
    char sanitized[MAX_FILENAME_LEN + 1];
    sanitizeSSID(ssid, sanitized, sizeof(sanitized));
    snprintf(output, outputLen, "%s/%s.json", config::SD_HANDSHAKES_PATH, sanitized);
}

/**
 * @brief Get sanitized filename (without path) for an SSID
 */
inline void getHandshakeFilename(const char* ssid, char* output, size_t outputLen) {
    if (!ssid || !output || outputLen < MAX_FILENAME_LEN + 6) {
        if (output && outputLen > 0) output[0] = '\0';
        return;
    }
    
    char sanitized[MAX_FILENAME_LEN + 1];
    sanitizeSSID(ssid, sanitized, sizeof(sanitized));
    snprintf(output, outputLen, "%s.pcap", sanitized);
}

/**
 * @brief Check if a handshake already exists for this SSID
 */
inline bool handshakeExists(const char* ssid) {
#ifdef ESP32
    char path[80];
    getHandshakePath(ssid, path, sizeof(path));
    return SD.exists(path);
#else
    (void)ssid;
    return false;
#endif
}

} // namespace adversary::filename_utils
