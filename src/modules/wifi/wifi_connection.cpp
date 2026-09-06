/**
 * @file wifi_connection.cpp
 * @brief WiFi Station (STA) connection manager implementation
 */

#include "wifi_connection.h"
#include "modules/storage/settings_manager.h"
#include <cstdio>

#ifdef ESP32
#include "ui/components/toast_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "core/event_bus.h"
#include "core/event_types.h"
#endif

namespace adversary {

WiFiConnection::WiFiConnection() {
    // Initial state
    state_ = ConnectionState::DISCONNECTED;
    ssid_[0] = '\0';
    password_[0] = '\0';
}

bool WiFiConnection::connect(const char* ssid, const char* password, 
                              uint8_t channel, bool save) {
    if (!ssid || strlen(ssid) == 0) {
#ifdef ESP32
        Serial.println("[WiFiConnection] Error: Empty SSID");
#endif
        return false;
    }
    
    // Store credentials (needed for both ESP32 and tests)
    strncpy(ssid_, ssid, 32);
    ssid_[32] = '\0';
    
    if (password) {
        strncpy(password_, password, 64);
        password_[64] = '\0';
    } else {
        password_[0] = '\0';
    }
    
    channel_ = channel;
    saveOnConnect_ = save;
    
#ifdef ESP32
    Serial.printf("[WiFiConnection] Connecting to %s...\n", ssid_);

    // CRITICAL FIX: Ensure clean state before connecting
    // 1. Disable promiscuous mode (leftover from attacks/sniffer)
    esp_wifi_set_promiscuous(false);
    
    // 2. Force disconnect and clear config to reset internal state machine
    WiFi.disconnect(true); 
    delay(100); 

    // Ensure WiFi is in STA mode (or STA+AP if already in AP mode)
    wifi_mode_t currentMode = WiFi.getMode();
    if (currentMode == WIFI_MODE_AP) {
        WiFi.mode(WIFI_AP_STA);
    } else if (currentMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
    }
    
    // Start connection
    if (channel > 0) {
        // Use channel hint if provided
        WiFi.begin(ssid_, password_, channel_);
    } else {
        WiFi.begin(ssid_, password_);
    }

    // ERROR 34 "MISSING_ACKS" Fix: Force Max TX Power
    // Moved AFTER begin() to avoid "Neither AP or STA has been started" error
    // This helps when connecting to APs that are far away or have interference
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    connectStartTime_ = millis();
    setState(ConnectionState::CONNECTING);
    
    return true;
#else
    // In native builds, immediately save if requested (simulate connection success)
    if (save) {
        saveCredentials();
    }
    return true;
#endif
}

void WiFiConnection::disconnect() {
#ifdef ESP32
    Serial.println("[WiFiConnection] Disconnecting...");
    
    // Use disconnect(true) with a safety check to avoid ESP_ERR_WIFI_NOT_INIT
    if (WiFi.getMode() != WIFI_MODE_NULL) {
        WiFi.disconnect(true);
    }
    
    // Return to AP-only mode if we were in AP+STA
    wifi_mode_t currentMode = WiFi.getMode();
    if (currentMode == WIFI_AP_STA) {
        WiFi.mode(WIFI_AP);
    }
    
    setState(ConnectionState::DISCONNECTED);
    ssid_[0] = '\0';
    password_[0] = '\0';
#endif
}

bool WiFiConnection::isConnected() const {
#ifdef ESP32
    return WiFi.isConnected() && state_ == ConnectionState::CONNECTED;
#else
    return false;
#endif
}

bool WiFiConnection::isConnectedTo(const char* ssid) const {
#ifdef ESP32
    if (!isConnected() || !ssid) return false;
    return strcmp(WiFi.SSID().c_str(), ssid) == 0;
#else
    (void)ssid;
    return false;
#endif
}

void WiFiConnection::update() {
#ifdef ESP32
    if (state_ != ConnectionState::CONNECTING) {
        // Check for unexpected disconnection
        if (state_ == ConnectionState::CONNECTED && !WiFi.isConnected()) {
            Serial.println("[WiFiConnection] Connection lost");
            setState(ConnectionState::DISCONNECTED);
        }
        return;
    }
    
    // Check if connected
    if (WiFi.isConnected()) {
        Serial.printf("[WiFiConnection] Connected! IP: %s\n", 
                      WiFi.localIP().toString().c_str());
        setState(ConnectionState::CONNECTED);
        
        // Save credentials if requested
        if (saveOnConnect_) {
            saveCredentials();
        }
        return;
    }
    
    // Check for timeout
    uint32_t elapsed = millis() - connectStartTime_;
    if (elapsed >= CONNECT_TIMEOUT_MS) {
        Serial.println("[WiFiConnection] Connection timeout");
        WiFi.disconnect(true);
        setState(ConnectionState::TIMEOUT);
        return;
    }
    
    // Check for failure (status gives more info)
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
        Serial.printf("[WiFiConnection] Connection failed: %d\n", status);
        WiFi.disconnect(true);
        setState(ConnectionState::FAILED);
    }
#endif
}

