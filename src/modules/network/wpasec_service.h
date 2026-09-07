/**
 * @file wpasec_service.h
 * @brief WPA-SEC cloud cracking service integration
 * 
 * Manages handshake upload to wpa-sec.stanev.org and
 * retrieves cracked passwords.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#ifdef ESP32
#include <WiFiClientSecure.h>
#include <SD.h>
#endif

namespace adversary {

/**
 * @brief Status of a handshake file in WPA-SEC
 */
enum class WpaSecStatus : uint8_t {
    NOT_UPLOADED,   // Red circle - not yet uploaded
    UPLOADED,       // Orange circle - uploaded, awaiting result
    CRACKED,        // Green circle - password found
    INVALID,        // Gray circle - invalid format, cannot upload
    INCOMPLETE      // White circle - missing frames (beacon/M3)
};

/**
 * @brief Result info for a cracked handshake
 */
struct WpaSecResult {
    char ssid[33] = {0};
    char bssid[18] = {0};
    char password[65] = {0};
    WpaSecStatus status = WpaSecStatus::NOT_UPLOADED;
};

/**
 * @brief WPA-SEC cloud cracking service client
 */
class WpaSecService {
public:
    static WpaSecService& getInstance() {
        static WpaSecService instance;
        return instance;
    }
    
    // Prevent copying
    WpaSecService(const WpaSecService&) = delete;
    WpaSecService& operator=(const WpaSecService&) = delete;
    
    /**
     * @brief Upload a handshake file to WPA-SEC
     * @param filepath Full path to the pcap file
     * @param deferCleanup when true (bulk upload), skip the per-file WiFi
     *        disconnect + rescanSummaries() so the caller can keep one WiFi
     *        session and do a single cleanup at the end. The per-file rescan also
     *        re-allocates the summaries vector, which fragments the freed-canvas
     *        region and breaks the 64KB canvas restore — so deferring it is what
     *        makes bulk upload work.
     * @return true if upload succeeded
     */
    bool uploadHandshake(const char* filepath, bool deferCleanup = false);
    
    /**
     * @brief Fetch cracked results from WPA-SEC
     * @return Number of cracked passwords retrieved
     */
    int fetchCrackedResults();
    
    /**
     * @brief Get status for a specific handshake file
     * @param filename Filename (without path)
     */
    WpaSecStatus getStatus(const char* filename) const;
    
    /**
     * @brief Set status for a specific handshake file
     */
    void setStatus(const char* filename, WpaSecStatus status);
    
    /**
     * @brief Get cracked password for a file (if available)
     * @return Password string or nullptr if not cracked
     */
    const char* getCrackedPassword(const char* filename) const;
    
    /**
     * @brief Validate pcap file format
     * @return true if file can be uploaded to WPA-SEC
     */
    bool validatePcap(const char* filepath);
    
    /**
     * @brief Check if WPA-SEC API key is configured
     */
    bool hasApiKey() const;
    
private:
    WpaSecService() = default;
    ~WpaSecService() = default;
    
    // No more global cache - status stored in per-handshake .json files
    
    static constexpr const char* WPASEC_HOST = "wpa-sec.stanev.org";
    static constexpr int WPASEC_PORT = 443;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline bool WpaSecService::hasApiKey() const {
#ifdef ESP32
    // Forward to SettingsManager
    extern bool _wpaSecHasApiKey();
    return _wpaSecHasApiKey();
#else
    return false;
#endif
}

// Status methods now implemented in .cpp (access per-handshake metadata files)

} // namespace adversary
