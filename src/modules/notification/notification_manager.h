/**
 * @file notification_manager.h
 * @brief Centralized notification manager for audio and LED feedback
 * 
 * Provides non-blocking acoustic (beeps) and visual (LED) notifications
 * for various Adversary events. Supports M5Cardputer and M5StickC Plus 2.
 */

#ifndef ADVERSARY_NOTIFICATION_MANAGER_H
#define ADVERSARY_NOTIFICATION_MANAGER_H

#include <cstdint>
#include "core/event_bus.h"
#include "modules/storage/settings_manager.h"

namespace adversary {

/**
 * @brief Notification pattern definition
 */
struct NotificationPattern {
    const uint16_t* frequencies;  ///< Tone frequencies (Hz), 0 = rest
    const uint16_t* durations;    ///< Tone durations (ms)
    uint8_t noteCount;            ///< Number of notes
    uint32_t ledColor;            ///< RGB color (0xRRGGBB)
    uint8_t ledFlashes;           ///< Number of LED flashes
    uint16_t ledOnMs;             ///< LED on duration (ms)
    uint16_t ledOffMs;            ///< LED off duration (ms)
};

/**
 * @brief Notification event types
 */
enum class NotificationType : uint8_t {
    EAPOL_PACKET,          ///< EAPOL M1/M2/M3/M4 detected
    HANDSHAKE_COMPLETE,    ///< Full handshake captured
    PMKID_CAPTURED,        ///< PMKID extracted from M1
    ATTACK_STARTED,        ///< Attack mode started
    ATTACK_STOPPED,        ///< Attack mode stopped
    ERROR,                 ///< Error occurred
    WARNING,               ///< Warning (non-critical)
    CREDENTIAL_CAPTURED,   ///< Evil twin credential captured
    TARGET_WHITELISTED,    ///< Network added to whitelist
    SCAN_COMPLETE,         ///< WiFi scan completed
    TAG_READ,              ///< RFID tag read successfully
    BLE_INTERACTION        ///< Bluetooth Low Energy interaction
};

/**
 * @brief Notification priority levels
 */
enum class NotificationPriority : uint8_t {
    PRIORITY_LOW = 0,
    PRIORITY_MEDIUM = 1,
    PRIORITY_HIGH = 2
};

// ============================================================================
// Predefined notification patterns
// ============================================================================

// EAPOL packet - single short beep, orange flash
inline const uint16_t EAPOL_FREQS[] = {800};
inline const uint16_t EAPOL_DURS[] = {30};
inline const NotificationPattern PATTERN_EAPOL = {
    EAPOL_FREQS, EAPOL_DURS, 1,
    0xFF6600, 1, 30, 0
};

// Handshake complete - ascending arpeggio (C5, E5, G5), green double flash
inline const uint16_t HS_FREQS[] = {523, 659, 784};
inline const uint16_t HS_DURS[] = {80, 80, 120};
inline const NotificationPattern PATTERN_HANDSHAKE = {
    HS_FREQS, HS_DURS, 3,
    0x00FF00, 2, 100, 100
};

// PMKID captured - two quick beeps, blue flash
inline const uint16_t PMKID_FREQS[] = {1000, 0, 1000};
inline const uint16_t PMKID_DURS[] = {40, 20, 40};
inline const NotificationPattern PATTERN_PMKID = {
    PMKID_FREQS, PMKID_DURS, 3,
    0x0066FF, 1, 80, 0
};

// Attack started - low tone, white flash
inline const uint16_t START_FREQS[] = {440};
inline const uint16_t START_DURS[] = {100};
inline const NotificationPattern PATTERN_ATTACK_STARTED = {
    START_FREQS, START_DURS, 1,
    0xFFFFFF, 1, 50, 0
};

// Attack stopped - descending tone, red flash
inline const uint16_t STOP_FREQS[] = {660, 440};
inline const uint16_t STOP_DURS[] = {80, 80};
inline const NotificationPattern PATTERN_ATTACK_STOPPED = {
    STOP_FREQS, STOP_DURS, 2,
    0xFF0000, 1, 50, 0
};

// Error - harsh buzz, red rapid flash
inline const uint16_t ERROR_FREQS[] = {200, 0, 200};
inline const uint16_t ERROR_DURS[] = {100, 50, 100};
inline const NotificationPattern PATTERN_ERROR = {
    ERROR_FREQS, ERROR_DURS, 3,
    0xFF0000, 3, 50, 50
};

// Warning - single low beep, yellow flash
inline const uint16_t WARN_FREQS[] = {300};
inline const uint16_t WARN_DURS[] = {150};
inline const NotificationPattern PATTERN_WARNING = {
    WARN_FREQS, WARN_DURS, 1,
    0xFFFF00, 1, 100, 0
};

// Credential captured - success melody, cyan triple flash
inline const uint16_t CRED_FREQS[] = {523, 659, 784, 1047};
inline const uint16_t CRED_DURS[] = {60, 60, 60, 150};
inline const NotificationPattern PATTERN_CREDENTIAL = {
    CRED_FREQS, CRED_DURS, 4,
    0x00FFFF, 3, 80, 80
};

// Target whitelisted - quick double beep, blue flash
inline const uint16_t WHITE_FREQS[] = {600, 0, 800};
inline const uint16_t WHITE_DURS[] = {30, 20, 30};
inline const NotificationPattern PATTERN_WHITELISTED = {
    WHITE_FREQS, WHITE_DURS, 3,
    0x0066FF, 1, 50, 0
};

// Scan complete - soft ping
inline const uint16_t SCAN_FREQS[] = {1200};
inline const uint16_t SCAN_DURS[] = {20};
inline const NotificationPattern PATTERN_SCAN_COMPLETE = {
    SCAN_FREQS, SCAN_DURS, 1,
    0x000000, 0, 0, 0  // No LED
};

// Tag read - double short high beeps, lime green flash
inline const uint16_t TAG_FREQS[] = {2500, 0, 3000};
inline const uint16_t TAG_DURS[] = {40, 20, 60};
inline const NotificationPattern PATTERN_TAG_READ = {
    TAG_FREQS, TAG_DURS, 3,
    0x32CD32, 1, 100, 0  // LimeGreen
};

/**
 * @brief Centralized notification manager
 * 
 * Queue-based, non-blocking notification system for audio and LED feedback.
 * Singleton pattern for global access.
 */
class NotificationManager {
public:
    static NotificationManager& getInstance();
    
