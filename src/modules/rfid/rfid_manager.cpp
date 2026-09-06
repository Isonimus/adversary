/**
 * @file rfid_manager.cpp
 * @brief RFIDManager implementation for WS1850S
 */

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522_I2C.h>
#include "rfid_manager.h"
#include "config/pins.h"
#include "core/event_bus.h"

namespace adversary {

RFIDManager::RFIDManager()
    : m_rfid(nullptr)
    , m_detected(false)
    , m_active(false)
    , m_hasTag(false)
    , m_lastScanTime(0) {
}

bool RFIDManager::init() {
    if (m_detected) return true;

    Serial.println("[RFID] Initializing WS1850S (I2C 0x28)...");
    
    // RFID2 unit uses I2C. On Cardputer Port A Grove uses pins 2 (SDA) and 1 (SCL)
    // We must ensure Wire is initialized on these pins if we use Grove.
#if defined(TARGET_CARDPUTER)
    Wire.begin(2, 1);
#else
    Wire.begin();
#endif

    m_rfid = new MFRC522_I2C(I2C_ADDR, -1);
    
    // PCD_Init calls Reset, which ensures a clean state
    m_rfid->PCD_Init();
    delay(50); // Stabilization delay
    
    // Detection check: Read version register
    byte version = m_rfid->PCD_ReadRegister(m_rfid->VersionReg);
    
    if (version == 0x00 || version == 0xFF) {
        Serial.printf("[RFID] Module not detected! (Version: 0x%02X)\n", version);
        delete m_rfid;
        m_rfid = nullptr;
        m_detected = false;
        return false;
    }

    // Set antenna gain to MAX (48dB)
    // This is critical for reading tags that are weak or have small antennas
    // 0x07 << 4 sets the RxGain bits to the maximum value
    m_rfid->PCD_SetAntennaGain(m_rfid->RxGain_max);

    Serial.printf("[RFID] WS1850S detected. Version: 0x%02X, Gain: MAX (48dB)\n", version);
    m_detected = true;
    
    // Antena starts OFF by default to save power
    deactivate();
    
    return true;
}

void RFIDManager::update() {
    if (!m_detected || !m_active || !m_rfid) return;

    // Limit polling rate to 5Hz to avoid flooding I2C
    if (millis() - m_lastScanTime < 200) return;
    m_lastScanTime = millis();

    // Look for new cards
    if (!m_rfid->PICC_IsNewCardPresent()) {
        return;
    }

    // Select one of the cards
    if (!m_rfid->PICC_ReadCardSerial()) {
        return;
    }

    // Tag detected
    m_lastTag.lastSeen = millis();
    
    // ATQA is returned by PICC_RequestA/WakeupA which happened just before ReadCardSerial
    // However, the MFRC522 library doesn't always expose it. 
    // We can try to re-read it if necessary, but it's usually 2 bytes.
    // For now, we'll focus on SAK and ATS.
    m_lastTag.sak = m_rfid->uid.sak;
    
    // Convert UID to string
    char uidBuf[32];
    char* p = uidBuf;
    for (byte i = 0; i < m_rfid->uid.size; i++) {
        p += sprintf(p, "%02X", m_rfid->uid.uidByte[i]);
    }
    m_lastTag.uid = uidBuf;
    
    // Get type
    MFRC522_I2C::PICC_Type piccType = (MFRC522_I2C::PICC_Type)m_rfid->PICC_GetType(m_rfid->uid.sak);
    m_lastTag.type = String(m_rfid->PICC_GetTypeName(piccType)).c_str();

    // ISO/IEC 14443-4 Extended Details (ATS)
    m_lastTag.ats = "";
    if (m_lastTag.sak & 0x20) {
        // This is an ISO-4 tag. Attempt to get ATS via RATS command.
        byte ratsBuffer[32];
        byte ratsSize = sizeof(ratsBuffer);
        
        // RATS: 0xE0, 0x50 (0x50 = FSDI=5, which is standard)
        byte ratsCmd[] = {0xE0, 0x50};
        MFRC522_I2C::StatusCode status = (MFRC522_I2C::StatusCode)m_rfid->PCD_CommunicateWithPICC(
            MFRC522_I2C::PCD_Transceive, 0x0C, ratsCmd, 2, ratsBuffer, &ratsSize
        );
        
        if (status == MFRC522_I2C::STATUS_OK && ratsSize > 0) {
            char atsBuf[128];
            char* ptr = atsBuf;
            for (byte i = 0; i < ratsSize; i++) {
                ptr += sprintf(ptr, "%02X ", ratsBuffer[i]);
            }
            m_lastTag.ats = atsBuf;
        }
    }
    
    // Check for Magic Card Gen1 (UID changeable via backdoor)
    m_lastTag.isMagic = false; 

    Serial.printf("[RFID] Tag Detected: %s (%s, SAK: 0x%02X)\n", 
                  m_lastTag.uid.c_str(), m_lastTag.type.c_str(), m_lastTag.sak);
    m_hasTag = true;

    // Notify ONLY if it's a new UID detect
    if (m_lastTag.uid != m_lastNotifiedUid) {
        // Publish EventBus event for semantic notification
        EventData event(EventType::RFID_TAG_DETECTED);
        // Include partial UID in network SSID field for identification in notification router if needed
        strncpy(event.payload.network.ssid, m_lastTag.uid.c_str(), 32); 
        EventBus::getInstance().publish(event);
        
        m_lastNotifiedUid = m_lastTag.uid;
    }

    // We do NOT Halt here anymore, to keep the tag ACTIVE for immediate auditing.
    // m_rfid->PICC_HaltA();
}

const RFIDTagInfo* RFIDManager::getLastTag() const {
    return m_hasTag ? &m_lastTag : nullptr;
}

void RFIDManager::clearTag() {
    if (m_hasTag && m_rfid) {
        // Halt and stop crypto when explicitly clearing, to allow re-scans
        m_rfid->PICC_HaltA();
        m_rfid->PCD_StopCrypto1();
    }
    m_hasTag = false;
    m_lastNotifiedUid = ""; // Reset to allow re-notifying if same tag returns
}

void RFIDManager::activate() {
    if (!m_detected || !m_rfid) return;
    
    Serial.println("[RFID] Activating antenna...");
    m_rfid->PCD_AntennaOn();
    m_active = true;
}

void RFIDManager::deactivate() {
    if (!m_detected || !m_rfid) return;
    
    Serial.println("[RFID] Deactivating antenna (low power)...");
    m_rfid->PCD_StopCrypto1();
    m_rfid->PICC_HaltA();
    m_rfid->PCD_AntennaOff();
    m_active = false;
}

void RFIDManager::resetForTest() {
    if (m_rfid) delete m_rfid;
    m_rfid = nullptr;
    m_detected = false;
    m_active = false;
    m_hasTag = false;
    m_lastNotifiedUid = "";
    m_lastScanTime = 0;
}

} // namespace adversary
