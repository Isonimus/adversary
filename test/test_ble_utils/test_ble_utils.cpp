/**
 * @file test_ble_utils.cpp
 * @brief Unit tests for BLE utility functions
 */

#include <unity.h>
#include "modules/ble/ble_utils.h"
#include <string>

#ifdef UNIT_TEST
#include "../../src/modules/ble/ble_utils.cpp"
#endif

using namespace adversary::ble;

void setUp() {
}

void tearDown() {
}

void test_addressToString_conversion() {
    uint8_t rawAddr[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    // NimBLEAddress ctor for raw bytes (from our mock improvement)
    NimBLEAddress addr(rawAddr);
    
    // Note: implementation does val[5]...val[0] which is standard for MACs in BLE
    std::string result = addressToString(addr);
    TEST_ASSERT_EQUAL_STRING("06:05:04:03:02:01", result.c_str());
}

void test_getManufacturerName_lookup() {
    TEST_ASSERT_EQUAL_STRING("Apple", getManufacturerName(0x004C));
    TEST_ASSERT_EQUAL_STRING("Google", getManufacturerName(0x00E0));
    TEST_ASSERT_EQUAL_STRING("Microsoft", getManufacturerName(0x0006));
    TEST_ASSERT_EQUAL_STRING("Samsung", getManufacturerName(0x00D2));
    TEST_ASSERT_EQUAL_STRING("Unknown", getManufacturerName(0xFFFF));
}

void test_isFastPair_detection_via_service_uuid() {
    NimBLEAdvertisedDevice device;
    
    // Test false initially
    TEST_ASSERT_FALSE(isFastPair(&device));
    
    // Add Fast Pair service UUID (0xFE2C)
    device.addServiceUUID(NimBLEUUID(0xFE2C));
    TEST_ASSERT_TRUE(isFastPair(&device));
}

void test_isFastPair_detection_via_service_data() {
    NimBLEAdvertisedDevice device;
    
    // Add Fast Pair service data UUID (0xFE2C)
    device.addServiceData(NimBLEUUID(0xFE2C), "data");
    TEST_ASSERT_TRUE(isFastPair(&device));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_addressToString_conversion);
    RUN_TEST(test_getManufacturerName_lookup);
    RUN_TEST(test_isFastPair_detection_via_service_uuid);
    RUN_TEST(test_isFastPair_detection_via_service_data);
    
    return UNITY_END();
}