    /**
     * @brief Initialize notification hardware
     */
    void init();
    
    /**
     * @brief Send a predefined notification
     * @param type The notification type to send
     */
    void notify(NotificationType type);
    
    /**
     * @brief Send a custom notification pattern
     * @param pattern The pattern to play
     * @param priority Notification priority
     */
    void notifyCustom(const NotificationPattern& pattern, 
                      NotificationPriority priority = NotificationPriority::PRIORITY_MEDIUM);
    
    /**
     * @brief Update playback state (call from main loop)
     */
    void update();
    
    /**
     * @brief Check if currently playing a notification
     */
    bool isPlaying() const { return playing_; }
    
    // Configuration
    void setAudioEnabled(bool enabled) { audioEnabled_ = enabled; }
    void setLedEnabled(bool enabled) { ledEnabled_ = enabled; }
    void setVolume(uint8_t volume);
    
    bool isAudioEnabled() const { return audioEnabled_; }
    bool isLedEnabled() const { return ledEnabled_; }
    uint8_t getVolume() const { return volume_; }
    
#ifdef UNIT_TEST
    uint8_t getQueueCount() const { return queueCount_; }
    NotificationType getLastNotifiedType() const { return lastType_; }
    void clearQueue() { queueHead_ = 0; queueTail_ = 0; queueCount_ = 0; playing_ = false; }
#endif
    
private:
    NotificationManager();
    ~NotificationManager();
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;
    
    /**
     * @brief Handle incoming events from EventBus
     */
    void handleEvent(const EventData& data);
    
    /**
     * @brief Check system settings for enabled flags
     */
    void syncSettings();
    
    /**
     * @brief Get pattern for notification type
     */
    const NotificationPattern& getPattern(NotificationType type) const;
    
    /**
     * @brief Get priority for notification type
     */
    NotificationPriority getPriority(NotificationType type) const;
    
    /**
     * @brief Queue a notification
     */
    bool enqueue(const NotificationPattern& pattern, NotificationPriority priority);
    
    /**
     * @brief Start playing the next queued notification
     */
    void playNext();
    
    /**
     * @brief Set LED color (hardware abstracted)
     */
    void setLed(uint32_t color);
    
    /**
     * @brief Play tone (hardware abstracted)
     */
    void playTone(uint16_t frequency, uint16_t duration);
    
    /**
     * @brief Stop current tone
     */
    void stopTone();
    
    // Queue
    struct QueuedNotification {
        NotificationPattern pattern;
        NotificationPriority priority;
    };
    
    static constexpr uint8_t QUEUE_SIZE = 8;
    QueuedNotification queue_[QUEUE_SIZE];
    uint8_t queueHead_ = 0;
    uint8_t queueTail_ = 0;
    uint8_t queueCount_ = 0;
    
    // Playback state
    bool playing_ = false;
    NotificationPattern currentPattern_;
    uint8_t currentNote_ = 0;
    uint32_t noteStartTime_ = 0;
    uint8_t ledFlashCount_ = 0;
    uint32_t ledToggleTime_ = 0;
    bool ledOn_ = false;
    
    // Settings
    bool audioEnabled_ = true;
    bool ledEnabled_ = true;
    uint8_t volume_ = 80;
    bool initialized_ = false;
#ifdef UNIT_TEST
    NotificationType lastType_;
#endif
};

} // namespace adversary

#endif // ADVERSARY_NOTIFICATION_MANAGER_H
