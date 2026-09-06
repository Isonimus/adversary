#pragma once

#include "../ap/ap_types.h"
#include "../ap/arduino_captive.h"
#include <cstdint>
#include <vector>
#include <functional>
#include <map>
#include <string>

#ifndef UNIT_TEST
#include <esp_wifi_types.h>
#endif

namespace attack {

/**
 * @brief Captured probe request information
 */
struct ProbeRequest {
    uint8_t clientMAC[6] = {0};    // Client MAC address
    char ssid[33] = {0};           // Requested SSID
    int8_t rssi = 0;               // Signal strength
    uint32_t timestamp = 0;        // When captured
    uint16_t count = 1;            // Times seen
    
    bool matchesMAC(const uint8_t* mac) const {
        for (int i = 0; i < 6; i++) {
            if (clientMAC[i] != mac[i]) return false;
        }
        return true;
    }
    
    bool matchesSSID(const char* s) const {
        return strcmp(ssid, s) == 0;
    }
};

/**
 * @brief Karma AP state
 */
enum class KarmaState {
    IDLE,
    LISTENING,      // Only capturing probes, not responding
    ACTIVE,         // Responding to probes with matching beacons
    ERROR
};

/**
 * @brief Karma AP attack statistics
 */
struct KarmaStats {
    uint32_t probesCaptured = 0;       // Total probes captured
    uint32_t uniqueClients = 0;        // Unique client MACs seen
    uint32_t uniqueSSIDs = 0;          // Unique SSIDs requested
    uint32_t beaconsSent = 0;          // Beacons broadcast
    uint32_t clientsConnected = 0;     // Currently connected clients
    uint32_t credentialsCaptured = 0;  // Credentials harvested
    uint32_t startTime = 0;
    
    uint32_t getDuration() const;
};

/**
 * @brief Karma AP Attack
 * 
 * Listens for probe requests from WiFi clients looking for known networks.
 * For each unique SSID probed, creates a matching beacon to lure the
 * client into connecting. Combines with captive portal for credential
 * harvesting.
 * 
 * Attack modes:
 * - LISTENING: Passive mode, only collects probe requests
 * - ACTIVE: Responds to probes with matching SSIDs
 */
class KarmaAP {
public:
    KarmaAP();
    ~KarmaAP();
    
    // Prevent copying
    KarmaAP(const KarmaAP&) = delete;
    KarmaAP& operator=(const KarmaAP&) = delete;
    
    /**
     * @brief Start Karma AP in listening mode
     * Only captures probe requests, doesn't respond
     * @return true if started successfully
     */
    bool startListening();
    
    /**
     * @brief Start Karma AP in active mode
     * Responds to probes with matching SSIDs
     * @param portalConfig Captive portal configuration
     * @return true if started successfully
     */
    bool startActive(const ap::CaptivePortalConfig& portalConfig);
    
    /**
     * @brief Stop Karma AP
     */
    void stop();
    
    /**
     * @brief Update Karma AP (call in main loop)
     */
    void update();
    
    /**
     * @brief Get current state
     */
    KarmaState getState() const { return state_; }
    
    /**
     * @brief Check if active
     */
    bool isRunning() const { 
        return state_ == KarmaState::LISTENING || state_ == KarmaState::ACTIVE; 
    }
    
    /**
     * @brief Get attack statistics
     */
    const KarmaStats& getStats() const { return stats_; }
    
    /**
     * @brief Get captured probe requests
     */
    const std::vector<ProbeRequest>& getProbes() const { return probes_; }
    
    /**
     * @brief Get unique SSIDs requested
     */
    std::vector<const char*> getUniqueSSIDs() const;
    
    /**
     * @brief Get unique client MACs
     */
    std::vector<const uint8_t*> getUniqueClients() const;
    
    /**
     * @brief Clear captured probes
     */
    void clearProbes() { probes_.clear(); }
    
    /**
     * @brief Get connected clients
     */
    uint8_t getClients(ap::APClient* clients, uint8_t maxClients) const;
    
    /**
     * @brief Get captured credentials
     */
    const std::vector<ap::CapturedCredential>& getCredentials() const;
    
    /**
     * @brief Set maximum SSIDs to track
     */
    void setMaxSSIDs(size_t max) { maxSSIDs_ = max; }
    
    /**
     * @brief Set maximum probes to store
     */
    void setMaxProbes(size_t max) { maxProbes_ = max; }
    
    /**
     * @brief Force stop the captive portal (DNS + HTTP) regardless of state
     * Used to ensure DNS port 53 is released before other attacks start
     */
    void forceStopPortal() { arduinoPortal_.stop(); }
    
    /**
     * @brief Get current broadcast SSID (in active mode)
     */
    const char* getCurrentSSID() const;

#ifdef UNIT_TEST
    // Expose internals for unit testing
    void handleProbe(const uint8_t* clientMAC, const char* ssid, int8_t rssi);
    bool isSSIDOnCooldown(const char* ssid) const;
    void markSSIDTried(const char* ssid);
    void clearSSIDCooldown(const char* ssid);
    void rotateSSID();
    void updateStats();
#endif

private:
    KarmaState state_ = KarmaState::IDLE;
    KarmaStats stats_;
    
    // Captured probe requests
    std::vector<ProbeRequest> probes_;
    size_t maxProbes_ = 100;
    size_t maxSSIDs_ = 20;
    
    // Current SSID being broadcast (in active mode)
    char currentSSID_[33] = {0};
    size_t ssidRotationIndex_ = 0;
    uint32_t lastSSIDRotation_ = 0;
    
    // SSID cooldown tracking - maps SSID name to last broadcast timestamp
    std::map<std::string, uint32_t> triedSSIDs_;
    
    // Get rotation interval based on settings (Fast=15s, Normal=30s, Slow=60s)
    uint32_t getRotationIntervalMs() const;
    
    // Get cooldown period in milliseconds from settings
    uint32_t getCooldownMs() const;
    
#ifndef UNIT_TEST
    // Private only when not testing
    void handleProbe(const uint8_t* clientMAC, const char* ssid, int8_t rssi);
    bool isSSIDOnCooldown(const char* ssid) const;
    void markSSIDTried(const char* ssid);
    void clearSSIDCooldown(const char* ssid);
#endif
    
    // Shared components - use Arduino captive portal for reliability
    ap::ArduinoCaptivePortal arduinoPortal_;  // Combined DNS + HTTP
    ap::CaptivePortalConfig portalConfig_;
    // Probe capture
    bool setupProbeCapture();
    void stopProbeCapture();
    
    // Static callback for promiscuous mode (platform-specific signature)
#ifdef UNIT_TEST
    static void probeCallback(void* buf, int type);
#else
    static void probeCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#endif
    static KarmaAP* instance_;  // For static callback access
    
#ifndef UNIT_TEST
    // Internal handlers
    void rotateSSID();
    void updateStats();
#endif
    
    // Check if SSID should be ignored (common/useless)
    bool shouldIgnoreSSID(const char* ssid) const;
};

} // namespace attack
