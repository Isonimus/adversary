/**
 * @file test_wigle_service.cpp
 * @brief Unit tests for WigleService
 */

#include <unity.h>
#ifndef ESP32
#include "../common/arduino_mocks.h"
#include "../common/SD.h"
#endif

#include "modules/network/wigle_service.h"
#include "modules/storage/settings_manager.h"
#include "modules/wardriving/wardriving_config.h"
#include <cstdio>
#include <fstream>

using namespace adversary;

void setUp(void) {
    SettingsManager::getInstance().reset();
    // Create wardriving dir for tests (recursive)
    SD.mkdir("/adversary");
    SD.mkdir("/adversary/captures");
    SD.mkdir("/adversary/captures/wardriving");
}

void tearDown(void) {
    // Clean up wardriving dir
    // (In a real mock we'd rmdir, but here we just leave it for now)
}

void test_wigle_status_logic(void) {
    WigleService& service = WigleService::getInstance();
    const char* testFile = "session_001.csv";
    
    // 1. Initial status should be NOT_UPLOADED
    TEST_ASSERT_EQUAL(WigleStatus::NOT_UPLOADED, service.getStatus(testFile));
    
    // 2. Mock a sidecar file
    char sidecarPath[128];
    snprintf(sidecarPath, sizeof(sidecarPath), "%s/session_001.wigle", wardriving::WARDRIVING_DIR);
    
    FILE* f = fopen(sidecarPath + 1, "w"); // +1 to skip leading slash for host-side access
    if (f) {
        fprintf(f, "uploaded\n");
        fclose(f);
    }
    
    // 3. Status should now be UPLOADED
    TEST_ASSERT_EQUAL(WigleStatus::UPLOADED, service.getStatus(testFile));
    
    // Cleanup
    SD.remove(sidecarPath);
}

void test_wigle_has_api_key(void) {
    WigleService& service = WigleService::getInstance();
    
    TEST_ASSERT_FALSE(service.hasApiKey());
    
    SettingsManager::getInstance().setWigleKey("YXBpX3VzZXI6YXBpX3Rva2Vu");
    TEST_ASSERT_TRUE(service.hasApiKey());
    
    SettingsManager::getInstance().setWigleKey(nullptr);
    TEST_ASSERT_FALSE(service.hasApiKey());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wigle_status_logic);
    RUN_TEST(test_wigle_has_api_key);
    return UNITY_END();
}
