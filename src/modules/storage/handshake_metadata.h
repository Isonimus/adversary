/**
 * @file handshake_metadata.h
 * @brief Handshake metadata storage and retrieval
 * 
 * Each captured handshake has a corresponding .json metadata file
 * containing type, timestamps, quality, and WPA-SEC status.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "../../utils/mac_utils.h"
#include "../network/wpasec_service.h"  // For WpaSecStatus enum

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#endif

namespace adversary {

/**
 * @brief Complete metadata for a captured handshake
 */
struct HandshakeMetadata {
    uint8_t version = 1;
    char ssid[33] = {0};
    uint8_t bssid[6] = {0};
    uint8_t channel = 0;
    char type[8] = "4WAY";  // "4WAY", "PMKID", or "EAPOL"
    uint32_t capturedAt = 0;  // Unix timestamp
    
    // EAPOL message flags
    bool hasMsg1 = false;
    bool hasMsg2 = false;
    bool hasMsg3 = false;
    bool hasMsg4 = false;
    bool hasPMKID = false;
    
    // Quality indicators
    uint8_t quality = 0;  // 0-100%
    int8_t signalStrength = 0;  // dBm
    
    // WPA-SEC integration
    WpaSecStatus wpaSecStatus = WpaSecStatus::NOT_UPLOADED;
    uint32_t wpaSecUploadedAt = 0;
    uint32_t wpaSecCrackedAt = 0;
    char wpaSecPassword[65] = {0};

    // pwncrack integration (parallel cracking service, .22000 upload)
    WpaSecStatus pwncrackStatus = WpaSecStatus::NOT_UPLOADED;
    uint32_t pwncrackUploadedAt = 0;
    uint32_t pwncrackCrackedAt = 0;
    char pwncrackPassword[65] = {0};
    bool has22000 = false;  ///< a "{ssid}.22000" exists (pwncrack-uploadable)

    // GPS geolocation (optional)
    bool hasGPS = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude = 0.0f;
    uint8_t satellites = 0;
    
    HandshakeMetadata() {
        memset(ssid, 0, sizeof(ssid));
        memset(bssid, 0, sizeof(bssid));
        memset(type, 0, sizeof(type));
        strcpy(type, "4WAY");
        memset(wpaSecPassword, 0, sizeof(wpaSecPassword));
    }
};

/**
 * @brief Save handshake metadata to JSON file
 * @param pcapPath Full path to the .pcap file
 * @param metadata Metadata to save
 * @return true if saved successfully
 */
