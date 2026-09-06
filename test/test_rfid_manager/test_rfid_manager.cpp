/**
 * @file test_rfid_manager.cpp
 * @brief Unit tests for RFIDManager lifecycle and power management
 */

#include <unity.h>
#include "modules/rfid/rfid_manager.h"
#include <cstring>

using namespace adversary;

void setUp() {
    RFIDManager::getInstance().resetForTest();
}

void tearDown() {
}

void test_rfid_manager_initial_state() {
    RFIDManager& mgr = RFIDManager::getInstance();
    
    TEST_ASSERT_FALSE(mgr.isDetected());
    TEST_ASSERT_FALSE(mgr.isActive());
    TEST_ASSERT_NULL(mgr.getLastTag());
}

void test_rfid_manager_init_lifecycle() {
    RFIDManager& mgr = RFIDManager::getInstance();
    
    // init() should detect the mock and set active=false
    bool detected = mgr.init();
    TEST_ASSERT_TRUE(detected);
    TEST_ASSERT_TRUE(mgr.isDetected());
    TEST_ASSERT_FALSE(mgr.isActive()); // Should be OFF by default
}

void test_rfid_manager_activation_logic() {
    RFIDManager& mgr = RFIDManager::getInstance();
    mgr.init();
    
    mgr.activate();
    TEST_ASSERT_TRUE(mgr.isActive());
    
    mgr.deactivate();
    TEST_ASSERT_FALSE(mgr.isActive());
}

void test_rfid_manager_polling_respects_active_state() {
    RFIDManager& mgr = RFIDManager::getInstance();
    mgr.init();
    
    // Inactive - update should do nothing
    mgr.update();
    TEST_ASSERT_NULL(mgr.getLastTag());
    
    // Activate - update should now find the tag (mock always reports tag)
    mgr.activate();
    mgr.update();
    TEST_ASSERT_NOT_NULL(mgr.getLastTag());
    TEST_ASSERT_EQUAL_STRING("DEDEDEDE", mgr.getLastTag()->uid.c_str());
    
    // Deactivate - should stop finding NEW tags (though lastTag might persist until clearTag)
    mgr.deactivate();
    // (Note: update returns early if !m_active)
}

void test_rfid_manager_clear_tag() {
    RFIDManager& mgr = RFIDManager::getInstance();
    mgr.init();
    mgr.activate();
    mgr.update();
    
    TEST_ASSERT_NOT_NULL(mgr.getLastTag());
    
    mgr.clearTag();
    TEST_ASSERT_NULL(mgr.getLastTag());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rfid_manager_initial_state);
    RUN_TEST(test_rfid_manager_init_lifecycle);
    RUN_TEST(test_rfid_manager_activation_logic);
    RUN_TEST(test_rfid_manager_polling_respects_active_state);
    RUN_TEST(test_rfid_manager_clear_tag);
    return UNITY_END();
}
