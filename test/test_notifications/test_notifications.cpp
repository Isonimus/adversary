/**
 * @file test_notifications.cpp
 * @brief Unit tests for the Semantic Notification System
 */

#include <unity.h>
#include <cstring>
#include "modules/notification/notification_manager.h"
#include "core/event_bus.h"
#include "modules/storage/settings_manager.h"

#ifndef ESP32
#include "../common/arduino_mocks.h"
#include "../common/esp32_mocks.h"
#endif

using namespace adversary;
using namespace test_mocks;

void setUp() {
    NotificationManager::getInstance().init();
    NotificationManager::getInstance().clearQueue();
    // Default settings: enabled
    SettingsManager::getInstance().getMutable().system.notifySounds = true;
    SettingsManager::getInstance().getMutable().system.notifyLeds = true;
}

void tearDown() {
    // Cleanup
}

/**
 * @brief Test that EventBus signals are correctly mapped to physical notifications
 */
void test_notification_mapping() {
    NotificationManager& nm = NotificationManager::getInstance();
    EventBus& bus = EventBus::getInstance();
    
    // 1. Test Handshake Captured mapping
    EventData hsEvent(EventType::HANDSHAKE_CAPTURED);
    bus.publish(hsEvent);
    TEST_ASSERT_EQUAL(NotificationType::HANDSHAKE_COMPLETE, nm.getLastNotifiedType());
    
    // 2. Test PMKID Captured mapping
    EventData pmkidEvent(EventType::PMKID_CAPTURED);
    bus.publish(pmkidEvent);
    TEST_ASSERT_EQUAL(NotificationType::PMKID_CAPTURED, nm.getLastNotifiedType());
    
    // 3. Test Client Connected mapping
    EventData clientEvent(EventType::CLIENT_CONNECTED);
    bus.publish(clientEvent);
    TEST_ASSERT_EQUAL(NotificationType::EAPOL_PACKET, nm.getLastNotifiedType());
    
    // 4. Test BLE Connected mapping
    EventData bleEvent(EventType::BLE_CONNECTED);
    bus.publish(bleEvent);
    TEST_ASSERT_EQUAL(NotificationType::HANDSHAKE_COMPLETE, nm.getLastNotifiedType());
}

/**
 * @brief Test that notifications respect SystemSettings (Stealth Mode)
 */
void test_notification_suppression() {
    NotificationManager& nm = NotificationManager::getInstance();
    
    nm.clearQueue();
    
    // Disable all notifications
    SettingsManager::getInstance().getMutable().system.notifySounds = false;
    SettingsManager::getInstance().getMutable().system.notifyLeds = false;
    
    nm.notify(NotificationType::HANDSHAKE_COMPLETE);
    
    // Queue should be empty because notifications are globally suppressed
    TEST_ASSERT_EQUAL(0, nm.getQueueCount());
    
    // Re-enable LEDs only
    SettingsManager::getInstance().getMutable().system.notifyLeds = true;
    nm.notify(NotificationType::HANDSHAKE_COMPLETE);
    
    // Should now be in queue
    TEST_ASSERT_EQUAL(1, nm.getQueueCount());
}

/**
 * @brief Test that EAPOL Captured mapping exists
 */
void test_eapol_mapping() {
    NotificationManager& nm = NotificationManager::getInstance();
    EventBus& bus = EventBus::getInstance();
    
    EventData eapolEvent(EventType::EAPOL_CAPTURED);
    bus.publish(eapolEvent);
    TEST_ASSERT_EQUAL(NotificationType::EAPOL_PACKET, nm.getLastNotifiedType());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_notification_mapping);
    RUN_TEST(test_notification_suppression);
    RUN_TEST(test_eapol_mapping);
    
    return UNITY_END();
}
