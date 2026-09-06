#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>

// Forward declare for non-ESP32 builds
#ifndef ESP32
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif

namespace adversary {

/**
 * @brief Deauth attack reason codes (802.11)
 */
enum class DeauthReason : uint16_t {
    UNSPECIFIED = 1,
    PREV_AUTH_NOT_VALID = 2,
    DEAUTH_LEAVING = 3,
    DISASSOC_DUE_TO_INACTIVITY = 4,
    DISASSOC_AP_BUSY = 5,
    CLASS2_FRAME_FROM_NONAUTH = 6,
    CLASS3_FRAME_FROM_NONASSOC = 7,
    DISASSOC_STA_LEAVING = 8,
    STA_REQ_NO_AUTH = 9,
    INVALID_IE = 13,
    MIC_FAILURE = 14,
    HANDSHAKE_TIMEOUT = 15,
    GROUP_KEY_UPDATE_TIMEOUT = 16,
    IE_MISMATCH = 17,
    INVALID_GROUP_CIPHER = 18,
    INVALID_PAIRWISE_CIPHER = 19,
    INVALID_AKMP = 20,
    UNSUPPORTED_RSN_VERSION = 21,
    INVALID_RSN_CAPABILITIES = 22,
    AUTH_FAILED = 23,
    CIPHER_SUITE_REJECTED = 24
};

/**
 * @brief Get human-readable string for deauth reason code
 */
const char* getDeauthReasonString(DeauthReason reason);

/**
 * @brief Deauth attack target type
 */
enum class DeauthTargetType {
    SINGLE_CLIENT,      // Target specific client
    ALL_CLIENTS,        // Broadcast to all clients
    AP_ONLY            // Target the AP itself
};

/**
 * @brief Deauth attack configuration
 */
struct DeauthConfig {
    uint8_t apBssid[6];           // Target AP BSSID
    uint8_t clientMac[6];         // Target client MAC (if SINGLE_CLIENT)
    DeauthTargetType targetType;   // Type of attack
    DeauthReason reason;           // Reason code to send
    uint32_t packetCount;          // Number of packets to send (0 = infinite)
    uint32_t delayMs;              // Delay between packets in ms
    uint8_t channel;               // Channel to attack on
    bool sendDisassoc;             // Also send disassociation frames
    
    DeauthConfig() :
        targetType(DeauthTargetType::ALL_CLIENTS),
        reason(DeauthReason::DEAUTH_LEAVING),
        packetCount(0),
        delayMs(100),
        channel(1),
        sendDisassoc(true) {
        memset(apBssid, 0, 6);
        memset(clientMac, 0xFF, 6);  // Broadcast by default
    }
    
    void setApBssid(const uint8_t* bssid) {
        if (bssid) memcpy(apBssid, bssid, 6);
    }
    
    void setClientMac(const uint8_t* mac) {
        if (mac) memcpy(clientMac, mac, 6);
    }
    
    bool hasValidBssid() const {
        for (int i = 0; i < 6; i++) {
            if (apBssid[i] != 0) return true;
        }
        return false;
    }
};

/**
 * @brief Deauth attack statistics
 */
struct DeauthStats {
    uint32_t packetsSent;
    uint32_t deauthSent;
    uint32_t disassocSent;
    uint32_t errors;
    uint32_t startTime;
    
    DeauthStats() : packetsSent(0), deauthSent(0), disassocSent(0), 
                   errors(0), startTime(0) {}
    
    void reset() {
        packetsSent = 0;
        deauthSent = 0;
        disassocSent = 0;
        errors = 0;
        startTime = 0;
    }
    
    uint32_t getDurationSeconds() const;
    float getPacketsPerSecond() const;
};

/**
 * @brief Deauth attack state
 */
enum class DeauthState {
    IDLE,
    RUNNING,
    PAUSED,
    COMPLETED,
    ERROR
};

/**
 * @brief 802.11 Deauthentication Attack Module
 * 
 * Sends deauthentication and disassociation frames to disconnect
 * clients from access points. Useful for:
 * - Forcing handshake renegotiation for capture
 * - Denial of service testing
 * - Client enumeration
 */
class DeauthAttack {
public:
    static DeauthAttack& getInstance();
    
