/**
 * @file test_event_bus.cpp
 * @brief Unit tests for the core EventBus pub/sub system
 */

#include <unity.h>
#include "core/event_bus.h"
#include <vector>

using namespace adversary;

// Test helper to track calls
struct MockHandler {
    int callCount = 0;
    EventData lastData;
    
    void handle(const EventData& data) {
        callCount++;
        lastData = data;
    }
};

static MockHandler handler1;
static MockHandler handler2;

void setUp(void) {
    handler1.callCount = 0;
    handler2.callCount = 0;
    EventBus::getInstance().reset();
}

void tearDown(void) {
    // EventBus is a singleton, so we should try to clear subscriptions 
    // to avoid cross-test contamination if possible, though EventBus doesn't have a clearAll.
    // We'll rely on unique IDs or singleton state management if needed.
}

// 1. test_subscribe_returns_valid_id
void test_subscribe_returns_valid_id() {
    EventBus& bus = EventBus::getInstance();
    EventBus::HandlerId id = bus.subscribe(EventType::WIFI_SCAN_COMPLETED, [](const EventData&){});
    TEST_ASSERT_NOT_EQUAL(EventBus::INVALID_HANDLER_ID, id);
    bus.unsubscribe(id);
}

// 2. test_publish_invokes_handler
void test_publish_invokes_handler() {
    EventBus& bus = EventBus::getInstance();
    EventBus::HandlerId id = bus.subscribe(EventType::WIFI_SCAN_COMPLETED, [](const EventData& d){
        handler1.handle(d);
    });
    
    EventData data(EventType::WIFI_SCAN_COMPLETED);
    data.payload.scan.count = 42;
    
    bus.publish(data);
    
    TEST_ASSERT_EQUAL(1, handler1.callCount);
    TEST_ASSERT_EQUAL(42, handler1.lastData.payload.scan.count);
    
    bus.unsubscribe(id);
}

// 3. test_publish_multiple_subscribers
void test_publish_multiple_subscribers() {
    EventBus& bus = EventBus::getInstance();
    auto id1 = bus.subscribe(EventType::SCREEN_CHANGED, [](const EventData& d){ handler1.handle(d); });
    auto id2 = bus.subscribe(EventType::SCREEN_CHANGED, [](const EventData& d){ handler2.handle(d); });
    
    bus.publish(EventData(EventType::SCREEN_CHANGED));
    
    TEST_ASSERT_EQUAL(1, handler1.callCount);
    TEST_ASSERT_EQUAL(1, handler2.callCount);
    
    bus.unsubscribe(id1);
    bus.unsubscribe(id2);
}

// 4. test_publish_wrong_type_not_invoked
void test_publish_wrong_type_not_invoked() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::WIFI_SCAN_COMPLETED, [](const EventData& d){ handler1.handle(d); });
    
    bus.publish(EventData(EventType::BLE_DEVICE_FOUND));
    
    TEST_ASSERT_EQUAL(0, handler1.callCount);
    
    bus.unsubscribe(id);
}

// 5. test_unsubscribe_stops_delivery
void test_unsubscribe_stops_delivery() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::WIFI_SCAN_COMPLETED, [](const EventData& d){ handler1.handle(d); });
    
    bus.publish(EventData(EventType::WIFI_SCAN_COMPLETED));
    TEST_ASSERT_EQUAL(1, handler1.callCount);
    
    bus.unsubscribe(id);
    bus.publish(EventData(EventType::WIFI_SCAN_COMPLETED));
    TEST_ASSERT_EQUAL(1, handler1.callCount); // Still 1
}

// 6. test_unsubscribe_invalid_id
void test_unsubscribe_invalid_id() {
    EventBus& bus = EventBus::getInstance();
    bool result = bus.unsubscribe(EventBus::INVALID_HANDLER_ID);
    TEST_ASSERT_FALSE(result);
    
    result = bus.unsubscribe(9999); // Likely invalid ID
    TEST_ASSERT_FALSE(result);
}

// 7. test_queue_and_processQueue
void test_queue_and_processQueue() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::SCREEN_CHANGED, [](const EventData& d){ handler1.handle(d); });
    
    bus.queue(EventData(EventType::SCREEN_CHANGED));
    TEST_ASSERT_EQUAL(0, handler1.callCount);
    
    bus.processQueue();
    TEST_ASSERT_EQUAL(1, handler1.callCount);
    
    bus.unsubscribe(id);
}

// 8. test_queue_not_delivered_before_process
void test_queue_not_delivered_before_process() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::SCREEN_CHANGED, [](const EventData& d){ handler1.handle(d); });
    
    bus.queue(EventData(EventType::SCREEN_CHANGED));
    bus.queue(EventData(EventType::SCREEN_CHANGED));
    
    TEST_ASSERT_EQUAL(0, handler1.callCount);
    TEST_ASSERT_EQUAL(2, bus.getQueueSize());
    
    bus.processQueue();
    TEST_ASSERT_EQUAL(2, handler1.callCount);
    TEST_ASSERT_EQUAL(0, bus.getQueueSize());
    
    bus.unsubscribe(id);
}

