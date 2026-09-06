/**
 * @file event_data.h
 * @brief Event payload definitions for the EventBus system
 * 
 * Contains the EventData struct with typed payloads for different event types.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "event_types.h"
#include "attack_type_id.h"

namespace adversary {

/**
 * @brief WiFi network event payload
 */
struct NetworkEventData {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;  // 0=Open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
};

/**
 * @brief Scan completion event payload
 */
struct ScanEventData {
    uint32_t count;
    uint32_t duration;
};

/**
 * @brief Handshake event payload
 */
struct HandshakeEventData {
    char filename[64];
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t type;  // 0=4WAY, 1=PMKID, 2=EAPOL
};

/**
 * @brief Credential capture event payload
 */
struct CredentialEventData {
    char ssid[33];
    char username[64];
    char password[64];
    uint8_t clientMac[6];
};

/**
 * @brief Attack state event payload
 */
struct AttackStateEventData {
    uint8_t attackType;  // Module-specific attack type enum
    int oldState;
    int newState;
    uint32_t packetCount;
};

/**
 * @brief Packet event payload (lightweight, for high-frequency events)
 */
struct PacketEventData {
    uint32_t count;
    uint32_t rate;  // Packets per second
    uint8_t type;   // Packet type filter
};

/**
 * @brief Progress event payload
 */
struct ProgressEventData {
    uint8_t percent;
    char status[32];
};

/**
 * @brief RFID tag event payload
 */
struct RFIDEventData {
    uint8_t uid[10];
    uint8_t uidLen;
    uint8_t tagType;
    uint8_t atqa[2];
    uint8_t sak;
};

/**
 * @brief BLE device event payload
 */
struct BLEEventData {
    char name[32];
    uint8_t address[6];
    int8_t rssi;
    uint8_t addressType;
};

/**
 * @brief GPS event payload
 */
struct GPSEventData {
    double latitude;
    double longitude;
    float altitude;
    float speed;
    uint8_t satellites;
    float hdop;
};

/**
 * @brief Client connection event payload
 */
struct ClientEventData {
    uint8_t mac[6];
    char hostname[32];
    uint32_t ip;
};

/**
 * @brief State change event payload
 */
struct StateEventData {
    int oldState;
    int newState;
    const char* reason;
};

/**
 * @brief Screen change event payload
 */
struct ScreenEventData {
    int fromScreen;
    int toScreen;
};


/**
 * @brief Event data container with typed payloads
 * 
 * Uses a union for memory efficiency. Access the appropriate payload
 * field based on the event type.
 */
struct EventData {
    EventType type;
    uint32_t timestamp;  // millis() when event was created
    
    union {
        NetworkEventData network;
        ScanEventData scan;
        HandshakeEventData handshake;
        CredentialEventData credential;
        AttackStateEventData attack;
        PacketEventData packet;
        ProgressEventData progress;
        RFIDEventData rfid;
        BLEEventData ble;
        GPSEventData gps;
        ClientEventData client;
        StateEventData state;
        ScreenEventData screen;

        void* custom;  // For extensibility
    } payload;
    
    /**
     * @brief Create an empty event
     */
    EventData() : type(EventType::STATE_CHANGED), timestamp(0) {
        memset(&payload, 0, sizeof(payload));
    }
    
    /**
     * @brief Create an event with type and auto-timestamp
     */
    explicit EventData(EventType t);
};

} // namespace adversary
