/**
 * @file notification_manager.cpp
 * @brief Notification manager implementation
 */

#include "notification_manager.h"

#ifdef ESP32
#include <Arduino.h>
#include <M5Unified.h>
#include <FastLED.h>

// LED configuration
#if defined(TARGET_CARDPUTER)
    #define LED_PIN 21        // M5Cardputer NeoPixel GPIO
#elif defined(TARGET_M5STICK)
    #define LED_PIN 19        // M5StickC Plus 2 
#else
    #define LED_PIN 21        // Default
#endif

#define NUM_LEDS 1
#define LED_TYPE WS2812
#define COLOR_ORDER GRB

static CRGB leds[NUM_LEDS];
static bool ledInitialized = false;
#endif

namespace adversary {

NotificationManager::NotificationManager() : initialized_(false) {
#ifdef UNIT_TEST
    lastType_ = (NotificationType)255; // Sentinel
#endif
}

NotificationManager::~NotificationManager() = default;

NotificationManager& NotificationManager::getInstance() {
    static NotificationManager instance;
    return instance;
}

void NotificationManager::init() {
    if (initialized_) return;
#ifdef ESP32
    // Speaker is already initialized by M5.begin()
    // Set initial volume
    M5.Speaker.setVolume(volume_ * 255 / 100);
    
    // Initialize FastLED for NeoPixel
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(128);  // 50% brightness
    leds[0] = CRGB::Black;
    FastLED.show();
    ledInitialized = true;
#endif

    initialized_ = true;

    // Sync initial settings
    syncSettings();

    // Subscribe to semantic events
    auto handler = [this](const EventData& data) { this->handleEvent(data); };
    
    EventBus::getInstance().subscribe(EventType::HANDSHAKE_CAPTURED, handler);
    EventBus::getInstance().subscribe(EventType::PMKID_CAPTURED, handler);
    EventBus::getInstance().subscribe(EventType::EAPOL_CAPTURED, handler);
    EventBus::getInstance().subscribe(EventType::CREDENTIAL_CAPTURED, handler);
    EventBus::getInstance().subscribe(EventType::CLIENT_CONNECTED, handler);
    EventBus::getInstance().subscribe(EventType::BLE_CONNECTED, handler);
    EventBus::getInstance().subscribe(EventType::RFID_TAG_DETECTED, handler);
}

void NotificationManager::notify(NotificationType type) {
    // Check if notifications are disabled globally in settings
    syncSettings();
    if (!audioEnabled_ && !ledEnabled_) return;

#ifdef UNIT_TEST
    lastType_ = type;
#endif

    const NotificationPattern& pattern = getPattern(type);
    NotificationPriority priority = getPriority(type);
    enqueue(pattern, priority);
}

void NotificationManager::notifyCustom(const NotificationPattern& pattern, 
                                        NotificationPriority priority) {
    enqueue(pattern, priority);
}

const NotificationPattern& NotificationManager::getPattern(NotificationType type) const {
    switch (type) {
        case NotificationType::EAPOL_PACKET:       return PATTERN_EAPOL;
        case NotificationType::HANDSHAKE_COMPLETE: return PATTERN_HANDSHAKE;
        case NotificationType::PMKID_CAPTURED:     return PATTERN_PMKID;
        case NotificationType::ATTACK_STARTED:     return PATTERN_ATTACK_STARTED;
        case NotificationType::ATTACK_STOPPED:     return PATTERN_ATTACK_STOPPED;
        case NotificationType::ERROR:              return PATTERN_ERROR;
        case NotificationType::WARNING:            return PATTERN_WARNING;
        case NotificationType::CREDENTIAL_CAPTURED: return PATTERN_CREDENTIAL;
        case NotificationType::TARGET_WHITELISTED: return PATTERN_WHITELISTED;
        case NotificationType::SCAN_COMPLETE:      return PATTERN_SCAN_COMPLETE;
        case NotificationType::TAG_READ:           return PATTERN_TAG_READ;
        default:                                   return PATTERN_EAPOL;
    }
}

NotificationPriority NotificationManager::getPriority(NotificationType type) const {
    switch (type) {
        case NotificationType::HANDSHAKE_COMPLETE:
        case NotificationType::ERROR:
        case NotificationType::CREDENTIAL_CAPTURED:
            return NotificationPriority::PRIORITY_HIGH;
            
        case NotificationType::PMKID_CAPTURED:
        case NotificationType::WARNING:
            return NotificationPriority::PRIORITY_MEDIUM;
            
        case NotificationType::TAG_READ:
            return NotificationPriority::PRIORITY_MEDIUM;
            
        default:
            return NotificationPriority::PRIORITY_LOW;
    }
}

bool NotificationManager::enqueue(const NotificationPattern& pattern, 
                                   NotificationPriority priority) {
    // If queue is full, check if we should replace a lower priority item
    if (queueCount_ >= QUEUE_SIZE) {
        // Queue full - drop if low priority
        if (priority == NotificationPriority::PRIORITY_LOW) {
            return false;
        }
        // For higher priority, overwrite oldest low priority item
        // For now, just drop
        return false;
    }
    
    // Add to queue
    queue_[queueTail_].pattern = pattern;
    queue_[queueTail_].priority = priority;
    queueTail_ = (queueTail_ + 1) % QUEUE_SIZE;
    queueCount_++;
    
    // Start playing if not already
    if (!playing_) {
        playNext();
    }
    
    return true;
}

void NotificationManager::playNext() {
    if (queueCount_ == 0) {
        playing_ = false;
        setLed(0);  // Turn off LED
        return;
    }
    
    // Get next from queue
    currentPattern_ = queue_[queueHead_].pattern;
    queueHead_ = (queueHead_ + 1) % QUEUE_SIZE;
    queueCount_--;
    
    playing_ = true;
    currentNote_ = 0;
    ledFlashCount_ = 0;
    ledOn_ = false;
    
#ifdef ESP32
    noteStartTime_ = millis();
    ledToggleTime_ = millis();
#endif
    
    // Start first note
    if (audioEnabled_ && currentPattern_.noteCount > 0) {
        playTone(currentPattern_.frequencies[0], currentPattern_.durations[0]);
    }
    
    // Start LED
    if (ledEnabled_ && currentPattern_.ledFlashes > 0) {
        setLed(currentPattern_.ledColor);
        ledOn_ = true;
    }
}

void NotificationManager::handleEvent(const EventData& data) {
    switch (data.type) {
        case EventType::HANDSHAKE_CAPTURED:
            notify(NotificationType::HANDSHAKE_COMPLETE);
            break;
        case EventType::PMKID_CAPTURED:
            notify(NotificationType::PMKID_CAPTURED);
            break;
        case EventType::EAPOL_CAPTURED:
            notify(NotificationType::EAPOL_PACKET);
            break;
        case EventType::CREDENTIAL_CAPTURED:
            notify(NotificationType::CREDENTIAL_CAPTURED);
            break;
        case EventType::CLIENT_CONNECTED:
            notify(NotificationType::EAPOL_PACKET); // Short beep for target join
            break;
        case EventType::BLE_CONNECTED:
            notify(NotificationType::HANDSHAKE_COMPLETE); // Success tone for BLE pairing
            break;
        case EventType::SD_ERROR:
            notify(NotificationType::ERROR);
            break;
        case EventType::BATTERY_LOW:
            notify(NotificationType::WARNING);
            break;
        default:
            // Ignore other events
            break;
    }
}

void NotificationManager::syncSettings() {
#ifdef ESP32
    const auto& sys = SettingsManager::getInstance().get().system;
    audioEnabled_ = sys.notifySounds;
    ledEnabled_ = sys.notifyLeds;
#endif
}

void NotificationManager::update() {
#ifdef ESP32
    if (!playing_) return;
    
    uint32_t now = millis();
    
    // Update audio
    if (audioEnabled_ && currentNote_ < currentPattern_.noteCount) {
        uint32_t noteDuration = currentPattern_.durations[currentNote_];
        if (now - noteStartTime_ >= noteDuration) {
            currentNote_++;
            noteStartTime_ = now;
            
            if (currentNote_ < currentPattern_.noteCount) {
                // Play next note
                playTone(currentPattern_.frequencies[currentNote_], 
                        currentPattern_.durations[currentNote_]);
            } else {
                stopTone();
            }
        }
    }
    
    // Update LED
    if (ledEnabled_ && currentPattern_.ledFlashes > 0) {
        if (ledOn_) {
            // LED is on - check if should turn off
            if (now - ledToggleTime_ >= currentPattern_.ledOnMs) {
                setLed(0);
                ledOn_ = false;
                ledFlashCount_++;
                ledToggleTime_ = now;
            }
        } else {
            // LED is off - check if should flash again
            if (ledFlashCount_ < currentPattern_.ledFlashes) {
                if (now - ledToggleTime_ >= currentPattern_.ledOffMs) {
                    setLed(currentPattern_.ledColor);
                    ledOn_ = true;
                    ledToggleTime_ = now;
                }
            }
        }
    }
    
    // Check if notification is complete
    bool audioComplete = !audioEnabled_ || currentNote_ >= currentPattern_.noteCount;
    bool ledComplete = !ledEnabled_ || 
                       currentPattern_.ledFlashes == 0 || 
                       (ledFlashCount_ >= currentPattern_.ledFlashes && !ledOn_);
    
    if (audioComplete && ledComplete) {
        // Move to next in queue
        playNext();
    }
#endif
}

void NotificationManager::setVolume(uint8_t volume) {
    volume_ = (volume > 100) ? 100 : volume;
#ifdef ESP32
    M5.Speaker.setVolume(volume_ * 255 / 100);
#endif
}

void NotificationManager::setLed(uint32_t color) {
#ifdef ESP32
    if (!ledInitialized) return;
    
    if (color == 0) {
        leds[0] = CRGB::Black;
    } else {
        // Convert 0xRRGGBB to CRGB
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        leds[0] = CRGB(r, g, b);
    }
    FastLED.show();
#else
    (void)color;
#endif
}

void NotificationManager::playTone(uint16_t frequency, uint16_t duration) {
#ifdef ESP32
    if (frequency == 0) {
        stopTone();
        return;
    }
    
    M5.Speaker.tone(frequency, duration);
#else
    (void)frequency;
    (void)duration;
#endif
}

void NotificationManager::stopTone() {
#ifdef ESP32
    M5.Speaker.stop();
#endif
}

} // namespace adversary
