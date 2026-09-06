/**
 * @file wardriving_manager.h
 * @brief Wardriving session manager
 * 
 * Manages wardriving sessions, network discovery, and GPS tracking.
 * Uses stream-to-SD architecture to prevent memory growth during long sessions.
 */

#pragma once

#include "wardriving_types.h"
#include "wardriving_config.h"
#include <vector>
#include <algorithm>
#include <unordered_set>

#ifdef ARDUINO
#include <SD.h>
#endif

namespace adversary {
namespace wardriving {

/**
 * @brief Singleton manager for wardriving sessions
 * 
 * Memory-efficient design:
 * - BSSID hashes stored in unordered_set for O(1) deduplication
 * - New networks written directly to CSV on SD card
 * - Small RAM buffer (~20 entries) for pending GPS updates
 */
class WardrivingManager {
public:
    /**
     * @brief Get singleton instance
     */
    static WardrivingManager& getInstance();
    
    // ========================================================================
    // Session Control
    // ========================================================================
    
    /**
     * @brief Start a new wardriving session
     * @return true if started successfully
     */
    bool startSession();
    
    /**
     * @brief Stop current session and export data
     */
    void stopSession();
    
    /**
     * @brief Stop current session and discard data (deletes CSV)
     */
    void discardSession();
    
    /**
     * @brief Pause data collection
     */
    void pauseSession();
    
    /**
     * @brief Resume data collection
     */
    void resumeSession();
    
    // ========================================================================
    // Runtime Updates
    // ========================================================================
    
    /**
     * @brief Update wardriving state (call from main loop)
     * Processes WiFi scans and GPS updates
     */
    void update();
    
    // ========================================================================
    // State Queries
    // ========================================================================
    
    /**
     * @brief Check if session is active
     */
    bool isActive() const;
    
    /**
     * @brief Check if session is paused
     */
    bool isPaused() const;
    
    /**
     * @brief Get current session data
     */
    const WardrivingSession& getSession() const;
    
    /**
     * @brief Get total network count (from hash set, always accurate)
     */
    uint32_t getNetworkCount() const;
    
    /**
     * @brief Get pending networks buffer (for UI display)
     * Note: Only contains recently added networks awaiting GPS update
     */
    const std::vector<NetworkEntry>& getPendingNetworks() const { return pendingNetworks_; }
    
    /**
     * @brief Legacy getter - returns pending buffer (use getNetworkCount for total)
     */
    std::vector<NetworkEntry> getNetworks() const;
    
private:
    WardrivingManager();
    ~WardrivingManager();
    
    // Prevent copying
    WardrivingManager(const WardrivingManager&) = delete;
    WardrivingManager& operator=(const WardrivingManager&) = delete;
    
    // ========================================================================
    // Internal Processing
    // ========================================================================
    
    /**
     * @brief Process WiFi scan results
     */
    void processWiFiScan();
    
    /**
     * @brief Process GPS updates and calculate distance
     */
    void processGPSUpdate();
    
    /**
     * @brief Check if BSSID has been seen before (O(1) lookup)
     * @param bssid 6-byte MAC address
     * @return true if already in hash set
     */
    bool hasSeenBSSID(const uint8_t* bssid) const;
    
    /**
     * @brief Add new network to pending buffer and hash set
     * @param bssid 6-byte MAC address
     * @return Pointer to new entry in pending buffer, nullptr if duplicate
     */
    NetworkEntry* addNewNetwork(const uint8_t* bssid);
    
    /**
     * @brief Update network with GPS coordinates
     * @param entry Network entry to update
     */
    void updateNetworkGPS(NetworkEntry* entry);
    
    /**
     * @brief Flush pending networks to SD card CSV file
     * Writes all pending entries to CSV and clears buffer
     */
    void flushToSD();
    
    /**
     * @brief Write single network entry to CSV file
     * @param entry Network entry to write
     * @return true if written successfully
     */
    bool writeNetworkToCSV(const NetworkEntry& entry);
    
    /**
     * @brief Calculate distance between two GPS points (Haversine formula)
     */
    float calculateDistance(double lat1, double lon1, double lat2, double lon2);
    
    /**
     * @brief Hash BSSID to uint64_t for set storage
     */
    uint64_t hashBSSID(const uint8_t* bssid) const;
    
    /**
     * @brief Generate session ID from current time
     */
    void generateSessionId(char* buffer);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    bool active_;                              ///< Session is running
    bool paused_;                              ///< Session is paused
    WardrivingSession session_;                ///< Current session data
    
    // Memory-efficient network tracking
    std::unordered_set<uint64_t> seenBSSIDs_; ///< BSSID hashes for O(1) dedup (~8 bytes each)
    std::vector<NetworkEntry> pendingNetworks_; ///< Small buffer for GPS updates (~20 max)
    
    // Streaming CSV file
    char csvPath_[128];                        ///< Path to current CSV file
    bool csvHeaderWritten_;                    ///< CSV header has been written
    
    // Scan deduplication
    uint32_t lastProcessedScanCount_;          ///< To avoid re-processing same results
    
    // GPS tracking for distance calculation
    double lastLat_;                           ///< Last GPS latitude
    double lastLon_;                           ///< Last GPS longitude
    bool hasLastPosition_;                     ///< Have previous GPS position
    
    // Update timing
    uint32_t lastWiFiScanMs_;                  ///< Last WiFi scan time
    uint32_t lastGPSUpdateMs_;                 ///< Last GPS update time
    uint32_t lastFlushMs_;                     ///< Last time buffer was flushed to SD
};

} // namespace wardriving
} // namespace adversary
