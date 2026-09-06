#pragma once

/**
 * @file ap_types.h
 * @brief Shared types for Access Point and Captive Portal modules
 * 
 * Contains common structures and typedefs used across AP, Evil Twin,
 * Karma, and Captive Portal implementations.
 */

#include <cstdint>
#include <cstring>
#include <functional>

namespace ap {

// ============================================================================
// Credential Capture Types
// ============================================================================

/**
 * @brief Captured credential entry
 */
struct CapturedCredential {
    char username[64] = {0};
    char password[64] = {0};
    char clientIP[16] = {0};
    uint32_t timestamp = 0;
    
    void setUsername(const char* u) {
        strncpy(username, u, sizeof(username) - 1);
    }
    
    void setPassword(const char* p) {
        strncpy(password, p, sizeof(password) - 1);
    }
    
    void setClientIP(const char* ip) {
        strncpy(clientIP, ip, sizeof(clientIP) - 1);
    }
};

/**
 * @brief Callback for credential capture events
 */
using CredentialCallback = std::function<void(const CapturedCredential& cred)>;

// ============================================================================
// Portal Configuration Types
// ============================================================================

/**
 * @brief Portal page type for built-in templates
 */
enum class PortalPageType {
    GENERIC_LOGIN,      // Generic "Enter credentials to connect"
    WIFI_LOGIN,         // "WiFi requires authentication"
    SOCIAL_FACEBOOK,    // Facebook-style login
    SOCIAL_GOOGLE,      // Google-style login
    CORPORATE,          // Corporate network login
    CUSTOM              // Custom HTML from SD card
};

/**
 * @brief Configuration for captive portal
 */
struct CaptivePortalConfig {
    PortalPageType pageType = PortalPageType::GENERIC_LOGIN;
    char customTitle[64] = "Network Login";
    char successMessage[128] = "Connected! You may now use the network.";
    char customHtmlPath[64] = {0};  // Path on SD for CUSTOM type (deprecated)
    char templateName[32] = {0};    // Template name for SD_TEMPLATE type
    bool enableCaptivePortal = true;  // false = passthrough mode (no portal)
    bool logToSD = true;
    char logPath[64] = "/adversary/captures/creds.txt";
    
    void setTitle(const char* t) {
        strncpy(customTitle, t, sizeof(customTitle) - 1);
    }
    
    void setSuccessMessage(const char* m) {
        strncpy(successMessage, m, sizeof(successMessage) - 1);
    }
    
    void setTemplateName(const char* n) {
        strncpy(templateName, n, sizeof(templateName) - 1);
    }
};

// ============================================================================
// Client Connection Types
// ============================================================================

/**
 * @brief Connected client information
 */
struct APClient {
    uint8_t mac[6] = {0};
    int8_t rssi = 0;
    uint32_t connectedTime = 0;    // Timestamp when connected
    
    bool isValid() const {
        for (int i = 0; i < 6; i++) {
            if (mac[i] != 0) return true;
        }
        return false;
    }
};
// ============================================================================
// AP Configuration Types
// ============================================================================

/**
 * @brief Soft AP configuration
 */
struct APConfig {
    char ssid[33] = {0};
    char password[64] = {0};   // Empty = open network
    uint8_t channel = 1;
    uint8_t maxConnections = 4;
    bool hidden = false;
    
    void setSSID(const char* s) {
        strncpy(ssid, s, sizeof(ssid) - 1);
    }
    
    void setPassword(const char* p) {
        strncpy(password, p, sizeof(password) - 1);
    }
    
    bool isOpen() const {
        return password[0] == '\0';
    }
};

} // namespace ap
