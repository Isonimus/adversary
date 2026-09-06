/**
 * @file event_types.h
 * @brief Event type definitions for the EventBus system
 * 
 * Defines all event types used for decoupled communication between modules.
 */

#pragma once

#include <cstdint>

namespace adversary {

/**
 * @brief Event types for publish/subscribe communication
 * 
 * Events are grouped by subsystem for organization.
 * Use EventBus::publish() to emit events and EventBus::subscribe() to listen.
 */
enum class EventType : uint16_t {
    // =========================================================================
    // WiFi Scanning (100-109)
    // =========================================================================
    WIFI_SCAN_STARTED = 100,
    WIFI_SCAN_COMPLETED = 101,
    WIFI_NETWORK_FOUND = 102,
    
    // =========================================================================
    // Attacks (200-219)
    // =========================================================================
    ATTACK_STARTED = 200,
    ATTACK_STOPPED = 201,
    ATTACK_STATE_CHANGED = 202,
    DEAUTH_PACKET_SENT = 203,
    BEACON_SPAM_TICK = 204,
    PROBE_FLOOD_TICK = 205,
    
    // =========================================================================
    // Handshake Capture (220-229)
    // =========================================================================
    HANDSHAKE_CAPTURED = 220,
    HANDSHAKE_SAVED = 221,
    PMKID_CAPTURED = 222,
    EAPOL_CAPTURED = 223,
    
    // =========================================================================
    // Evil Twin / Karma AP (230-239)
    // =========================================================================
    CREDENTIAL_CAPTURED = 230,
    CLIENT_CONNECTED = 231,
    CLIENT_DISCONNECTED = 232,
    PROBE_REQUEST_RECEIVED = 233,
    AP_STARTED = 234,
    AP_STOPPED = 235,
    
    // =========================================================================
    // Sniffer (250-259)
    // =========================================================================
    PACKET_CAPTURED = 250,
    SNIFFER_STARTED = 251,
    SNIFFER_STOPPED = 252,
    
    // =========================================================================
    // BLE (300-319)
    // =========================================================================
    BLE_DEVICE_FOUND = 300,
    BLE_SCAN_STARTED = 301,
    BLE_SCAN_STOPPED = 302,
    BLE_ATTACK_STARTED = 303,
    BLE_ATTACK_STOPPED = 304,
    BLE_CONNECTED = 305,
    BLE_DISCONNECTED = 306,
    
    // =========================================================================
    // RFID (320-329)
    // =========================================================================
    RFID_TAG_DETECTED = 320,
    RFID_TAG_LOST = 321,
    RFID_AUDIT_PROGRESS = 322,
    RFID_AUDIT_COMPLETED = 323,
    RFID_KEY_FOUND = 324,
    
    // =========================================================================
    // Wardriving (330-339)
    // =========================================================================
    WARDRIVING_STARTED = 330,
    WARDRIVING_STOPPED = 331,
    WARDRIVING_NETWORK_LOGGED = 332,
    
    // =========================================================================
    // System (400-419)
    // =========================================================================
    STATE_CHANGED = 400,
    GPS_FIX_ACQUIRED = 401,
    GPS_FIX_LOST = 402,
    BATTERY_LOW = 403,
    BATTERY_CRITICAL = 404,
    SD_ERROR = 405,
    SD_READY = 406,
    WIFI_CONNECTED = 407,
    WIFI_DISCONNECTED = 408,
    
    // =========================================================================
    // UI (420-429)
    // =========================================================================
    SCREEN_CHANGED = 420,
    TOAST_SHOWN = 421,
    MENU_ACTION = 422,
    
    // =========================================================================
    // Infrared (430-439)
    // =========================================================================
    IR_TRANSMISSION_STARTED = 430,
    IR_TRANSMISSION_COMPLETED = 431,
    
    // Sentinel for array sizing
    _EVENT_COUNT = 500
};

/**
 * @brief Get human-readable name for an event type
 * @param type Event type to convert
 * @return String name of the event type
 */
const char* eventTypeToString(EventType type);

} // namespace adversary
