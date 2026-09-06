/**
 * @file rfid_audit.h
 * @brief Logic for RFID dictionary attacks and sector dumping
 */

#pragma once

#include <Arduino.h>
#include <MFRC522_I2C.h>
#include <vector>
#include <string>
#include <functional>

namespace adversary {

/**
 * @struct MifareSectorData
 * @brief Data for a single MIFARE Classic sector
 */
struct MifareSectorData {
    uint8_t sector;
    bool keyAFound;
    bool keyBFound;
    uint8_t keyA[6];
    uint8_t keyB[6];
    uint8_t keyAIndex;     // Which default key matched (0-31, 255=none)
    uint8_t keyBIndex;     // Which default key matched (0-31, 255=none)
    // NOTE: Block data is streamed to SD card, not stored here
};

/**
 * @struct AuditProgressInfo
 * @brief Detailed progress for RFID auditing
 */
struct AuditProgressInfo {
    uint8_t currentSector;
    bool isKeyB;           // false = Key A, true = Key B
    int keysFound;
    float overallProgress;
    bool sectorResults[16][2]; // [sector][0=KeyA, 1=KeyB]
    uint32_t elapsedMs;    // Time elapsed since audit start
};

/**
 * @struct AuditSummary
 * @brief Post-audit summary statistics
 */
struct AuditSummary {
    int totalKeysFound;
    int sectorsFullyCracked;      // Both Key A and B found
    int sectorsPartiallyCracked;  // Only Key A or B found
    int sectorsUncracked;
    uint32_t durationMs;
    bool dataReadEnabled;
    char dumpFilename[64];
};

/**
 * @struct AuditOptions
 * @brief Options passed to runDictionaryAttack
 */
struct AuditOptions {
    bool readData;           // Whether to read block data after cracking
    const char* dumpPath;    // Path to stream data (e.g., /adversary/captures/RFID/xxx.nfc)
};

class RfidAudit {
public:
    RfidAudit(MFRC522_I2C* rfid);

    /**
     * @brief Run a dictionary attack on a MIFARE Classic tag
     * @param onProgress Callback for detailed progress
     * @param opts Audit options (read data, dump path)
     * @return Number of keys found, or -1 on incompatible tag
     */
    int runDictionaryAttack(
        std::function<void(const AuditProgressInfo&)> onProgress,
        const AuditOptions& opts = {}
    );

    /**
     * @brief Save current audit data to SD card (.nfc format)
     * @param filename Base filename
     * @return true if success
     */
    bool saveToSD(const char* filename);

    /**
     * @brief Get results
     */
    const std::vector<MifareSectorData>& getResults() const { return m_results; }

    /**
     * @brief Get audit summary statistics
     */
    AuditSummary getSummary() const;

    /**
     * @brief Get human-readable name for a key index
     * @param keyIndex Index into DEFAULT_KEYS array
     */
    static const char* getKeyName(uint8_t keyIndex);

    /**
     * @brief Number of default keys in dictionary
     */
    static const size_t NUM_DEFAULT_KEYS;

private:
    MFRC522_I2C* m_rfid;
    std::vector<MifareSectorData> m_results;
    uint32_t m_auditDurationMs;
    bool m_dataReadEnabled;
    char m_dumpFilename[64];
    
    // Default keys to try (extended from Flipper Zero dictionary)
    static const uint8_t DEFAULT_KEYS[][6];
    static const char* KEY_NAMES[];

    bool tryKey(uint8_t block, uint8_t* key, bool isKeyA);
};

} // namespace adversary
