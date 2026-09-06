#pragma once

/**
 * @file sd_manager.h
 * @brief SD Card management and file operations
 * 
 * This is the first module to be initialized - handles all persistent storage.
 */

#include <stdint.h>
#include <stddef.h>

// Arduino/native SPI bus handle, shared with the multi-radio cap probe.
class SPIClass;

namespace adversary {

/**
 * @brief SD Card status
 */
enum class SDStatus {
    NOT_INITIALIZED,
    NO_CARD,
    MOUNT_FAILED,
    READY,
    ERROR
};

/**
 * @brief File operation result
 */
struct FileResult {
    bool success;
    const char* error;
    size_t bytesWritten;
    size_t bytesRead;
};

/**
 * @brief SD Card information
 */
struct SDCardInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    const char* type;  // "SD", "SDHC", "SDXC"
};

/**
 * @brief Get string representation of SD status
 */
const char* sdStatusToString(SDStatus status);

/**
 * @brief SD Card Manager class
 */
class SDManager {
public:
    /**
     * @brief Get singleton instance
     */
    static SDManager& getInstance();

    // Delete copy/move constructors
    SDManager(const SDManager&) = delete;
    SDManager& operator=(const SDManager&) = delete;

    /**
     * @brief Initialize SD card
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief The dedicated FSPI bus the card is mounted on, for sharing.
     *
     * Returns the SPIClass instance SDManager owns (Method-2 mount), so another
     * device on the same physical bus — the multi-radio cap's CC1101/NRF24 —
     * can transact over one peripheral instead of a conflicting second one
     * (slice-0002). Returns nullptr when the launcher pre-mounted the card
     * (Method 1) and SDManager does not own the bus.
     */
    SPIClass* spiBus();

    /**
     * @brief Force a clean re-mount of the SD bus.
     *
     * Tears down the SD driver (SD.end()) and re-runs init() so the card's SPI
     * state machine is re-synced from CMD0. Needed after the multi-radio cap
     * probe transacts on the shared bus, which desyncs the card and yields CRC
     * errors on subsequent SD access (slice-0002).
     * @return true if the card re-mounted successfully.
     */
    bool remount();

    /**
     * @brief Deinitialize SD card
     */
    void deinit();

    /**
     * @brief Get current SD status
     */
    SDStatus getStatus() const { return m_status; }

    /**
     * @brief Check if SD card is ready for operations
     */
    bool isReady() const { return m_status == SDStatus::READY; }

    /**
     * @brief Get SD card info
     */
    SDCardInfo getCardInfo() const;

    /**
     * @brief Create directory structure for adversary
     * @return true if all directories created/exist
     */
    bool createDirectoryStructure();

    /**
     * @brief Check if file exists
     */
    bool fileExists(const char* path) const;

    /**
     * @brief Check if directory exists
     */
    bool directoryExists(const char* path) const;

    /**
     * @brief Create directory (recursive)
     */
    bool createDirectory(const char* path);

    /**
     * @brief Delete file
     */
    bool deleteFile(const char* path);

    /**
     * @brief Delete directory (must be empty)
     */
    bool deleteDirectory(const char* path);

    /**
     * @brief Rename/move file
     */
    bool renameFile(const char* oldPath, const char* newPath);

    /**
     * @brief Get file size
     */
    size_t getFileSize(const char* path) const;

    /**
     * @brief Write data to file (overwrites if exists)
     */
    FileResult writeFile(const char* path, const uint8_t* data, size_t size);

    /**
     * @brief Write string to file (overwrites if exists)
     */
    FileResult writeFile(const char* path, const char* content);

    /**
     * @brief Append data to file
     */
    FileResult appendFile(const char* path, const uint8_t* data, size_t size);

    /**
     * @brief Append string to file
     */
    FileResult appendFile(const char* path, const char* content);

    /**
     * @brief Read entire file into buffer
     * @param path File path
     * @param buffer Output buffer (must be pre-allocated)
     * @param bufferSize Size of buffer
     */
    FileResult readFile(const char* path, uint8_t* buffer, size_t bufferSize);

    /**
     * @brief Generate unique filename with timestamp
     * @param prefix Filename prefix (e.g., "handshake")
     * @param extension File extension (e.g., ".pcap")
     * @param outBuffer Output buffer for filename
     * @param bufferSize Size of output buffer
     */
    void generateTimestampFilename(const char* prefix, const char* extension,
                                    char* outBuffer, size_t bufferSize);

    /**
     * @brief Get free space percentage
     */
    uint8_t getFreeSpacePercent() const;

    /**
     * @brief Check if there's enough space for capture
     * @param requiredBytes Minimum bytes needed
     */
    bool hasSpaceFor(size_t requiredBytes) const;

private:
    SDManager();
    ~SDManager();

    SDStatus m_status;
    bool m_initialized;
};

} // namespace adversary
