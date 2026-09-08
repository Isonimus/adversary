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
    g_mockMfrcVersion = 0x92;  // default: reader present, unless a test says otherwise
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

// slice-0018: redetect() must re-probe in BOTH directions, unlike init() which
// short-circuits on m_detected. These are the regression for the Modules
// dashboard's Re-scan re-greying a removed reader / finding a hot-inserted one.

void test_rfid_redetect_sees_removal() {
    RFIDManager& mgr = RFIDManager::getInstance();

    // Present at first probe.
    TEST_ASSERT_TRUE(mgr.init());
    TEST_ASSERT_TRUE(mgr.isDetected());

    // Reader unplugged -> bus reads the absent 0xFF. init() alone would keep
    // reporting detected (its `if (m_detected) return true` guard never re-probes);
    // redetect() must clear the verdict.
    g_mockMfrcVersion = 0xFF;
    TEST_ASSERT_FALSE(mgr.redetect());
    TEST_ASSERT_FALSE(mgr.isDetected());
}

void test_rfid_redetect_sees_insertion() {
    RFIDManager& mgr = RFIDManager::getInstance();

    // Absent at boot.
    g_mockMfrcVersion = 0x00;
    TEST_ASSERT_FALSE(mgr.init());
    TEST_ASSERT_FALSE(mgr.isDetected());

    // Reader hot-inserted -> a fresh probe finds it.
    g_mockMfrcVersion = 0x92;
    TEST_ASSERT_TRUE(mgr.redetect());
    TEST_ASSERT_TRUE(mgr.isDetected());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rfid_manager_initial_state);
    RUN_TEST(test_rfid_manager_init_lifecycle);
    RUN_TEST(test_rfid_manager_activation_logic);
    RUN_TEST(test_rfid_manager_polling_respects_active_state);
    RUN_TEST(test_rfid_manager_clear_tag);
    RUN_TEST(test_rfid_redetect_sees_removal);
    RUN_TEST(test_rfid_redetect_sees_insertion);
    return UNITY_END();
}
