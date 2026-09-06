/**
 * @file notification_mocks.cpp
 * @brief Native implementation of NotificationManager for unit tests
 */

#include "modules/notification/notification_manager.h"

#ifndef ESP32

namespace adversary {

NotificationManager& NotificationManager::getInstance() {
    static NotificationManager instance;
    return instance;
}

void NotificationManager::init() {}
void NotificationManager::notify(NotificationType type) {}
void NotificationManager::notifyCustom(const NotificationPattern& pattern, NotificationPriority priority) {}
void NotificationManager::update() {}
void NotificationManager::setVolume(uint8_t volume) {}
void NotificationManager::setLed(uint32_t color) {}
void NotificationManager::playTone(uint16_t frequency, uint16_t duration) {}
void NotificationManager::stopTone() {}

// Internal helpers if needed by the header (e.g., if they were not inline)
const NotificationPattern& NotificationManager::getPattern(NotificationType type) const {
    static NotificationPattern dummy = { nullptr, nullptr, 0, 0, 0, 0, 0 };
    return dummy;
}

NotificationPriority NotificationManager::getPriority(NotificationType type) const {
    return NotificationPriority::PRIORITY_LOW;
}

bool NotificationManager::enqueue(const NotificationPattern& pattern, NotificationPriority priority) {
    return true;
}

void NotificationManager::playNext() {}

} // namespace adversary

#endif
