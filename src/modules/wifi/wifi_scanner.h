/**
 * @file wifi_scanner.h
 * @brief WiFi network scanner module
 * 
 * Scans for nearby WiFi networks and provides detailed information
 * about each discovered access point.
 */

#ifndef ADVERSARY_WIFI_SCANNER_H
#define ADVERSARY_WIFI_SCANNER_H

#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace adversary {

/**
 * @brief Security type of a WiFi network
 */
enum class WiFiSecurity : uint8_t {
    OPEN = 0,
    WEP,
    WPA_PSK,
    WPA2_PSK,
    WPA_WPA2_PSK,
    WPA2_ENTERPRISE,
    WPA3_PSK,
    WPA2_WPA3_PSK,
    UNKNOWN
};

/**
 * @brief Information about a discovered WiFi network
 */
struct NetworkInfo {
    std::string ssid;           ///< Network name (may be empty for hidden networks)
    uint8_t bssid[6];           ///< MAC address of the access point
    int8_t rssi;                ///< Signal strength in dBm
    uint8_t channel;            ///< WiFi channel (1-14)
    WiFiSecurity security;      ///< Security type
    bool isHidden;              ///< True if SSID is hidden
    uint32_t lastSeen;          ///< Timestamp when last seen (millis)
    
    /**
     * @brief Get BSSID as formatted string (XX:XX:XX:XX:XX:XX)
     */
    std::string getBssidString() const;
    
    /**
     * @brief Get security type as human-readable string
     */
    const char* getSecurityString() const;
    
    /**
     * @brief Get signal quality as percentage (0-100)
     */
    uint8_t getSignalQuality() const;
};

/**
 * @brief Scanner state
 */
enum class ScannerState : uint8_t {
    IDLE,
    SCANNING,
    COMPLETED,
    ERROR
};

/**
 * @brief WiFi network scanner
 * 
 * Provides functionality to scan for nearby WiFi networks,
 * with support for continuous scanning and filtering.
 */
class WiFiScanner {
public:
    /// Callback for scan completion
    // ScanCallback removed in favor of EventBus (WIFI_SCAN_COMPLETED)
    
    /**
     * @brief Get singleton instance
     */
    static WiFiScanner& getInstance();
    
    // Prevent copying
    WiFiScanner(const WiFiScanner&) = delete;
    WiFiScanner& operator=(const WiFiScanner&) = delete;
    
    /**
     * @brief Initialize the scanner
     * @return true if initialization successful
     */
    bool init();
    
    /**
     * @brief Deinitialize and cleanup
     */
    void deinit();
    
    /**
     * @brief Start an asynchronous scan
     * @return true if scan started successfully
     */
    bool startScan();
    
    /**
     * @brief Start a synchronous (blocking) scan
     * @param timeoutMs Maximum time to wait for scan
     * @return true if scan completed successfully
     */
    bool scanSync(uint32_t timeoutMs = 5000);
    
    /**
     * @brief Stop any ongoing scan
     */
    void stopScan();
    
    /**
     * @brief Check and process scan results (call from loop)
     */
    void update();
    
    /**
     * @brief Get current scanner state
     */
    ScannerState getState() const { return m_state; }
    
    /**
     * @brief Check if currently scanning
     */
    bool isScanning() const { return m_state == ScannerState::SCANNING; }
    
    /**
     * @brief Get discovered networks from last scan
     */
    const std::vector<NetworkInfo>& getNetworks() const { return m_networks; }
    
    /**
     * @brief Get number of discovered networks
     */
    size_t getNetworkCount() const { return m_networks.size(); }

    /**
     * @brief Clear all discovered networks and free RAM
     */
    void clearResults();
    
    /**
     * @brief Get network by index
     * @param index Network index
     * @return Pointer to network info, or nullptr if invalid index
     */
    const NetworkInfo* getNetwork(size_t index) const;
    
    /**
     * @brief Sort networks by signal strength (strongest first)
     */
    void sortBySignal();
    
    /**
     * @brief Sort networks by channel
     */
    void sortByChannel();
    
    /**
     * @brief Sort networks by SSID alphabetically
     */
    void sortBySSID();
    
    /**
     * @brief Get total scan count since init
     */
    uint32_t getScanCount() const { return m_scanCount; }
    
    /**
     * @brief Enable/disable continuous scanning mode
     * @param enable True to enable continuous scanning
     * @param intervalMs Interval between scans in milliseconds
     */
    void setContinuousMode(bool enable, uint32_t intervalMs = 3000);
    
    /**
     * @brief Check if continuous mode is enabled
     */
    bool isContinuousMode() const { return m_continuousMode; }
    
#ifdef UNIT_TEST
    /**
     * @brief Set mock networks for testing (UNIT_TEST only)
     */
    void setMockNetworks(const std::vector<NetworkInfo>& networks) {
        m_networks = networks;
        m_state = ScannerState::COMPLETED;
    }
    
    /**
     * @brief Increment scan count for testing (UNIT_TEST only)
     */
    void incrementScanCount() { m_scanCount++; }
#endif

private:
    WiFiScanner();
    ~WiFiScanner();
    
    /**
     * @brief Process raw scan results from WiFi driver
     */
    void processResults();
    
    /**
     * @brief Convert WiFi auth mode to our security enum
     */
    static WiFiSecurity authModeToSecurity(uint8_t authMode);
    
    std::vector<NetworkInfo> m_networks;

    // ScanCallback m_callback; // Removed
    ScannerState m_state;
    uint32_t m_scanCount;
    uint32_t m_scanStartTime;
    uint32_t m_lastScanTime;
    uint32_t m_continuousInterval;
    bool m_initialized;
    bool m_continuousMode;
};

} // namespace adversary

#endif // ADVERSARY_WIFI_SCANNER_H
