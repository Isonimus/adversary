/**
 * @file wigle_service.h
 * @brief WiGLE.net upload service for wardriving CSV files
 * 
 * Manages upload of wardriving CSV files to WiGLE.net API v2.
 * Status tracked via sidecar .wigle marker files (zero heap cost).
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace adversary {

/**
 * @brief Upload status for a wardriving CSV file
 */
enum class WigleStatus : uint8_t {
    NOT_UPLOADED,   // Red circle - not yet uploaded
    UPLOADED        // Green circle - successfully uploaded
};

/**
 * @brief WiGLE.net upload service client
 */
class WigleService {
public:
    static WigleService& getInstance() {
        static WigleService instance;
        return instance;
    }
    
    // Prevent copying
    WigleService(const WigleService&) = delete;
    WigleService& operator=(const WigleService&) = delete;
    
    /**
     * @brief Upload a wardriving CSV file to WiGLE
     * @param filepath Full path to the CSV file on SD
     * @return true if upload succeeded
     */
    bool uploadCSV(const char* filepath);
    
    /**
     * @brief Get upload status for a CSV file
     * @param filename Filename (without path)
     * @return WigleStatus based on sidecar file existence
     */
    WigleStatus getStatus(const char* filename) const;
    
    /**
     * @brief Check if WiGLE API key is configured
     */
    bool hasApiKey() const;

private:
    WigleService() = default;
    ~WigleService() = default;
    
    /**
     * @brief Build sidecar marker path for a given CSV filename
     * @param filename CSV filename (e.g., "session_001.csv")
     * @param outPath Output buffer (min 128 bytes)
     */
    static void getSidecarPath(const char* filename, char* outPath, size_t outSize);
    
    /**
     * @brief Create sidecar marker file to indicate successful upload
     */
    static bool createSidecar(const char* filename);
    
    static constexpr const char* WIGLE_HOST = "api.wigle.net";
    static constexpr int WIGLE_PORT = 443;
    static constexpr const char* WIGLE_UPLOAD_PATH = "/api/v2/file/upload";
};

} // namespace adversary