    // Lifecycle
    bool init();
    void deinit();
    
    // Attack control
    bool start(const DeauthConfig& config);
    void stop();
    void pause();
    void resume();
    
    // State
    DeauthState getState() const { return state_; }
    bool isRunning() const { return state_ == DeauthState::RUNNING; }
    bool isPaused() const { return state_ == DeauthState::PAUSED; }
    bool isBypassEnabled() const { return bypassEnabled_; }  // WSL bypass for deauth frames
    
    // Statistics
    const DeauthStats& getStats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    
    // Configuration
    const DeauthConfig& getConfig() const { return config_; }
    // Frame building (exposed for testing)
    static size_t buildDeauthFrame(uint8_t* buffer, size_t bufferSize,
                                   const uint8_t* destMac, const uint8_t* srcMac,
                                   const uint8_t* bssid, DeauthReason reason);
    
    static size_t buildDisassocFrame(uint8_t* buffer, size_t bufferSize,
                                     const uint8_t* destMac, const uint8_t* srcMac,
                                     const uint8_t* bssid, DeauthReason reason);
    
    // ===========================================
    // Lightweight one-shot utilities
    // Use these for quick deauth needs without full attack setup
    // ===========================================
    
    /**
     * @brief Send a single deauth frame without full attack setup
     * @param bssid Target AP BSSID (spoofed as source)
     * @param destMac Destination MAC (client or broadcast)
     * @param reason Reason code (default: DEAUTH_LEAVING)
     * @param alsoDisassoc Send disassoc frame as well (recommended for max effectiveness)
     * @return true if at least one frame sent successfully
     */
    static bool sendSingleDeauth(const uint8_t* bssid, const uint8_t* destMac,
                                  DeauthReason reason = DeauthReason::DEAUTH_LEAVING,
                                  bool alsoDisassoc = true);
    
    /**
     * @brief Send deauth burst to multiple clients + broadcast fallback
     *        Perfect for handshake capture's targeted deauth needs
     * @param bssid Target AP BSSID
     * @param clients Array of client MACs (can be nullptr if clientCount=0)
     * @param clientCount Number of clients in array (max 16)
     * @param burstCount Number of frames per target (default 3)
     * @param alsoDisassoc Send disassoc frames too (default true)
     * @return Number of frames successfully sent
     */
    static uint16_t sendTargetedBurst(const uint8_t* bssid,
                                       const uint8_t clients[][6],
                                       uint8_t clientCount,
                                       uint8_t burstCount = 3,
                                       bool alsoDisassoc = true);
    
    // Process loop (called from main loop or task)
    void update();
    
private:
    DeauthAttack();
    ~DeauthAttack() = default;
    DeauthAttack(const DeauthAttack&) = delete;
    DeauthAttack& operator=(const DeauthAttack&) = delete;
    
    void setState(DeauthState newState);
    bool sendDeauthPacket();
    bool sendDisassocPacket();
    bool sendRawFrame(const uint8_t* frame, size_t length);
    
    DeauthState state_;
    DeauthConfig config_;
    DeauthStats stats_;
    bool initialized_;
    bool bypassEnabled_;       // Whether WSL bypass for deauth frames is working
    uint32_t lastPacketTime_;
    // Frame buffer
    static constexpr size_t MAX_FRAME_SIZE = 128;
    uint8_t frameBuffer_[MAX_FRAME_SIZE];
};

// 802.11 Frame type/subtype constants for deauth
namespace Deauth80211 {
    // Frame Control field
    constexpr uint8_t FC_TYPE_MGMT = 0x00;
    constexpr uint8_t FC_SUBTYPE_DEAUTH = 0xC0;      // 1100 0000 (subtype 12)
    constexpr uint8_t FC_SUBTYPE_DISASSOC = 0xA0;    // 1010 0000 (subtype 10)
    
    // Frame sizes
    constexpr size_t MAC_HEADER_SIZE = 24;
    constexpr size_t REASON_CODE_SIZE = 2;
    constexpr size_t DEAUTH_FRAME_SIZE = MAC_HEADER_SIZE + REASON_CODE_SIZE;
    
    // Broadcast MAC
    constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

} // namespace adversary
