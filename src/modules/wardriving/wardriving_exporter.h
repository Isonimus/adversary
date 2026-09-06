/**
 * @file wardriving_exporter.h
 * @brief Wardriving data export (CSV + JSON)
 * 
 * Exports wardriving session data in Wigle.net compatible CSV format
 * and detailed JSON format for web visualization.
 */

#pragma once

#include "wardriving_types.h"
#include <vector>

namespace adversary {
namespace wardriving {

/**
 * @brief Wardriving data exporter
 */
class WardrivingExporter {
public:
    /**
     * @brief Export session to Wigle.net compatible CSV
     * @param session Session metadata
     * @param networks Vector of network entries
     * @param outputPath Full path to output file
     * @return true if successful
     */
    static bool exportToCSV(
        const WardrivingSession& session,
        const std::vector<NetworkEntry>& networks,
        const char* outputPath
    );
    
    /**
     * @brief Export session to detailed JSON
     * @param session Session metadata
     * @param networks Vector of network entries
     * @param outputPath Full path to output file
     * @return true if successful
     */
    static bool exportToJSON(
        const WardrivingSession& session,
        const std::vector<NetworkEntry>& networks,
        const char* outputPath
    );

private:
    /**
     * @brief Convert WiFiSecurity enum to Wigle CSV auth mode string
     * @param security Security type
     * @return Auth mode string (e.g., "[WPA2-PSK][ESS]")
     */
    static const char* securityToWigleAuthMode(uint8_t security);
    
    /**
     * @brief Convert WiFiSecurity enum to human-readable string
     * @param security Security type
     * @return Security string (e.g., "WPA2-PSK")
     */
    static const char* securityToString(uint8_t security);
    
    /**
     * @brief Format MAC address as XX:XX:XX:XX:XX:XX
     * @param bssid 6-byte MAC address
     * @param buffer Output buffer (min 18 bytes)
     */
    static void formatMAC(const uint8_t* bssid, char* buffer);
    
    /**
     * @brief Generate timestamp string from millis
     * @param timestampMs millis() value
     * @param buffer Output buffer (min 20 bytes)
     */
    static void formatTimestamp(uint32_t timestampMs, char* buffer);
    
    /**
     * @brief Convert WiFi channel number to frequency in MHz
     * @param channel WiFi channel (1-14 for 2.4GHz, 36-177 for 5GHz)
     * @return Frequency in MHz (e.g., 2412 for channel 1)
     */
    static int channelToFrequency(uint8_t channel);
};

} // namespace wardriving
} // namespace adversary