// 9. test_getSubscriberCount
void test_getSubscriberCount() {
    EventBus& bus = EventBus::getInstance();
    size_t initial = bus.getSubscriberCount(EventType::GPS_FIX_ACQUIRED);
    
    auto id1 = bus.subscribe(EventType::GPS_FIX_ACQUIRED, [](const EventData&){});
    TEST_ASSERT_EQUAL(initial + 1, bus.getSubscriberCount(EventType::GPS_FIX_ACQUIRED));
    
    auto id2 = bus.subscribe(EventType::GPS_FIX_ACQUIRED, [](const EventData&){});
    TEST_ASSERT_EQUAL(initial + 2, bus.getSubscriberCount(EventType::GPS_FIX_ACQUIRED));
    
    bus.unsubscribe(id1);
    TEST_ASSERT_EQUAL(initial + 1, bus.getSubscriberCount(EventType::GPS_FIX_ACQUIRED));
    
    bus.unsubscribe(id2);
}

// 10. test_getTotalSubscriptions
void test_getTotalSubscriptions() {
    EventBus& bus = EventBus::getInstance();
    size_t initial = bus.getTotalSubscriptions();
    
    auto id1 = bus.subscribe(EventType::GPS_FIX_ACQUIRED, [](const EventData&){});
    auto id2 = bus.subscribe(EventType::WIFI_SCAN_COMPLETED, [](const EventData&){});
    
    TEST_ASSERT_EQUAL(initial + 2, bus.getTotalSubscriptions());
    
    bus.unsubscribe(id1);
    bus.unsubscribe(id2);
}

// 11. test_getQueueSize
void test_getQueueSize() {
    EventBus& bus = EventBus::getInstance();
    bus.processQueue(); // Clear any leftovers
    TEST_ASSERT_EQUAL(0, bus.getQueueSize());
    
    bus.queue(EventData(EventType::SCREEN_CHANGED));
    TEST_ASSERT_EQUAL(1, bus.getQueueSize());
    
    bus.processQueue();
    TEST_ASSERT_EQUAL(0, bus.getQueueSize());
}

// 12. test_event_data_payload_integrity
void test_event_data_payload_integrity() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::HANDSHAKE_CAPTURED, [](const EventData& d){
        handler1.handle(d);
    });
    
    EventData data(EventType::HANDSHAKE_CAPTURED);
    strncpy(data.payload.handshake.ssid, "IntegrityTest", 32);
    data.payload.handshake.channel = 11;
    
    bus.publish(data);
    
    TEST_ASSERT_EQUAL_STRING("IntegrityTest", handler1.lastData.payload.handshake.ssid);
    TEST_ASSERT_EQUAL(11, handler1.lastData.payload.handshake.channel);
    
    bus.unsubscribe(id);
}

// 13. test_attack_target_payload_integrity
// Guards the ATTACK_TARGET_SELECTED contract added in slice-0028: the Scanner/Sniffer
// publish this event to launch an attack, and the navigator reads back every field. The
// targetScreen field exercises the new AttackTargetEventData union member (a raw ScreenId-
// shaped int16_t is used so this native test pulls in no UI/hardware headers).
void test_attack_target_payload_integrity() {
    EventBus& bus = EventBus::getInstance();
    auto id = bus.subscribe(EventType::ATTACK_TARGET_SELECTED, [](const EventData& d){
        handler1.handle(d);
    });

    const int16_t targetScreen = 4242;  // arbitrary ScreenId-shaped sentinel
    const uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    EventData data(EventType::ATTACK_TARGET_SELECTED);
    data.payload.attackTarget.targetScreen = targetScreen;
    memcpy(data.payload.attackTarget.bssid, bssid, 6);
    strncpy(data.payload.attackTarget.ssid, "TargetNet", 32);
    data.payload.attackTarget.channel = 6;

    bus.publish(data);

    TEST_ASSERT_EQUAL(1, handler1.callCount);
    TEST_ASSERT_EQUAL_INT16(targetScreen, handler1.lastData.payload.attackTarget.targetScreen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, handler1.lastData.payload.attackTarget.bssid, 6);
    TEST_ASSERT_EQUAL_STRING("TargetNet", handler1.lastData.payload.attackTarget.ssid);
    TEST_ASSERT_EQUAL(6, handler1.lastData.payload.attackTarget.channel);

    bus.unsubscribe(id);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_subscribe_returns_valid_id);
    RUN_TEST(test_publish_invokes_handler);
    RUN_TEST(test_publish_multiple_subscribers);
    RUN_TEST(test_publish_wrong_type_not_invoked);
    RUN_TEST(test_unsubscribe_stops_delivery);
    RUN_TEST(test_unsubscribe_invalid_id);
    RUN_TEST(test_queue_and_processQueue);
    RUN_TEST(test_queue_not_delivered_before_process);
    RUN_TEST(test_getSubscriberCount);
    RUN_TEST(test_getTotalSubscriptions);
    RUN_TEST(test_getQueueSize);
    RUN_TEST(test_event_data_payload_integrity);
    RUN_TEST(test_attack_target_payload_integrity);
    return UNITY_END();
}
