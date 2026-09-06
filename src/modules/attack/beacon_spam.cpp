/**
 * @file beacon_spam.cpp
 * @brief Beacon Spam attack implementation
 */

#include "beacon_spam.h"
#include "../../core/event_bus.h"
#include "../../utils/wifi_utils.h"
#include "../../utils/raw_wifi.h"
#include <cstdlib>

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_err.h>
#else
#include "../../test/common/arduino_mocks.h"
#endif

namespace adversary {

// SSID lists in flash — no heap at boot; vector is built only when attack starts
static const char* const RICKROLL_LYRICS[] = {
    "Never Gonna Give You Up",
    "Never Gonna Let You Down",
    "Never Gonna Run Around",
    "And Desert You",
    "Never Gonna Make You Cry",
    "Never Gonna Say Goodbye",
    "Never Gonna Tell A Lie",
    "And Hurt You"
};

static const char* const FUNNY_SSIDS[] = {
    "FBI Surveillance Van",
    "Pretty Fly for a WiFi",
    "Tell My WiFi Love Her",
    "Silence of the LANs",
    "404 Network Unavailable",
    "Abraham Linksys",
    "Martin Router King",
    "The Promised LAN",
    "LAN of Milk and Honey",
    "The LAN Before Time",
    "Panic At The Cisco",
    "It Burns When IP",
    "No More Mr WiFi",
    "Drop It Like Its Hotspot",
    "Get Off My LAN",
    "Wu-Tang LAN",
    "Bill Wi the Science Fi",
    "I Believe Wi Can Fi",
    "Loading...",
    "Connecting...",
    "Searching...",
    "Virus.exe",
    "Virus Distribution Center",
    "Free Virus Here",
    "HACK ME IF YOU CAN"
};

static const char* const OFFENSIVE_SSIDS[] = {
    "Unhappy WiFi",
    "Get Your Own WiFi",
    "Stop Using My WiFi",
    "Toilet Cam #3",
    "Surveillance Camera",
    "Hidden Camera 4",
    "Creeper Van",
    "Mobile Hotspot",
    "Definitely Not FBI"
};

static std::vector<std::string> makeList(const char* const* arr, size_t n) {
    std::vector<std::string> v;
    v.reserve(n);
    for (size_t i = 0; i < n; i++) v.emplace_back(arr[i]);
    return v;
}

std::vector<std::string> getSSIDList(BeaconSpamMode mode) {
    switch (mode) {
        case BeaconSpamMode::RICKROLL:
            return makeList(RICKROLL_LYRICS,  sizeof(RICKROLL_LYRICS)  / sizeof(*RICKROLL_LYRICS));
        case BeaconSpamMode::FUNNY:
            return makeList(FUNNY_SSIDS,      sizeof(FUNNY_SSIDS)      / sizeof(*FUNNY_SSIDS));
        case BeaconSpamMode::OFFENSIVE:
            return makeList(OFFENSIVE_SSIDS,  sizeof(OFFENSIVE_SSIDS)  / sizeof(*OFFENSIVE_SSIDS));
        default:
            return {};
    }
}

const char* getBeaconSpamModeName(BeaconSpamMode mode) {
    switch (mode) {
        case BeaconSpamMode::SINGLE_SSID:   return "Single SSID";
        case BeaconSpamMode::RANDOM_SSIDS:  return "Random";
        case BeaconSpamMode::SSID_LIST:     return "Custom List";
        case BeaconSpamMode::RICKROLL:      return "Rick Roll";
        case BeaconSpamMode::FUNNY:         return "Funny";
        case BeaconSpamMode::OFFENSIVE:     return "Offensive";
        default:                            return "Unknown";
    }
}

BeaconSpam& BeaconSpam::getInstance() {
    if (!s_instance_) {
        s_instance_ = new BeaconSpam();
    }
    return *s_instance_;
}

BeaconSpam::BeaconSpam()
    : state_(BeaconSpamState::IDLE)
    , currentSsidIndex_(0)
    , lastBeaconTime_(0)
    , lastHopTime_(0)
    , hopChannelIndex_(0)
    , sequenceNumber_(0)
    , initialized_(false)
{
    memset(currentSsid_, 0, sizeof(currentSsid_));
    memset(currentBssid_, 0, sizeof(currentBssid_));
}

bool BeaconSpam::init() {
    if (initialized_) return true;
    
#ifdef ESP32
    Serial.println("[BeaconSpam] Initializing...");
#endif
    
    initialized_ = true;
    return true;
}

void BeaconSpam::deinit() {
    stop();
    initialized_ = false;
}

bool BeaconSpam::start(const BeaconSpamConfig& config) {
    if (!initialized_) {
        if (!init()) return false;
    }
    
    if (state_ == BeaconSpamState::RUNNING) {
        stop();
    }
    
    config_ = config;
    stats_.reset();
    currentSsidIndex_ = 0;
    sequenceNumber_ = 0;
    hopChannelIndex_ = 0;
    lastHopTime_ = millis();
    
    // Load SSID list based on mode
    switch (config_.mode) {
        case BeaconSpamMode::SINGLE_SSID:
            ssidList_.clear();
            ssidList_.push_back(config_.ssid);
            break;
            
        case BeaconSpamMode::RANDOM_SSIDS:
            ssidList_.clear();
            // Will generate random SSIDs on the fly
            break;
            
        case BeaconSpamMode::SSID_LIST:
            // Use custom list set via setCustomSSIDs()
            if (ssidList_.empty()) {
                ssidList_.push_back("Custom SSID");
            }
            break;
            
        case BeaconSpamMode::RICKROLL:
        case BeaconSpamMode::FUNNY:
        case BeaconSpamMode::OFFENSIVE:
            ssidList_ = getSSIDList(config_.mode);
            break;
    }
    
    stats_.ssidsUsed = ssidList_.size();
    
#ifdef ESP32
    // Set WiFi to station mode for injection
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    // Set channel
    wifi_utils::setChannel(config_.channel);
    
    stats_.startTime = millis();
    
    Serial.printf("[BeaconSpam] Started on channel %d\n", config_.channel);
    Serial.printf("[BeaconSpam] Mode: %s (%zu SSIDs)\n", 
                  getBeaconSpamModeName(config_.mode), ssidList_.size());
#endif
    
    setState(BeaconSpamState::RUNNING);
    lastBeaconTime_ = millis();
    
    return true;
}

void BeaconSpam::stop() {
    if (state_ == BeaconSpamState::IDLE) return;
    
#ifdef ESP32
    // Release WiFi memory
    WiFi.mode(WIFI_OFF);
    delay(50);
    
    Serial.printf("[BeaconSpam] Stopped. Sent %u beacons\n", stats_.beaconsSent);
#endif
    
    setState(BeaconSpamState::IDLE);
}

void BeaconSpam::togglePause() {
    if (state_ == BeaconSpamState::RUNNING) {
        setState(BeaconSpamState::PAUSED);
    } else if (state_ == BeaconSpamState::PAUSED) {
        setState(BeaconSpamState::RUNNING);
    }
}

void BeaconSpam::update() {
    if (state_ != BeaconSpamState::RUNNING) return;
    
#ifdef ESP32
    uint32_t now = millis();
    stats_.duration = now - stats_.startTime;
    
    // Handle channel hopping
    if (config_.channelHopEnabled) {
        if (now - lastHopTime_ >= config_.channelHopIntervalMs) {
            hopChannel();
            lastHopTime_ = now;
        }
    }
    
    // Check if it's time to send next beacon/burst
    if (now - lastBeaconTime_ >= config_.intervalMs) {
        if (config_.burstMode && !ssidList_.empty()) {
            // Burst mode: send ALL SSIDs in rapid succession
            sendBurst();
        } else {
            // Normal mode: send one beacon
            sendBeacon();
        }
        lastBeaconTime_ = now;
        
        // Check if we've hit max beacons
        if (config_.maxBeacons > 0 && stats_.beaconsSent >= config_.maxBeacons) {
            setState(BeaconSpamState::COMPLETED);
        }
    }
#endif
}

void BeaconSpam::sendBeacon() {
#ifdef ESP32
    // Get next SSID
    const char* ssid = getNextSSID();
    
    // Generate or use BSSID
    if (config_.randomizeBssid) {
        generateRandomBssid(currentBssid_);
    } else {
        memcpy(currentBssid_, config_.baseBssid, 6);
        // Increment last byte for variation
        currentBssid_[5] = (stats_.beaconsSent & 0xFF);
    }
    
    // Build beacon frame
    uint8_t buffer[MAX_BEACON_SIZE];
    size_t frameSize = buildBeaconFrame(buffer, sizeof(buffer), 
                                       ssid, currentBssid_,
                                       config_.channel, config_.encryptedNetwork);
    
    if (frameSize > 0) {
        // Send frame via RawWiFi utility
        if (RawWiFi::transmit(WiFiInterface::STATION, buffer, frameSize, false)) {
            stats_.beaconsSent++;
        }
    }
#endif
}

void BeaconSpam::sendBurst() {
#ifdef ESP32
    // Send ALL SSIDs in the list in rapid succession
    size_t listSize = ssidList_.size();
    if (listSize == 0) {
        sendBeacon();  // Fallback to single beacon
        return;
    }
    
    for (size_t i = 0; i < listSize; i++) {
        const char* ssid = getSSIDAt(i);
        
        // Generate unique BSSID for each SSID in burst
        if (config_.randomizeBssid) {
            generateRandomBssid(currentBssid_);
        } else {
            memcpy(currentBssid_, config_.baseBssid, 6);
            // Use index to vary BSSID so each SSID has unique BSSID
            currentBssid_[5] = (uint8_t)i;
        }
        
        // Build and send beacon frame
        uint8_t buffer[MAX_BEACON_SIZE];
        size_t frameSize = buildBeaconFrame(buffer, sizeof(buffer), 
                                           ssid, currentBssid_,
                                           config_.channel, config_.encryptedNetwork);
        
        if (frameSize > 0) {
            if (RawWiFi::transmit(WiFiInterface::STATION, buffer, frameSize, false)) {
                stats_.beaconsSent++;
            }
        }
        
        // Small delay between beacons in burst to avoid overwhelming TX queue
        if (config_.burstDelayMs > 0 && i < listSize - 1) {
            delay(config_.burstDelayMs);
        }
    }
#endif
}

void BeaconSpam::generateRandomBssid(uint8_t* bssid) {
    for (int i = 0; i < 6; i++) {
        bssid[i] = random(0, 256);
    }
    // Set locally administered bit
    bssid[0] = (bssid[0] & 0xFE) | 0x02;
}

const char* BeaconSpam::getNextSSID() {
    if (config_.mode == BeaconSpamMode::RANDOM_SSIDS) {
        // Generate random SSID
        char randomSsid[33];
        int len = random(8, 32);
        for (int i = 0; i < len; i++) {
            randomSsid[i] = random(32, 127);  // Printable ASCII
        }
        randomSsid[len] = '\0';
        strncpy(currentSsid_, randomSsid, 32);
        currentSsid_[32] = '\0';
        return currentSsid_;
    }
    
    if (ssidList_.empty()) {
        strcpy(currentSsid_, "Adversary");
        return currentSsid_;
    }
    
    // Cycle through list
    const std::string& ssid = ssidList_[currentSsidIndex_];
    strncpy(currentSsid_, ssid.c_str(), 32);
    currentSsid_[32] = '\0';
    
    currentSsidIndex_ = (currentSsidIndex_ + 1) % ssidList_.size();
    
    return currentSsid_;
}

const char* BeaconSpam::getSSIDAt(size_t index) {
    if (ssidList_.empty() || index >= ssidList_.size()) {
        strcpy(currentSsid_, "Adversary");
        return currentSsid_;
    }
    
    const std::string& ssid = ssidList_[index];
    strncpy(currentSsid_, ssid.c_str(), 32);
    currentSsid_[32] = '\0';
    
    return currentSsid_;
}

void BeaconSpam::setCustomSSIDs(const std::vector<std::string>& ssids) {
    ssidList_ = ssids;
    stats_.ssidsUsed = ssidList_.size();
}

void BeaconSpam::hopChannel() {
#ifdef ESP32
    hopChannelIndex_ = (hopChannelIndex_ + 1) % HOP_CHANNEL_COUNT;
    config_.channel = HOP_CHANNELS[hopChannelIndex_];
    wifi_utils::setChannel(config_.channel);
#endif
}

void BeaconSpam::setState(BeaconSpamState newState) {
    if (state_ == newState) return;
    
    BeaconSpamState oldState = state_;
    state_ = newState;
    
    // Publish state change event
    EventData event(EventType::ATTACK_STATE_CHANGED);
    event.payload.attack.attackType = static_cast<uint8_t>(AttackTypeId::BEACON_SPAM);
    event.payload.attack.oldState = static_cast<int>(oldState);
    event.payload.attack.newState = static_cast<int>(newState);
    event.payload.attack.packetCount = stats_.beaconsSent;
    EventBus::getInstance().publish(event);
    

}

size_t BeaconSpam::buildBeaconFrame(uint8_t* buffer, size_t bufferSize,
                                   const char* ssid, const uint8_t* bssid,
                                   uint8_t channel, bool encrypted) {
    if (!buffer || !ssid || !bssid || bufferSize < 128) {
        return 0;
    }
    
    size_t pos = 0;
    
    // Frame Control (0x80 0x00 = Management, Beacon)
    buffer[pos++] = 0x80;
    buffer[pos++] = 0x00;
    
    // Duration
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    
    // Destination Address (broadcast)
    for (int i = 0; i < 6; i++) {
        buffer[pos++] = 0xFF;
    }
    
    // Source Address (BSSID)
    memcpy(&buffer[pos], bssid, 6);
    pos += 6;
    
    // BSSID
    memcpy(&buffer[pos], bssid, 6);
    pos += 6;
    
    // Sequence Control (fragment number 0, sequence number 0)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    
    // === Beacon Frame Body ===
    
    // Timestamp (8 bytes) - can be zero
    for (int i = 0; i < 8; i++) {
        buffer[pos++] = 0x00;
    }
    
    // Beacon Interval (0x64 0x00 = 100 TUs = ~102.4ms)
    buffer[pos++] = 0x64;
    buffer[pos++] = 0x00;
    
    // Capability Information
    uint16_t capability = 0x0001;  // ESS (infrastructure mode)
    if (encrypted) {
        capability |= 0x0010;  // Privacy bit (encrypted)
    }
    buffer[pos++] = capability & 0xFF;
    buffer[pos++] = (capability >> 8) & 0xFF;
    
    // === Information Elements ===
    
    // SSID Parameter Set (Tag 0)
    uint8_t ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;
    buffer[pos++] = 0x00;       // Tag: SSID
    buffer[pos++] = ssidLen;    // Length
    memcpy(&buffer[pos], ssid, ssidLen);
    pos += ssidLen;
    
    // Supported Rates (Tag 1)
    buffer[pos++] = 0x01;       // Tag: Supported Rates
    buffer[pos++] = 0x08;       // Length
    buffer[pos++] = 0x82;       // 1 Mbps (basic)
    buffer[pos++] = 0x84;       // 2 Mbps (basic)
    buffer[pos++] = 0x8B;       // 5.5 Mbps (basic)
    buffer[pos++] = 0x96;       // 11 Mbps (basic)
    buffer[pos++] = 0x0C;       // 6 Mbps
    buffer[pos++] = 0x12;       // 9 Mbps
    buffer[pos++] = 0x18;       // 12 Mbps
    buffer[pos++] = 0x24;       // 18 Mbps
    
    // DS Parameter Set (Tag 3) - Current Channel
    buffer[pos++] = 0x03;       // Tag: DS Parameter Set
    buffer[pos++] = 0x01;       // Length
    buffer[pos++] = channel;    // Current channel
    
    // Extended Supported Rates (Tag 50)
    buffer[pos++] = 0x32;       // Tag: Extended Supported Rates
    buffer[pos++] = 0x04;       // Length
    buffer[pos++] = 0x30;       // 24 Mbps
    buffer[pos++] = 0x48;       // 36 Mbps
    buffer[pos++] = 0x60;       // 48 Mbps
    buffer[pos++] = 0x6C;       // 54 Mbps
    
    // RSN Information Element (Tag 48) - if encrypted
    if (encrypted) {
        buffer[pos++] = 0x30;   // Tag: RSN
        buffer[pos++] = 0x14;   // Length (20 bytes)
        buffer[pos++] = 0x01;   // RSN Version 1
        buffer[pos++] = 0x00;
        // Group Cipher Suite (CCMP)
        buffer[pos++] = 0x00; buffer[pos++] = 0x0F; buffer[pos++] = 0xAC; buffer[pos++] = 0x04;
        // Pairwise Cipher Suite Count = 1
        buffer[pos++] = 0x01; buffer[pos++] = 0x00;
        // Pairwise Cipher Suite (CCMP)
        buffer[pos++] = 0x00; buffer[pos++] = 0x0F; buffer[pos++] = 0xAC; buffer[pos++] = 0x04;
        // AKM Suite Count = 1
        buffer[pos++] = 0x01; buffer[pos++] = 0x00;
        // AKM Suite (PSK)
        buffer[pos++] = 0x00; buffer[pos++] = 0x0F; buffer[pos++] = 0xAC; buffer[pos++] = 0x02;
        // RSN Capabilities
        buffer[pos++] = 0x00; buffer[pos++] = 0x00;
    }
    
    return pos;
}

// Static member initialization
BeaconSpam* BeaconSpam::s_instance_ = nullptr;

} // namespace adversary