void WiFiConnection::setState(ConnectionState newState) {
    if (state_ == newState) return;
    
    ConnectionState oldState = state_;
    state_ = newState;
    
#ifdef ESP32
    Serial.printf("[WiFiConnection] State: %s -> %s\n",
                  getConnectionStateName(oldState),
                  getConnectionStateName(newState));
    
    // Show toast notification based on state
    char toastMsg[64];
    switch (newState) {
        case ConnectionState::CONNECTED:
            snprintf(toastMsg, sizeof(toastMsg), "Connected to %s", ssid_);
            ToastManager::getInstance().show(toastMsg, ToastType::SUCCESS, ToastPriority::PRIORITY_HIGH);
            EventBus::getInstance().publish(EventData(EventType::WIFI_CONNECTED));
            break;
        case ConnectionState::FAILED:
            ToastManager::getInstance().show("Connection failed", ToastType::ERROR, ToastPriority::PRIORITY_HIGH);
            break;
        case ConnectionState::TIMEOUT:
            ToastManager::getInstance().show("Connection timeout", ToastType::ERROR, ToastPriority::PRIORITY_HIGH);
            break;
        case ConnectionState::DISCONNECTED:
            if (oldState == ConnectionState::CONNECTED) {
                ToastManager::getInstance().show("WiFi disconnected", ToastType::WARNING, ToastPriority::PRIORITY_MEDIUM);
                EventBus::getInstance().publish(EventData(EventType::WIFI_DISCONNECTED));
            }
            break;
        default:
            break;
    }
#else
    (void)oldState;  // Suppress unused warning
#endif
    
    // Notify callback
    if (onConnectionResult_) {
        bool success = (newState == ConnectionState::CONNECTED);
        onConnectionResult_(success, newState);
    }
}

// =============================================================================
// Credential Persistence
// =============================================================================

void WiFiConnection::saveCredentials() {
    auto& settings = SettingsManager::getInstance();
    settings.addWiFiCredential(ssid_, password_);
    settings.save();
#ifdef ESP32
    Serial.printf("[WiFiConnection] Credentials saved for %s\n", ssid_);
#endif
}

void WiFiConnection::forgetCredentials() {
    auto& settings = SettingsManager::getInstance();
    // This now clears ALL credentials if called without arguments
    // Or we could change it to take an SSID, but for legacy compatibility
    // we'll keep it as "clear all" or just remove the most recent one.
    // Given the new UI, this method might be less used.
    settings.clearWiFiCredentials();
    settings.save();
#ifdef ESP32
    Serial.println("[WiFiConnection] All saved credentials cleared");
#endif
}

bool WiFiConnection::hasSavedCredentials() const {
#ifdef ESP32
    return !SettingsManager::getInstance().getSavedCredentials().empty();
#else
    return false;
#endif
}

const char* WiFiConnection::getSavedSSID() const {
#ifdef ESP32
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    if (!creds.empty()) return creds.back().ssid;
    return "";
#else
    return "";
#endif
}

const char* WiFiConnection::getSavedPassword() const {
#ifdef ESP32
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    if (!creds.empty()) return creds.back().password;
    return "";
#else
    return "";
#endif
}

void WiFiConnection::autoConnect() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance();
    const auto& creds = settings.getSavedCredentials();
    if (creds.empty()) {
        Serial.println("[WiFiConnection] No saved credentials for auto-connect");
        return;
    }
    
    // Auto-connect to the last successful network
    const auto& last = creds.back();
    Serial.printf("[WiFiConnection] Auto-connecting to %s\n", last.ssid);
    connect(last.ssid, last.password, 0, false);  // Don't re-save
#endif
}

#ifdef ESP32
IPAddress WiFiConnection::getLocalIP() const {
    if (isConnected()) {
        return WiFi.localIP();
    }
    return IPAddress(0, 0, 0, 0);
}

int8_t WiFiConnection::getRSSI() const {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}
#endif

} // namespace adversary
