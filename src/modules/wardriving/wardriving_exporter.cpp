/**
 * @file wardriving_exporter.cpp
 * @brief Wardriving data export implementation
 */

#include "wardriving_exporter.h"
#include "wardriving_config.h"
#include "../wifi/wifi_scanner.h"
#include <SD.h>
#include <Arduino.h>
#include <time.h>
#include "../system/time_manager.h"

// Logging macros
#define LOG_I(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_E(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

namespace adversary {
namespace wardriving {

// ============================================================================
// CSV Export (Wig.net format)
// ============================================================================

bool WardrivingExporter::exportToCSV(
    const WardrivingSession& session,
    const std::vector<NetworkEntry>& networks,
    const char* outputPath
) {
    File csvFile = SD.open(outputPath, FILE_WRITE);
    if (!csvFile) {
        LOG_E("[Exporter] Failed to create CSV: %s", outputPath);
        return false;
    }
    
    // Write Wigle.net pre-header (version 1.6)
    csvFile.println("WigleWifi-1.6,appRelease=Adversary,model=Cardputer,release=1.0,device=ESP32-S3,display=Cardputer,board=ESP32-S3,brand=M5Stack,star=Sol,body=3,subBody=0");
    
    // Write column headers (v1.6 format)
    csvFile.println("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type");
    
    // Count networks with GPS
    size_t networksWithGPS = 0;
    
    // Write network entries (only those with GPS)
    for (const auto& net : networks) {
        if (!net.hasGPS) {
            continue;  // Skip networks without GPS coordinates
        }
        
        networksWithGPS++;
        
        // Format MAC address
        char macStr[18];
        formatMAC(net.bssid, macStr);
        
        // Format timestamp
        char timeStr[20];
        formatTimestamp(net.firstSeenMs, timeStr);
        
        // Get auth mode string
        const char* authMode = securityToWigleAuthMode(net.encryptionType);
        
        // Derive frequency from channel
        int frequency = channelToFrequency(net.channel);
        
        // Estimate accuracy from GPS (rough approximation)
        float accuracyMeters = 10.0f;  // Default estimate (we don't store HDOP per network)
        
        // Write CSV row (v1.6 format)
        // Format: MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,Lat,Lon,Alt,Accuracy,RCOIs,MfgrId,Type
        csvFile.printf("%s,%s,%s,%s,%d,%d,%d,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
                      macStr,
                      net.ssid,
                      authMode,
                      timeStr,
                      net.channel,
                      frequency,
                      net.rssi,
                      net.latitude,
                      net.longitude,
                      net.altitude,
                      accuracyMeters);
    }
    
    csvFile.close();
    
    // Log with GPS stats
    if (networksWithGPS == 0 && networks.size() > 0) {
        LOG_E("[Exporter] CSV exported: %s - WARNING: 0/%u networks have GPS!", 
              outputPath, networks.size());
        LOG_E("[Exporter] CSV will only contain headers - GPS fix was never acquired");
    } else {
        LOG_I("[Exporter] CSV exported: %s (%u/%u networks with GPS)", 
              outputPath, networksWithGPS, networks.size());
    }
    
    return true;
}

// ============================================================================
// JSON Export (Extended format)
// ============================================================================

bool WardrivingExporter::exportToJSON(
    const WardrivingSession& session,
    const std::vector<NetworkEntry>& networks,
    const char* outputPath
) {
    // Open file for writing
    File jsonFile = SD.open(outputPath, FILE_WRITE);
    if (!jsonFile) {
        LOG_E("[Exporter] Failed to create JSON: %s", outputPath);
        return false;
    }
    
    LOG_I("[Exporter] Writing JSON with %u networks (streaming mode)...", networks.size());
    
    // Write JSON manually to avoid massive heap allocation
    // For hundreds of networks, DynamicJsonDocument would need 150KB+
    
    jsonFile.println("{");
    
    // Session metadata
    jsonFile.println("  \"version\": \"1.0\",");
    jsonFile.printf("  \"sessionId\": \"%s\",\n", session.sessionId);
    jsonFile.printf("  \"startTime\": %lu,\n", (unsigned long)(session.startTimeMs / 1000));
    jsonFile.printf("  \"endTime\": %lu,\n", (unsigned long)(session.endTimeMs / 1000));
    jsonFile.printf("  \"durationSeconds\": %lu,\n", (unsigned long)session.getDurationSeconds());
    jsonFile.printf("  \"networksFound\": %lu,\n", (unsigned long)session.networksFound);
    jsonFile.printf("  \"networksWithGPS\": %lu,\n", (unsigned long)session.networksWithGPS);
    jsonFile.printf("  \"distanceTraveledKm\": %.3f,\n", session.distanceTraveledKm);
    
    // Device info
    jsonFile.println("  \"device\": {");
    jsonFile.println("    \"model\": \"Cardputer\",");
    jsonFile.println("    \"firmware\": \"1.0.0\"");
    jsonFile.println("  },");
    
    // Networks array (write each entry individually)
    jsonFile.println("  \"networks\": [");
    
    for (size_t i = 0; i < networks.size(); i++) {
        const auto& net = networks[i];
        
        // Format BSSID
        char macStr[18];
        formatMAC(net.bssid, macStr);
        
        jsonFile.println("    {");
        jsonFile.printf("      \"ssid\": \"%s\",\n", net.ssid);
        jsonFile.printf("      \"bssid\": \"%s\",\n", macStr);
        jsonFile.printf("      \"rssi\": %d,\n", net.rssi);
        jsonFile.printf("      \"channel\": %d,\n", net.channel);
        jsonFile.printf("      \"encryption\": \"%s\",\n", securityToString(net.encryptionType));
        
        if (net.hasGPS) {
            jsonFile.printf("      \"latitude\": %.6f,\n", net.latitude);
            jsonFile.printf("      \"longitude\": %.6f,\n", net.longitude);
            jsonFile.printf("      \"altitude\": %.1f,\n", net.altitude);
            jsonFile.printf("      \"satellites\": %d,\n", net.satellites);
            jsonFile.println("      \"hasGPS\": true,");
        } else {
            jsonFile.println("      \"hasGPS\": false,");
        }
        
        jsonFile.printf("      \"firstSeen\": %lu,\n", (unsigned long)(net.firstSeenMs / 1000));
        jsonFile.printf("      \"lastSeen\": %lu,\n", (unsigned long)(net.lastSeenMs / 1000));
        jsonFile.printf("      \"seenCount\": %lu\n", (unsigned long)net.seenCount);
        
        // Last network has no comma
        if (i < networks.size() - 1) {
            jsonFile.println("    },");
        } else {
            jsonFile.println("    }");
        }
        
        // Flush periodically to avoid SD buffer issues
        if (i % 10 == 0) {
            jsonFile.flush();
        }
    }
    
    jsonFile.println("  ]");
    jsonFile.println("}");
    
    jsonFile.close();
    
    LOG_I("[Exporter] JSON exported: %s (%u networks)", outputPath, networks.size());
    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

const char* WardrivingExporter::securityToWigleAuthMode(uint8_t security) {
    // Map WiFiSecurity enum to Wigle CSV auth mode format
    // WiFiSecurity enum values (from wifi_scanner.h):
    // OPEN=0, WEP=1, WPA=2, WPA2=3, WPA3=4, WPA_WPA2=5, WPA2_WPA3=6
    
    switch (security) {
        case 0:  return "[Open][ESS]";          // OPEN
        case 1:  return "[WEP][ESS]";           // WEP
        case 2:  return "[WPA-PSK][ESS]";       // WPA
        case 3:  return "[WPA2-PSK][ESS]";      // WPA2
        case 4:  return "[WPA3-SAE][ESS]";      // WPA3
        case 5:  return "[WPA-PSK][WPA2-PSK][ESS]"; // WPA/WPA2 mixed
        case 6:  return "[WPA2-PSK][WPA3-SAE][ESS]"; // WPA2/WPA3 mixed
        default: return "[Unknown][ESS]";
    }
}

const char* WardrivingExporter::securityToString(uint8_t security) {
    // Simple security type string for JSON
    switch (security) {
        case 0:  return "Open";
        case 1:  return "WEP";
        case 2:  return "WPA-PSK";
        case 3:  return "WPA2-PSK";
        case 4:  return "WPA3-SAE";
        case 5:  return "WPA/WPA2";
        case 6:  return "WPA2/WPA3";
        default: return "Unknown";
    }
}

void WardrivingExporter::formatMAC(const uint8_t* bssid, char* buffer) {
    // Format as xx:xx:xx:xx:xx:xx (lowercase per WiGLE spec)
    snprintf(buffer, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5]);
}

void WardrivingExporter::formatTimestamp(uint32_t timestampMs, char* buffer) {
    // Convert millis-based timestamp to real wall-clock time
    // Uses TimeManager to compute offset from millis() to real epoch
    uint32_t currentMillis = millis();
    time_t currentEpoch = TimeManager::getInstance().now();
    
    // Calculate how long ago this event happened
    uint32_t elapsedMs = currentMillis - timestampMs;
    time_t eventEpoch = currentEpoch - (elapsedMs / 1000);
    
    struct tm* timeinfo = gmtime(&eventEpoch);
    if (timeinfo && eventEpoch > 1000000000) {  // Sanity: after ~2001
        strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        // Fallback: if time was never synced, use millis-based estimate
        uint32_t seconds = timestampMs / 1000;
        time_t t = seconds;
        struct tm* fallback = localtime(&t);
        if (fallback) {
            strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", fallback);
        } else {
            snprintf(buffer, 20, "1970-01-01 00:00:00");
        }
    }
}

int WardrivingExporter::channelToFrequency(uint8_t channel) {
    // 2.4 GHz band: channels 1-14
    if (channel >= 1 && channel <= 13) {
        return 2407 + (channel * 5);
    }
    if (channel == 14) {
        return 2484;
    }
    // 5 GHz band: common channels
    if (channel >= 36 && channel <= 177) {
        return 5000 + (channel * 5);
    }
    return 0; // Unknown
}

} // namespace wardriving
} // namespace adversary
