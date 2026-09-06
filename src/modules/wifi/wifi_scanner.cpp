/**
 * @file wifi_scanner.cpp
 * @brief WiFi network scanner implementation
 */

#include "wifi_scanner.h"
#include "core/event_bus.h"
#include "config/config.h"

#ifndef UNIT_TEST
#include <WiFi.h>
#include <esp_wifi.h>
#endif

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace adversary {

// NetworkInfo methods
std::string NetworkInfo::getBssidString() const {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return std::string(buf);
}

const char* NetworkInfo::getSecurityString() const {
    switch (security) {
        case WiFiSecurity::OPEN:           return "OPEN";
        case WiFiSecurity::WEP:            return "WEP";
        case WiFiSecurity::WPA_PSK:        return "WPA";
        case WiFiSecurity::WPA2_PSK:       return "WPA2";
        case WiFiSecurity::WPA_WPA2_PSK:   return "WPA/2";
        case WiFiSecurity::WPA2_ENTERPRISE: return "WPA2-E";
        case WiFiSecurity::WPA3_PSK:       return "WPA3";
        case WiFiSecurity::WPA2_WPA3_PSK:  return "WPA2/3";
        default:                           return "???";
    }
}

uint8_t NetworkInfo::getSignalQuality() const {
    // Convert RSSI to percentage
    // Typical range: -100 dBm (worst) to -30 dBm (best)
    if (rssi >= -30) return 100;
    if (rssi <= -100) return 0;
    int quality = 2 * (rssi + 100);
    return static_cast<uint8_t>(quality > 100 ? 100 : quality);
}

// WiFiScanner implementation
WiFiScanner& WiFiScanner::getInstance() {
    static WiFiScanner instance;
    return instance;
}

WiFiScanner::WiFiScanner()
    : m_state(ScannerState::IDLE)
    , m_scanCount(0)
    , m_scanStartTime(0)
    , m_lastScanTime(0)
    , m_continuousInterval(3000)
    , m_initialized(false)
    , m_continuousMode(false)
{
}

WiFiScanner::~WiFiScanner() {
    deinit();
}

#ifndef UNIT_TEST

bool WiFiScanner::init() {
    if (m_initialized) {
        return true;
    }
    
    Serial.println("[WiFiScanner] Initializing...");
    
    // Preserve existing connection if present
    bool wasConnected = WiFi.isConnected();
    String savedSSID;
    String savedPass;
    
    if (wasConnected) {
        Serial.println("[WiFiScanner] Preserving existing connection");
        // WiFi is already in STA mode and connected - don't disconnect
    } else {
        // Set WiFi mode to station for scanning
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
    }
    
    // Configure country for all 13/14 channels (some regions limit to 11)
    // Using manual policy to ensure we scan all channels including 12-14
    wifi_country_t country = {};
    country.cc[0] = 'J';  // Japan allows channels 1-14
    country.cc[1] = 'P';
    country.cc[2] = '\0';
    country.schan = 1;
    country.nchan = 14;   // Scan all 14 channels
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);
    
    // Extend minimum active scan time per channel for better detection
    // Default is ~100ms, we use 300ms to catch slow-responding APs
    WiFi.setScanActiveMinTime(300);
    
    // Allow time for mode change
    delay(100);
    
    m_initialized = true;
    m_state = ScannerState::IDLE;
    
    Serial.println("[WiFiScanner] Ready (CH1-14, 300ms min/ch)");
    return true;
}

void WiFiScanner::deinit() {
    if (!m_initialized) {
        return;
    }
    
    stopScan();
    m_networks.clear();
    m_initialized = false;
    m_state = ScannerState::IDLE;
    
    Serial.println("[WiFiScanner] Deinitialized");
}

bool WiFiScanner::startScan() {
    if (!m_initialized) {
        Serial.println("[WiFiScanner] Error: Not initialized");
        return false;
    }
    
    if (m_state == ScannerState::SCANNING) {
        Serial.println("[WiFiScanner] Scan already in progress");
        return false;
    }
    
    m_scanStartTime = millis();
    
    // Start async scan with all options for maximum detection:
    // async=true, show_hidden=true, passive=true (passive scan catches more APs)
    // channel=0 (all channels), ms_per_chan=300 (extended dwell time)
    
    // ERROR 34 "MISSING_ACKS" Fix: Force Max TX Power ensures Probe Requests reach distant APs
    // Ensure STA mode is active so setTxPower works (avoids "Neither AP or STA has been started")
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    int16_t result = WiFi.scanNetworks(
        true,   // async
        true,   // show_hidden
        true,   // passive (listens longer, better detection)
        300     // 300ms per channel (default is ~100ms)
    );
    
    if (result == WIFI_SCAN_RUNNING) {
        m_state = ScannerState::SCANNING;
        Serial.println("[WiFiScanner] Scan started (passive, 300ms/ch)");
        return true;
    } else if (result == WIFI_SCAN_FAILED) {
        m_state = ScannerState::ERROR;
        Serial.println("[WiFiScanner] Scan failed to start");
        return false;
    }
    
    // Immediate result (shouldn't happen with async)
    m_state = ScannerState::SCANNING;
    return true;
}

bool WiFiScanner::scanSync(uint32_t timeoutMs) {
    if (!m_initialized) {
        return false;
    }
    
    m_scanStartTime = millis();
    m_state = ScannerState::SCANNING;
    
    Serial.println("[WiFiScanner] Starting synchronous scan...");
    
    // Synchronous scan with hidden networks
    int16_t count = WiFi.scanNetworks(false, true);  // async=false, show_hidden=true
    
    if (count < 0) {
        m_state = ScannerState::ERROR;
        Serial.printf("[WiFiScanner] Scan failed: %d\n", count);
        return false;
    }
    
    processResults();
    return true;
}

