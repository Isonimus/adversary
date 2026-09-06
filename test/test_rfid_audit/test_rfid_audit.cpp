/**
 * @file test_rfid_audit.cpp
 * @brief Unit tests for RFID auditing engine
 */

#include <unity.h>
#include <functional>
#include "modules/rfid/rfid_audit.h"

#ifndef ESP32
#include "../common/arduino_mocks.h"
#include "../common/mfrc522_mocks.h"
#endif

using namespace adversary;

void setUp() {
}

void tearDown() {
}

void test_audit_dictionary_attack() {
    MFRC522_I2C rfid(0x28, -1);
    RfidAudit audit(&rfid);
    
    int progressCalls = 0;
    auto onProgress = [&](const AuditProgressInfo& info) {
        progressCalls++;
        // Check if progress is increasing or at least non-negative
        if (progressCalls > 1) {
            TEST_ASSERT_TRUE(info.overallProgress >= -0.001f);
        }
    };
    
    // Our mock returns OK for FF FF FF FF FF FF and A0 A1 A2 A3 A4 A5
    // Both are in the default key list.
    // s=0, try FF FF (Key A index 0) -> Found!
    // s=0, try FF FF (Key B index 0) -> Found!
    // ...
    // Total 16 sectors * 2 keys = 32 keys found.
    
    int result = audit.runDictionaryAttack(onProgress);
    
    TEST_ASSERT_EQUAL(32, result);
    TEST_ASSERT_TRUE(progressCalls > 32);
    
    const auto& results = audit.getResults();
    TEST_ASSERT_EQUAL(16, results.size());
    TEST_ASSERT_TRUE(results[0].keyAFound);
    TEST_ASSERT_TRUE(results[0].keyBFound);
}

void test_audit_incompatible_tag() {
    MFRC522_I2C rfid(0x28, -1);
    // Set SAK to 0x20 (ISO 14443-4, e.g. DESFire)
    rfid.uid.sak = 0x20;
    
    RfidAudit audit(&rfid);
    int result = audit.runDictionaryAttack([](const AuditProgressInfo&){});
    
    // Should return -1 for incompatible type
    TEST_ASSERT_EQUAL(-1, result);
}

void test_audit_results_clear() {
    MFRC522_I2C rfid(0x28, -1);
    RfidAudit audit(&rfid);
    
    // First run (compatible tag)
    audit.runDictionaryAttack([](const AuditProgressInfo&){});
    TEST_ASSERT_TRUE(audit.getResults().size() > 0);
    
    // Second run with incompatible tag
    rfid.uid.sak = 0x20; 
    audit.runDictionaryAttack([](const AuditProgressInfo&){});
    
    // Results should be cleared
    TEST_ASSERT_EQUAL(0, audit.getResults().size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_audit_dictionary_attack);
    RUN_TEST(test_audit_incompatible_tag);
    RUN_TEST(test_audit_results_clear);
    return UNITY_END();
}
