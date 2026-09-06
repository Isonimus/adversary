#pragma once

/**
 * @file mfrc522_mocks.h
 * @brief Standards-compliant mock for MFRC522_I2C to match real library
 */

#ifndef ESP32
#pragma message("USING REALISTIC MFRC522 MOCK")

#include <stdint.h>
#include <cstring>
#include <vector>

typedef uint8_t byte;

class MFRC522_I2C {
public:
    enum PCD_Register : byte {
        VersionReg = 0x37
    };

    enum PCD_Command : byte {
        PCD_Idle = 0x00,
        PCD_Mem = 0x01,
        PCD_GenerateRandomID = 0x02,
        PCD_CalcCRC = 0x03,
        PCD_Transceive = 0x0C,
        PCD_MFAuthent = 0x0E,
        PCD_SoftReset = 0x0F
    };

    enum PICC_Command : byte {
        PICC_CMD_REQA = 0x26,
        PICC_CMD_WUPA = 0x52,
        PICC_CMD_MF_AUTH_KEY_A = 0x60,
        PICC_CMD_MF_AUTH_KEY_B = 0x61,
        PICC_CMD_HLTA = 0x50
    };

    enum StatusCode : byte {
        STATUS_OK = 1,
        STATUS_ERROR = 2,
        STATUS_COLLISION = 3,
        STATUS_TIMEOUT = 4,
        STATUS_NO_ROOM = 5,
        STATUS_INTERNAL_ERROR = 6,
        STATUS_INVALID = 7,
        STATUS_CRC_WRONG = 8,
        STATUS_MIFARE_NACK = 9
    };

    typedef struct {
        byte size;
        byte uidByte[10];
        byte sak;
    } Uid;

    typedef struct {
        byte keyByte[6];
    } MIFARE_Key;

    Uid uid;

    MFRC522_I2C(byte addr, int8_t resetPin) {
        uid.size = 4;
        uid.sak = 0x08; // MIFARE Classic 1K
        memset(uid.uidByte, 0xDE, 4);
    }
    
    void PCD_Init() {}
    byte PCD_ReadRegister(byte reg) { return 0x92; } // Version 2.0
    void PCD_SetAntennaGain(byte gain) {}
    void PCD_AntennaOn() {}
    void PCD_AntennaOff() {}
    bool PICC_IsNewCardPresent() { return true; }
    bool PICC_ReadCardSerial() { return true; }
    void PICC_HaltA() {}
    void PCD_StopCrypto1() {}
    
    byte PICC_WakeupA(byte* bufferATQA, byte* bufferSize) { return STATUS_OK; }
    byte PICC_RequestA(byte* bufferATQA, byte* bufferSize) { return STATUS_OK; }

    byte PCD_Authenticate(byte command, byte blockAddr, MIFARE_Key* key, Uid* uid) {
        if (key->keyByte[0] == 0xFF && key->keyByte[1] == 0xFF) return STATUS_OK;
        if (key->keyByte[0] == 0xA0 && key->keyByte[1] == 0xA1) return STATUS_OK;
        return STATUS_ERROR;
    }

    enum PICC_Type : byte {
        PICC_TYPE_UNKNOWN = 0,
        PICC_TYPE_MIFARE_1K = 4
    };

    const char* PICC_GetTypeName(byte type) { return "Mock Tag"; }
    byte PICC_GetType(byte sak) { return PICC_TYPE_MIFARE_1K; }

    static const byte RxGain_max = 0x07 << 4;

    byte PCD_CommunicateWithPICC(byte command, byte waitIRq, byte* sendData, byte sendLen, byte* backData = nullptr, byte* backLen = nullptr) {
        return STATUS_OK;
    }

    byte MIFARE_Read(byte blockAddr, byte* buffer, byte* bufferSize) {
        if (buffer && bufferSize && *bufferSize >= 18) {
            memset(buffer, 0, 18);
        }
        return STATUS_OK;
    }
};

#endif