void WiFiScanner::stopScan() {
    if (m_state == ScannerState::SCANNING) {
        WiFi.scanDelete();
        m_state = ScannerState::IDLE;
        Serial.println("[WiFiScanner] Scan stopped");
    }
}

void WiFiScanner::update() {
    if (!m_initialized) {
        return;
    }
    
    // Check for async scan completion
    if (m_state == ScannerState::SCANNING) {
        int16_t result = WiFi.scanComplete();
        
        if (result >= 0) {
            // Scan complete
            processResults();
            
            // EventBus event published in processResults
        } else if (result == WIFI_SCAN_FAILED) {
            m_state = ScannerState::ERROR;
            Serial.println("[WiFiScanner] Scan failed");
        }
        // WIFI_SCAN_RUNNING means still scanning
    }
    
    // Handle continuous mode
    if (m_continuousMode && m_state == ScannerState::COMPLETED) {
        uint32_t now = millis();
        if (now - m_lastScanTime >= m_continuousInterval) {
            startScan();
        }
    }
}

void WiFiScanner::processResults() {
    int16_t count = WiFi.scanComplete();
    if (count < 0) {
        count = 0;
    }
    
    m_networks.clear();
    m_networks.reserve(count);
    
    for (int i = 0; i < count; i++) {
        NetworkInfo net;
        
        // Get SSID
        String ssid = WiFi.SSID(i);
        net.ssid = ssid.c_str();
        net.isHidden = (ssid.length() == 0);
        
        // Get BSSID
        uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) {
            memcpy(net.bssid, bssid, 6);
        } else {
            memset(net.bssid, 0, 6);
        }
        
        // Get other info
        net.rssi = WiFi.RSSI(i);
        net.channel = WiFi.channel(i);
        net.security = authModeToSecurity(WiFi.encryptionType(i));
        net.lastSeen = millis();
        
        m_networks.push_back(net);
    }
    
    // Clean up scan results
    WiFi.scanDelete();
    
    m_scanCount++;
    m_lastScanTime = millis();
    m_state = ScannerState::COMPLETED;
    
    Serial.printf("[WiFiScanner] Found %d networks in %lu ms\n", 
                  count, m_lastScanTime - m_scanStartTime);
                  
    // Publish scan completion event
    adversary::EventData event(adversary::EventType::WIFI_SCAN_COMPLETED);
    event.payload.scan.count = count;
    event.payload.scan.duration = m_lastScanTime - m_scanStartTime;
    adversary::EventBus::getInstance().publish(event);
}


WiFiSecurity WiFiScanner::authModeToSecurity(uint8_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:
            return WiFiSecurity::OPEN;
        case WIFI_AUTH_WEP:
            return WiFiSecurity::WEP;
        case WIFI_AUTH_WPA_PSK:
            return WiFiSecurity::WPA_PSK;
        case WIFI_AUTH_WPA2_PSK:
            return WiFiSecurity::WPA2_PSK;
        case WIFI_AUTH_WPA_WPA2_PSK:
            return WiFiSecurity::WPA_WPA2_PSK;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return WiFiSecurity::WPA2_ENTERPRISE;
        case WIFI_AUTH_WPA3_PSK:
            return WiFiSecurity::WPA3_PSK;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return WiFiSecurity::WPA2_WPA3_PSK;
        default:
            return WiFiSecurity::UNKNOWN;
    }
}

#else // UNIT_TEST stubs

bool WiFiScanner::init() {
    m_initialized = true;
    m_state = ScannerState::IDLE;
    return true;
}

void WiFiScanner::deinit() {
    m_initialized = false;
}

bool WiFiScanner::startScan() {
    if (!m_initialized) return false;
    m_state = ScannerState::SCANNING;
    return true;
}

bool WiFiScanner::scanSync(uint32_t timeoutMs) {
    if (!m_initialized) return false;
    m_state = ScannerState::COMPLETED;
    m_scanCount++;
    return true;
}

void WiFiScanner::stopScan() {
    m_state = ScannerState::IDLE;
}

void WiFiScanner::update() {
    // No-op for tests
}

void WiFiScanner::processResults() {
    // No-op for tests
}

WiFiSecurity WiFiScanner::authModeToSecurity(uint8_t authMode) {
    return WiFiSecurity::UNKNOWN;
}

#endif // UNIT_TEST

// Common methods (work for both real and test builds)

void WiFiScanner::clearResults() {
    m_networks.clear();
    m_networks.shrink_to_fit();
}

const NetworkInfo* WiFiScanner::getNetwork(size_t index) const {
    if (index >= m_networks.size()) {
        return nullptr;
    }
    return &m_networks[index];
}

void WiFiScanner::sortBySignal() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.rssi > b.rssi;  // Higher RSSI = stronger signal
              });
}

void WiFiScanner::sortByChannel() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.channel < b.channel;
              });
}

void WiFiScanner::sortBySSID() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.ssid < b.ssid;
              });
}

void WiFiScanner::setContinuousMode(bool enable, uint32_t intervalMs) {
    m_continuousMode = enable;
    m_continuousInterval = intervalMs;
    
    if (enable && m_state != ScannerState::SCANNING) {
        startScan();
    }
}

} // namespace adversary
