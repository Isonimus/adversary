/**
 * @file probe_flood.h
 * @brief Probe Flood attack - broadcast mass probe request frames
 * 
 * Sends 802.11 probe request frames to flood network with requests,
 * useful for stress testing and reconnaissance flooding.
 */

#ifndef ADVERSARY_PROBE_FLOOD_H
#define ADVERSARY_PROBE_FLOOD_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <string>

#ifndef ESP32
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif

namespace adversary {

/**
 * @brief Probe flood attack mode
 */
enum class ProbeFloodMode : uint8_t {
    RANDOM_SSIDS,       ///< Send probes with random SSIDs
    SSID_LIST,          ///< Cycle through SSID list
    TARGETED,           ///< Probe for specific SSID
    BLANK               ///< Wildcard/null probe (no SSID)
};

/**
 * @brief Probe flood configuration
 */
struct ProbeFloodConfig {
    ProbeFloodMode mode;            ///< Attack mode
    char ssid[33];                  ///< Target SSID (for TARGETED mode)
    uint8_t channel;                ///< Channel to send on (0 = hopping)
    uint32_t intervalMs;            ///< Interval between probe bursts in ms
    bool randomizeMac;              ///< Randomize source MAC for each probe
    uint8_t baseMac[6];             ///< Base MAC address
    uint8_t targetBssid[6];         ///< Target BSSID (optional, all zeros = broadcast)
    uint32_t maxProbes;             ///< Max probes to send (0 = infinite)
    bool burstMode;                 ///< Send multiple probes per interval
    uint8_t burstCount;             ///< Number of probes per burst
    uint32_t burstDelayMs;          ///< Delay between probes in burst
    bool channelHopEnabled;         ///< Enable channel hopping
    uint32_t channelHopIntervalMs;  ///< Channel hop interval
    
    ProbeFloodConfig()
        : mode(ProbeFloodMode::RANDOM_SSIDS)
        , channel(0)
        , intervalMs(50)
        , randomizeMac(true)
        , maxProbes(0)
        , burstMode(true)
        , burstCount(5)
        , burstDelayMs(5)
        , channelHopEnabled(true)
        , channelHopIntervalMs(200)
    {
        memset(ssid, 0, sizeof(ssid));
        memset(baseMac, 0, 6);
        memset(targetBssid, 0xFF, 6);  // Broadcast by default
        // Default base MAC with locally administered bit set
        baseMac[0] = 0x02;  // Locally administered, unicast
    }
    
    void setSsid(const char* s) {
        if (s) {
            strncpy(ssid, s, 32);
            ssid[32] = '\0';
        }
    }
    
    void setBaseMac(const uint8_t* mac) {
        if (mac) memcpy(baseMac, mac, 6);
    }
    
    void setTargetBssid(const uint8_t* bssid) {
        if (bssid) memcpy(targetBssid, bssid, 6);
    }
    
    bool hasTargetBssid() const {
        for (int i = 0; i < 6; i++) {
            if (targetBssid[i] != 0xFF) return true;
        }
        return false;
    }
};

/**
 * @brief Probe flood statistics
 */
struct ProbeFloodStats {
    uint32_t probesSent;            ///< Total probes transmitted
    uint32_t ssidsUsed;             ///< Number of unique SSIDs used
    uint32_t startTime;             ///< Attack start time (ms)
    uint32_t duration;              ///< Current duration (ms)
    uint8_t currentChannel;         ///< Current channel
    
    ProbeFloodStats() { reset(); }
    
    void reset() {
        probesSent = 0;
        ssidsUsed = 0;
        startTime = 0;
        duration = 0;
        currentChannel = 1;
    }
    
    float getProbesPerSecond() const {
        if (duration == 0) return 0.0f;
        return (float)probesSent * 1000.0f / (float)duration;
    }
};

/**
 * @brief Probe flood attack state
 */
enum class ProbeFloodState : uint8_t {
    IDLE,               ///< Not running
    RUNNING,            ///< Attack in progress
    PAUSED,             ///< Attack paused
    COMPLETED,          ///< Max probes reached
    ERROR               ///< Error occurred
};

/**
 * @brief Get mode name as string
 */
const char* getProbeFloodModeName(ProbeFloodMode mode);

/**
 * @brief Probe Flood attack module
 * 
 * Broadcasts 802.11 probe request frames to flood networks
 * with reconnaissance traffic.
 */
class ProbeFlood {
public:
    /**
     * @brief Get singleton instance
     */
    static ProbeFlood& getInstance();
    
    // Delete copy/move
    ProbeFlood(const ProbeFlood&) = delete;
    ProbeFlood& operator=(const ProbeFlood&) = delete;
    
    /**
     * @brief Initialize the module
     */
    bool init();
    
    /**
     * @brief Deinitialize the module
     */
    void deinit();
    
    /**
     * @brief Start probe flood attack
     * @param config Attack configuration
     * @return true if started successfully
     */
    bool start(const ProbeFloodConfig& config);
    
    /**
     * @brief Stop the attack
     */
    void stop();
    
    /**
     * @brief Pause/resume the attack
     */
    void togglePause();
    
    /**
     * @brief Update - call regularly from main loop
     * 
     * Handles probe transmission timing and channel hopping.
     */
    void update();
    
    /**
     * @brief Set custom SSID list for SSID_LIST mode
     * @param ssids Vector of SSIDs to probe
     */
    void setCustomSSIDs(const std::vector<std::string>& ssids);
    
    // Getters
    ProbeFloodState getState() const { return state_; }
    const ProbeFloodConfig& getConfig() const { return config_; }
    const ProbeFloodStats& getStats() const { return stats_; }
    bool isRunning() const { return state_ == ProbeFloodState::RUNNING; }
    bool isPaused() const { return state_ == ProbeFloodState::PAUSED; }
    const char* getCurrentSSID() const { return currentSsid_; }
    
    /**
     * @brief Build a probe request frame
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @param ssid SSID to probe for (null or empty for wildcard)
     * @param srcMac Source MAC address
     * @param bssid Target BSSID (broadcast for wildcard)
     * @return Frame size, or 0 on error
     */
    static size_t buildProbeRequestFrame(uint8_t* buffer, size_t bufferSize,
                                         const char* ssid, const uint8_t* srcMac,
                                         const uint8_t* bssid);

private:
    ProbeFlood();
    ~ProbeFlood() = default;
    
    void setState(ProbeFloodState newState);
    void sendProbe();
    void sendBurst();
    void generateRandomMac(uint8_t* mac);
    void generateRandomSsid(char* ssid, size_t maxLen);
    const char* getNextSSID();
    void hopChannel();
    
    ProbeFloodState state_;
    ProbeFloodConfig config_;
    ProbeFloodStats stats_;
    
    std::vector<std::string> ssidList_;
    size_t currentSsidIndex_;
    char currentSsid_[33];
    uint8_t currentMac_[6];
    
    uint32_t lastProbeTime_;
    uint32_t lastHopTime_;
    uint8_t hopChannelIndex_;
    uint16_t sequenceNumber_;
    bool initialized_;
    
    // Singleton instance
    static ProbeFlood* s_instance_;
    
    // Probe frame constants
    static constexpr size_t MAX_PROBE_SIZE = 128;
    static constexpr uint8_t PROBE_FRAME_CONTROL[] = {0x40, 0x00};  // Management, Probe Request
    
    // Channel hopping sequence
    static constexpr uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10};
    static constexpr uint8_t HOP_CHANNEL_COUNT = 14;
};

} // namespace adversary

#endif // ADVERSARY_PROBE_FLOOD_H
