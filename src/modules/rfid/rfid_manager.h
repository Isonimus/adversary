/**
 * @file rfid_manager.h
 * @brief Singleton for RFID module communication (WS1850S)
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <MFRC522_I2C.h>
#include <string>
#include <vector>

namespace adversary {

/**
 * @struct RFIDTagInfo
 * @brief Data for a detected RFID tag
 */
struct RFIDTagInfo {
    std::string uid;
    std::string type;
    uint8_t sak;
    uint16_t atqa;
    std::string ats;
    bool isMagic;
    uint32_t lastSeen;
};

class RFIDManager {
public:
    static RFIDManager& getInstance() {
        static RFIDManager instance;
        return instance;
    }

    /**
     * @brief Initialize WS1850S module via I2C
     * @return true if detected
     */
    bool init();

    /**
     * @brief Periodic update (call from loop/task)
     * Polles for new tags.
     */
    void update();

    /**
     * @brief Get last detected tag
     */
    const RFIDTagInfo* getLastTag() const;

    /**
     * @brief Manually activate RFID module (turn antenna ON)
     */
    void activate();

    /**
     * @brief Manually deactivate RFID module (turn antenna OFF)
     */
    void deactivate();

    /**
     * @brief Check if module is actively polling
     */
    bool isActive() const { return m_active; }

    /**
     * @brief Check if module is present
     */
    bool isDetected() const { return m_detected; }

    /**
     * @brief Get pointer to the internal MFRC522 hardware driver
     */
    MFRC522_I2C* getReader() { return m_rfid; }

    /**
     * @brief Clear current tag data
     */
    void clearTag();

    /**
     * @brief Reset state for unit testing (not used in firmware)
     */
    void resetForTest();

private:
    RFIDManager();
    ~RFIDManager() = default;
    RFIDManager(const RFIDManager&) = delete;
    RFIDManager& operator=(const RFIDManager&) = delete;

    MFRC522_I2C* m_rfid;
    RFIDTagInfo m_lastTag;
    std::string m_lastNotifiedUid;
    bool m_detected;
    bool m_active;   // Whether the antenna is on and we are polling
    bool m_hasTag;
    uint32_t m_lastScanTime;

    static constexpr uint8_t I2C_ADDR = 0x28;
};

} // namespace adversary
