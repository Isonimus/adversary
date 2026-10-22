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
#include <ctime>    // time_t for formatWigleTimestamp

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

    /**
     * @brief Format a network's WiGLE "FirstSeen" timestamp (pure, unit-testable).
     *
     * A network is stamped with the real wall-clock time it was first seen. Because
     * only device-uptime (`millis()`) is recorded per network, the real epoch is
     * reconstructed from the device's current millis and current wall-clock epoch:
     * `eventEpoch = currentEpoch - (currentMillis - eventMs)/1000`. If the clock was
     * never synced to a real epoch (`now()` returns 0, below MIN_SANE_EPOCH), the
     * epoch-zero sentinel "1970-01-01 00:00:00" is written rather than a fabricated
     * date — "time unknown" is stated, not masked.
     *
     * Kept free of millis()/TimeManager reads so it is testable off-hardware; the
     * live values are supplied by the single call site in writeNetworkToCSV.
     *
     * @param currentMillis Device uptime now (millis())
     * @param currentEpoch  Device wall-clock epoch now (TimeManager::now())
     * @param eventMs        The network's firstSeenMs (device uptime at first sighting)
     * @param out            Output buffer; receives "YYYY-MM-DD HH:MM:SS" (UTC)
     * @param outLen         Size of out (>= 20)
     */
    static void formatWigleTimestamp(uint32_t currentMillis, time_t currentEpoch,
                                     uint32_t eventMs, char* out, size_t outLen);

    /**
     * @brief Format a session id (pure, unit-testable).
     *
     * Emits "WD-YYYYMMDD-HHMMSS" (UTC) when `now` is a sane synced epoch whose year is
     * in [2001, 9999]; otherwise the uptime-hex fallback "WD-XXXXXXXX" from `uptimeMs`.
     * The year bound is a correctness guard, not cosmetics: an out-of-range clock would
     * otherwise overflow a fixed id buffer with a truncated datestamp. Kept free of
     * millis()/TimeManager reads so it is testable; the live values are supplied by
     * generateSessionId.
     *
     * @param now      Wall-clock epoch (TimeManager::now())
     * @param uptimeMs Device uptime (millis()), used only for the fallback id
     * @param out      Output buffer; receives a NUL-terminated id (needs >= 20)
     * @param outLen   Size of out
     */
    static void formatSessionId(time_t now, uint32_t uptimeMs, char* out, size_t outLen);

    /**
     * @brief Format one WiGLE CSV data row for a network entry (pure, unit-testable).
     *
     * Emits a single `WigleWifi-1.4` row terminated with '\n'. The row shape is
     * selected by `entry.type` (ADR-0060 / slice-0061):
     *   - WIFI: AuthMode from the encryption-type mapping, real WiFi channel, Type "WIFI".
     *   - BLE : AuthMode "Misc [LE]", Channel 0, Type "BLE" — WiGLE's documented BT/BLE
     *           field spec (api.wigle.net/csvFormat-1_4.html: "0 for Bluetooth devices",
     *           capability example "Misc [LE]"). The BLE discriminator is the Type column.
     *
     * Kept free of SD/display and of millis()/TimeManager reads so it is testable
     * off-hardware (the WIFI path must stay byte-identical to the pre-extraction writer).
     * The FirstSeen timestamp is reconstructed via formatWigleTimestamp from the live
     * clock values supplied by the single call site in writeNetworkToCSV.
     *
     * @param entry         The network to format
     * @param currentMillis Device uptime now (millis())
     * @param currentEpoch  Device wall-clock epoch now (TimeManager::now())
     * @param out           Output buffer; receives the NUL-terminated row (needs >= 160)
     * @param outLen        Size of out
     */
    static void formatWigleRow(const NetworkEntry& entry, uint32_t currentMillis,
                               time_t currentEpoch, char* out, size_t outLen);

    /**
     * @brief Compute the deduplication key for an observation (pure, unit-testable).
     *
     * A WiFi BSSID and a BLE BD_ADDR are both 6-byte MACs and would collide in a
     * plain MAC-keyed set, suppressing a BLE device that happens to share a WiFi
     * AP's address. The 6 MAC bytes occupy the low 48 bits of the key; the
     * observation type is folded into the high bits so WIFI and BLE MACs never
     * collide, while the WIFI key stays equal to the historical 48-bit MAC hash
     * (type WIFI == 0), leaving existing WiFi dedup untouched (slice-0061 D1).
     *
     * @param mac  6-byte address (BSSID or BD_ADDR)
     * @param type Observation source
     * @return 64-bit dedup key, unique per (mac, type)
     */
    static uint64_t observationKey(const uint8_t* mac, ObservationType type);

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
     * @brief Process BLE scan results (device-only; harvests BLEScanner devices).
     *
     * Mirrors processWiFiScan for the BLE source: maps each discovered advertiser to
     * an observation and feeds it through ingestObservation. Self-initializes the
     * NimBLE controller and continuous scan on first call so it is safe when the mode
     * is switched to BLE mid-session (ADR-0060 / slice-0061 D3).
     */
    void processBLEScan();

    /**
     * @brief Ingest one observation into the shared dedup/pending/GPS pipeline.
     *
     * The single code path both scan sources use (slice-0061 D1): dedup by
     * (mac, type), update the pending entry's RSSI/last-seen on a re-sighting, or add
     * a new entry and GPS-tag it. Keeps processWiFiScan/processBLEScan as thin,
     * source-specific mappers rather than duplicating the pipeline glue.
     *
     * @param mac            6-byte address (BSSID or BD_ADDR)
     * @param type           Observation source
     * @param rssi           Signal strength (dBm)
     * @param channel        WiFi channel, or 0 for BLE
     * @param encryptionType WiFi security enum value, or 0 for BLE (unused in the row)
     * @param name           SSID / BLE device name (may be null/empty)
     * @return true if a new entry was added, false if it was a known duplicate/full
     */
    bool ingestObservation(const uint8_t* mac, ObservationType type, int rssi,
                           uint8_t channel, uint8_t encryptionType, const char* name);

    /**
     * @brief Process GPS updates and calculate distance
     */
    void processGPSUpdate();
    
    /**
     * @brief Check if an observation has been seen before (O(1) lookup)
     * @param bssid 6-byte MAC address (BSSID or BD_ADDR)
     * @param type  Observation source (WiFi and BLE dedup independently)
     * @return true if already in hash set
     */
    bool hasSeenBSSID(const uint8_t* bssid, ObservationType type) const;

    /**
     * @brief Add new observation to pending buffer and hash set
     * @param bssid 6-byte MAC address (BSSID or BD_ADDR)
     * @param type  Observation source (recorded on the entry, selects the WiGLE row)
     * @return Pointer to new entry in pending buffer, nullptr if duplicate/full
     */
    NetworkEntry* addNewNetwork(const uint8_t* bssid, ObservationType type);
    
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
     * @brief Generate session ID from current time
     */
    void generateSessionId(char* buffer);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    bool active_;                              ///< Session is running
    bool paused_;                              ///< Session is paused
    WardrivingSession session_;                ///< Current session data
    
    // Bounded-memory network tracking (ADR-0103). The Bloom filter answers "seen this
    // (mac,type)?" in fixed memory regardless of device count; session_.networksFound is the
    // exact unique count (the Bloom cannot count), incremented only on a genuinely-new add.
    SeenFilter seenFilter_;                   ///< Fixed ~6 KB dedup, replaces unordered_set
    bool saturationWarned_;                   ///< One-time warn when the dedup hits capacity
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