inline bool saveHandshakeMetadata(const char* pcapPath, const HandshakeMetadata& metadata) {
#ifdef ESP32
    if (!pcapPath) {
        Serial.println("[Metadata] ERROR: pcapPath is null!");
        return false;
    }
    
    // Build .json path from .pcap path
    char jsonPath[128];
    strncpy(jsonPath, pcapPath, sizeof(jsonPath) - 1);
    jsonPath[127] = '\0';  // Ensure null-termination
    
    char* ext = strstr(jsonPath, ".pcap");
    if (!ext) {
        Serial.printf("[Metadata] ERROR: Path doesn't contain '.pcap': %s\n", jsonPath);
        return false;
    }
    strcpy(ext, ".json");
    
    // Create JSON document
    StaticJsonDocument<512> doc;
    doc["version"] = metadata.version;
    doc["ssid"] = metadata.ssid;
    
    // BSSID as hex string
    char bssidStr[18];
    utils::formatMacBytes(metadata.bssid, bssidStr, sizeof(bssidStr));
    doc["bssid"] = bssidStr;
    
    doc["channel"] = metadata.channel;
    doc["type"] = metadata.type;
    doc["capturedAt"] = metadata.capturedAt;
    doc["hasMsg1"] = metadata.hasMsg1;
    doc["hasMsg2"] = metadata.hasMsg2;
    doc["hasMsg3"] = metadata.hasMsg3;
    doc["hasMsg4"] = metadata.hasMsg4;
    doc["hasPMKID"] = metadata.hasPMKID;
    doc["quality"] = metadata.quality;
    doc["signalStrength"] = metadata.signalStrength;
    doc["wpaSecStatus"] = static_cast<uint8_t>(metadata.wpaSecStatus);
    doc["wpaSecUploadedAt"] = metadata.wpaSecUploadedAt;
    doc["wpaSecCrackedAt"] = metadata.wpaSecCrackedAt;
    if (metadata.wpaSecPassword[0]) {
        doc["wpaSecPassword"] = metadata.wpaSecPassword;
    }
    
    // GPS geolocation (only if available)
    if (metadata.hasGPS) {
        JsonObject gps = doc.createNestedObject("gps");
        gps["latitude"] = metadata.latitude;
        gps["longitude"] = metadata.longitude;
        gps["altitude"] = metadata.altitude;
        gps["satellites"] = metadata.satellites;
    }
    
    // Write to file
    File file = SD.open(jsonPath, FILE_WRITE);
    if (!file) {
        Serial.printf("[Metadata] Failed to create: %s\n", jsonPath);
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.printf("[Metadata] Saved: %s\n", jsonPath);
    return true;
#else
    (void)pcapPath;
    (void)metadata;
    return false;
#endif
}

/**
 * @brief Load handshake metadata from JSON file
 * @param pcapPath Full path to the .pcap file
 * @param metadata Output metadata struct
 * @return true if loaded successfully
 */
inline bool loadHandshakeMetadata(const char* pcapPath, HandshakeMetadata& metadata) {
#ifdef ESP32
    if (!pcapPath) return false;
    
    // Build .json path from .pcap path
    char jsonPath[128];
    strncpy(jsonPath, pcapPath, sizeof(jsonPath) - 1);
    char* ext = strstr(jsonPath, ".pcap");
    if (!ext) return false;
    strcpy(ext, ".json");
    
    // Open JSON file
    File file = SD.open(jsonPath, FILE_READ);
    if (!file) {
        return false;  // No metadata file
    }
    
    // Check heap before parsing - skip if too low to prevent NoMemory errors
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 10000) {  // StaticJsonDocument<512> needs ~2-3KB overhead
        file.close();
        Serial.printf("[Metadata] Skipping parse - low heap: %u bytes (file: %s)\n", freeHeap, pcapPath);
        return false;  // Skip metadata loading when heap is critically low
    }
    
    Serial.printf("[Metadata] Loading %s (heap: %u)\n", jsonPath, freeHeap);
    
    // Parse JSON (1024 bytes for metadata with GPS data)
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("[Metadata] Parse error: %s (heap: %u, needed more than 1KB buffer)\n", error.c_str(), freeHeap);
        return false;
    }
    
    // Populate metadata struct
    metadata.version = doc["version"] | 1;
    strncpy(metadata.ssid, doc["ssid"] | "", sizeof(metadata.ssid) - 1);
    
    // Parse BSSID
    const char* bssidStr = doc["bssid"] | "";
    sscanf(bssidStr, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
           &metadata.bssid[0], &metadata.bssid[1], &metadata.bssid[2],
           &metadata.bssid[3], &metadata.bssid[4], &metadata.bssid[5]);
    
    metadata.channel = doc["channel"] | 0;
    strncpy(metadata.type, doc["type"] | "4WAY", sizeof(metadata.type) - 1);
    metadata.capturedAt = doc["capturedAt"] | 0;
    metadata.hasMsg1 = doc["hasMsg1"] | false;
    metadata.hasMsg2 = doc["hasMsg2"] | false;
    metadata.hasMsg3 = doc["hasMsg3"] | false;
    metadata.hasMsg4 = doc["hasMsg4"] | false;
    metadata.hasPMKID = doc["hasPMKID"] | false;
    metadata.quality = doc["quality"] | 0;
    metadata.signalStrength = doc["signalStrength"] | 0;
    metadata.wpaSecStatus = static_cast<WpaSecStatus>(doc["wpaSecStatus"] | 0);
    metadata.wpaSecUploadedAt = doc["wpaSecUploadedAt"] | 0;
    metadata.wpaSecCrackedAt = doc["wpaSecCrackedAt"] | 0;
    
    if (doc.containsKey("wpaSecPassword")) {
        strncpy(metadata.wpaSecPassword, doc["wpaSecPassword"] | "", 
                sizeof(metadata.wpaSecPassword) - 1);
    }
    
    // GPS geolocation (optional)
    if (doc.containsKey("gps")) {
        JsonObject gps = doc["gps"];
        metadata.hasGPS = true;
        metadata.latitude = gps["latitude"] | 0.0;
        metadata.longitude = gps["longitude"] | 0.0;
        metadata.altitude = gps["altitude"] | 0.0f;
        metadata.satellites = gps["satellites"] | 0;
    }
    
    return true;
#else
    (void)pcapPath;
    (void)metadata;
    return false;
#endif
}

} // namespace adversary
