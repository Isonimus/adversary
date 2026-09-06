/**
 * @file station_scanner.h
 * @brief Passive station (client) scanner module
 * 
 * Discovers devices connected to WiFi access points by monitoring
 * data frames in promiscuous mode. Used for:
 * - Targeted deauth in handshake capture
 * - Client count display in scanner screen
 */

#pragma once

#include <cstdint>
#include <cstring>

#ifdef ESP32
#include <Arduino.h>
#include <esp_wifi.h>
#endif

namespace adversary {

/**
 * @brief Single discovered station (client device)
 */
struct DiscoveredStation {
    uint8_t mac[6] = {0};         // Client MAC address
    uint8_t bssid[6] = {0};       // Associated AP BSSID
    int8_t rssi = 0;              // Last seen signal strength
    uint32_t lastSeen = 0;        // Timestamp (millis)
    uint16_t frameCount = 0;      // Number of frames observed
    
    bool matches(const uint8_t* clientMac) const {
        return memcmp(mac, clientMac, 6) == 0;
    }
    
    bool isForAP(const uint8_t* apBssid) const {
        return memcmp(bssid, apBssid, 6) == 0;
    }
};

/**
 * @brief Station scan results container
 */
struct StationScanResult {
    static constexpr size_t MAX_STATIONS = 64;
    DiscoveredStation stations[MAX_STATIONS];
    size_t count = 0;
    
    void clear() {
        count = 0;
        memset(stations, 0, sizeof(stations));
    }
    
    /**
     * @brief Count stations for a specific AP
     */
    size_t countForBssid(const uint8_t* bssid) const {
        size_t result = 0;
        for (size_t i = 0; i < count; i++) {
            if (stations[i].isForAP(bssid)) {
                result++;
            }
        }
        return result;
    }
    
    /**
     * @brief Add or update a station
     * @return true if added (new), false if updated (existing)
     */
    bool addOrUpdate(const uint8_t* clientMac, const uint8_t* bssid, int8_t rssi) {
        // Check if already tracked
        for (size_t i = 0; i < count; i++) {
            if (stations[i].matches(clientMac)) {
                // Update existing
                stations[i].rssi = rssi;
                stations[i].frameCount++;
#ifdef ESP32
                stations[i].lastSeen = millis();
#endif
                return false;
            }
        }
        
        // Add new if space available
        if (count < MAX_STATIONS) {
            memcpy(stations[count].mac, clientMac, 6);
            memcpy(stations[count].bssid, bssid, 6);
            stations[count].rssi = rssi;
            stations[count].frameCount = 1;
#ifdef ESP32
            stations[count].lastSeen = millis();
#endif
            count++;
            return true;
        }
        
        return false;
    }
};

/**
 * @brief Passive station scanner using promiscuous mode
 */
class StationScanner {
public:
    static StationScanner& getInstance();
    
    // Prevent copying
    StationScanner(const StationScanner&) = delete;
    StationScanner& operator=(const StationScanner&) = delete;
    
    /**
     * @brief Start scanning on specific channel
     * @param channel WiFi channel (1-14)
     * @param durationMs Scan duration in milliseconds (0 = until stop())
     * @return true if started successfully
     */
    bool start(uint8_t channel, uint32_t durationMs = 5000);
    
    /**
     * @brief Start scanning for clients of specific AP
     * @param bssid AP BSSID to filter by
     * @param channel AP channel
     * @param durationMs Scan duration
     * @return true if started successfully
     */
    bool startForAP(const uint8_t* bssid, uint8_t channel, uint32_t durationMs = 3000);
    
    /**
     * @brief Stop scanning
     */
    void stop();
    
    /**
     * @brief Check if currently scanning
     */
    bool isScanning() const { return scanning_; }
    
    /**
     * @brief Update scanner (call from loop, handles timeout)
     */
    void update();
    
    /**
     * @brief Get all scan results
     */
    const StationScanResult& getResults() const { return results_; }
    
    /**
     * @brief Get station count for specific AP
     */
    size_t getStationCountForAP(const uint8_t* bssid) const {
        return results_.countForBssid(bssid);
    }
    
    /**
     * @brief Copy station MACs for specific AP
     * @param bssid AP BSSID
     * @param outMacs Output buffer for MAC addresses (6 bytes each)
     * @param maxCount Maximum stations to copy
     * @return Number of stations copied
     */
    size_t getStationsForAP(const uint8_t* bssid, uint8_t* outMacs, size_t maxCount) const;
    
    /**
     * @brief Clear all results
     */
    void clearResults() { results_.clear(); }
    
private:
    StationScanner() = default;
    ~StationScanner() = default;
    
#ifdef ESP32
    // Promiscuous mode callback (ESP32 only)
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#endif
    
    // Process a received frame
    void processFrame(const uint8_t* data, uint16_t len, int8_t rssi);
    
    // State
    bool scanning_ = false;
    uint32_t startTime_ = 0;
    uint32_t duration_ = 0;
    bool filterByBssid_ = false;
    uint8_t targetBssid_[6] = {0};
    
    // Results
    StationScanResult results_;
    
    // Static instance for callback
    static StationScanner* s_instance_;
};

} // namespace adversary
