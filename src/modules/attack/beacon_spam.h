/**
 * @file beacon_spam.h
 * @brief Beacon Spam attack - broadcast fake network beacons
 * 
 * Broadcasts 802.11 beacon frames with custom SSIDs to create
 * fake network entries in nearby devices' WiFi lists.
 */

#ifndef ADVERSARY_BEACON_SPAM_H
#define ADVERSARY_BEACON_SPAM_H

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
 * @brief Beacon spam attack mode
 */
enum class BeaconSpamMode : uint8_t {
    SINGLE_SSID,        ///< Broadcast single SSID repeatedly
    RANDOM_SSIDS,       ///< Broadcast random SSIDs
    SSID_LIST,          ///< Cycle through SSID list
    RICKROLL,           ///< Rick Roll lyrics as SSIDs
    FUNNY,              ///< Funny/meme SSIDs
    OFFENSIVE           ///< Offensive SSIDs (use responsibly)
};

/**
 * @brief Beacon spam configuration
 */
struct BeaconSpamConfig {
    BeaconSpamMode mode;            ///< Attack mode
    char ssid[33];                  ///< Single SSID (for SINGLE_SSID mode)
    uint8_t channel;                ///< Channel to broadcast on
    uint32_t intervalMs;            ///< Interval between beacon bursts in ms
    bool randomizeBssid;            ///< Randomize BSSID for each beacon
    uint8_t baseBssid[6];           ///< Base BSSID (if not randomizing)
    bool encryptedNetwork;          ///< Advertise as WPA2 encrypted
    uint32_t maxBeacons;            ///< Max beacons to send (0 = infinite)
    bool burstMode;                 ///< Send all SSIDs in rapid burst each interval
    uint32_t burstDelayMs;          ///< Delay between beacons in burst (default 10ms)
    bool channelHopEnabled;         ///< Enable channel hopping
    uint32_t channelHopIntervalMs;  ///< Channel hop interval (default 200ms)
    
    BeaconSpamConfig()
        : mode(BeaconSpamMode::RICKROLL)
        , channel(1)
        , intervalMs(100)
        , randomizeBssid(true)
        , encryptedNetwork(false)
        , maxBeacons(0)
        , burstMode(true)
        , burstDelayMs(10)
        , channelHopEnabled(false)
        , channelHopIntervalMs(200)
    {
        memset(ssid, 0, sizeof(ssid));
        memset(baseBssid, 0, 6);
        // Default base BSSID with locally administered bit set
        baseBssid[0] = 0x02;  // Locally administered, unicast
    }
    
    void setSsid(const char* s) {
        if (s) {
            strncpy(ssid, s, 32);
            ssid[32] = '\0';
        }
    }
    
    void setBaseBssid(const uint8_t* bssid) {
        if (bssid) memcpy(baseBssid, bssid, 6);
    }
};

/**
 * @brief Beacon spam statistics
 */
struct BeaconSpamStats {
    uint32_t beaconsSent;           ///< Total beacons transmitted
    uint32_t ssidsUsed;             ///< Number of unique SSIDs used
    uint32_t startTime;             ///< Attack start time (ms)
    uint32_t duration;              ///< Current duration (ms)
    
    BeaconSpamStats() { reset(); }
    
    void reset() {
        beaconsSent = 0;
        ssidsUsed = 0;
        startTime = 0;
        duration = 0;
    }
    
    float getBeaconsPerSecond() const {
        if (duration == 0) return 0.0f;
        return (float)beaconsSent * 1000.0f / (float)duration;
    }
};

/**
 * @brief Beacon spam attack state
 */
enum class BeaconSpamState : uint8_t {
    IDLE,               ///< Not running
    RUNNING,            ///< Attack in progress
    PAUSED,             ///< Attack paused
    COMPLETED,          ///< Max beacons reached
    ERROR               ///< Error occurred
};

/**
 * @brief Get SSID list by mode
 * @param mode Beacon spam mode
 * @return Vector of SSIDs for that mode
 */
std::vector<std::string> getSSIDList(BeaconSpamMode mode);

/**
 * @brief Get mode name as string
 */
const char* getBeaconSpamModeName(BeaconSpamMode mode);

/**
 * @brief Beacon Spam attack module
 * 
 * Broadcasts fake 802.11 beacon frames to flood nearby devices
 * with fake network entries.
 */
class BeaconSpam {
public:
    /**
     * @brief Get singleton instance
     */
    static BeaconSpam& getInstance();
    
    // Delete copy/move
    BeaconSpam(const BeaconSpam&) = delete;
    BeaconSpam& operator=(const BeaconSpam&) = delete;
    
    /**
     * @brief Initialize the module
     */
    bool init();
    
    /**
     * @brief Deinitialize the module
     */
    void deinit();
    
    /**
     * @brief Start beacon spam attack
     * @param config Attack configuration
     * @return true if started successfully
     */
    bool start(const BeaconSpamConfig& config);
    
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
     * Handles beacon transmission timing.
     */
    void update();
    
    /**
     * @brief Set custom SSID list
     * @param ssids Vector of SSIDs to broadcast
     */
    void setCustomSSIDs(const std::vector<std::string>& ssids);
    
    // Getters
    BeaconSpamState getState() const { return state_; }
    const BeaconSpamConfig& getConfig() const { return config_; }
    const BeaconSpamStats& getStats() const { return stats_; }
    bool isRunning() const { return state_ == BeaconSpamState::RUNNING; }
    bool isPaused() const { return state_ == BeaconSpamState::PAUSED; }
    const char* getCurrentSSID() const { return currentSsid_; }
    
    /**
     * @brief Build a beacon frame
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @param ssid SSID to include
     * @param bssid BSSID to use
     * @param channel Channel number
     * @param encrypted Whether to advertise as WPA2
     * @return Frame size, or 0 on error
     */
    static size_t buildBeaconFrame(uint8_t* buffer, size_t bufferSize,
                                   const char* ssid, const uint8_t* bssid,
                                   uint8_t channel, bool encrypted);

private:
    BeaconSpam();
    ~BeaconSpam() = default;
    
    void setState(BeaconSpamState newState);
    void sendBeacon();
    void sendBurst();  ///< Send all SSIDs in rapid burst
    void generateRandomBssid(uint8_t* bssid);
    const char* getNextSSID();
    const char* getSSIDAt(size_t index);  ///< Get SSID at specific index
    void hopChannel();  ///< Hop to next channel
    
    BeaconSpamState state_;
    BeaconSpamConfig config_;
    BeaconSpamStats stats_;
    
    std::vector<std::string> ssidList_;
    size_t currentSsidIndex_;
    char currentSsid_[33];
    uint8_t currentBssid_[6];
    
    uint32_t lastBeaconTime_;
    uint32_t lastHopTime_;
    uint8_t hopChannelIndex_;
    uint16_t sequenceNumber_;
    bool initialized_;
    
    // Singleton instance
    static BeaconSpam* s_instance_;
    
    // Beacon frame constants
    static constexpr size_t MAX_BEACON_SIZE = 256;
    static constexpr uint8_t BEACON_FRAME_CONTROL[] = {0x80, 0x00};  // Management, Beacon
    
    // Channel hopping sequence
    static constexpr uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10};
    static constexpr uint8_t HOP_CHANNEL_COUNT = 14;
};

} // namespace adversary

#endif // ADVERSARY_BEACON_SPAM_H
