/**
 * @file test_ir_blaster.cpp
 * @brief Unit tests for TV-B-Gone IR Blaster
 */

#include <unity.h>
#include "modules/ir/tvbgone_blaster.h"
#include <cstring>

#ifndef ESP32
#include "../common/arduino_mocks.h"
#include "../common/ir_mocks.h"
#endif

using namespace adversary::ir;

// Access private members for testing via a pointer if needed, 
// but here we can mostly use public methods and the mock.
// We need to inject the mock if it's dynamic, but TvBGoneBlaster 
// owns its IRsend pointer.

void setUp() {
    TvBGoneBlaster::getInstance().stop();
    setMockMillis(0);
}

void tearDown() {
}

void test_blaster_initialization() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();
    blaster.init();
    
    // Check if it's not blasting initially
    TEST_ASSERT_FALSE(blaster.isBlasting());
    TEST_ASSERT_EQUAL(0, blaster.getCurrentIndex());
    TEST_ASSERT_EQUAL(0, blaster.getTotalCodes());
}

void test_blaster_start_region() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();
    blaster.init();
    
    // Start for North America
    blaster.start(Region::NORTH_AMERICA);
    TEST_ASSERT_TRUE(blaster.isBlasting());
    TEST_ASSERT_EQUAL(Region::NORTH_AMERICA, blaster.getRegion());
    TEST_ASSERT_TRUE(blaster.getTotalCodes() > 0);
    
    // Index should move after first call in start()
    TEST_ASSERT_EQUAL(1, blaster.getCurrentIndex());
    
    blaster.stop();
    TEST_ASSERT_FALSE(blaster.isBlasting());
}

void test_blaster_progress() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();
    blaster.start(Region::NORTH_AMERICA);
    
    float progress = blaster.getProgress();
    TEST_ASSERT_TRUE(progress > 0.0f);
    TEST_ASSERT_TRUE(progress <= 1.0f);
    
    blaster.stop();
}

void test_blaster_update_cycle() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();
    blaster.start(Region::NORTH_AMERICA);
    int initialIdx = blaster.getCurrentIndex(); // Should be 1
    
    // Mock time to move forward
    setMockMillis(100);
    blaster.update();
    // 100ms < 300ms, should NOT have advanced
    TEST_ASSERT_EQUAL(initialIdx, blaster.getCurrentIndex());
    
    setMockMillis(500);
    blaster.update();
    // 500ms > 300ms, should have advanced
    TEST_ASSERT_TRUE(blaster.getCurrentIndex() > initialIdx);
    
    blaster.stop();
}

void test_blaster_stop_resets_index() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();
    blaster.start(Region::NORTH_AMERICA);
    
    // Simulate some work
    setMockMillis(1000);
    blaster.update();
    TEST_ASSERT_TRUE(blaster.getCurrentIndex() > 0);
    
    blaster.stop();
    TEST_ASSERT_FALSE(blaster.isBlasting());
    TEST_ASSERT_EQUAL(0, blaster.getCurrentIndex());
}

void test_blaster_invalid_region() {
    TvBGoneBlaster& blaster = TvBGoneBlaster::getInstance();

    // Test that an invalid region doesn't cause a crash and doesn't start blasting
    // (Assuming Region is an enum, we cast an invalid value)
    blaster.stop();
    blaster.start(static_cast<Region>(99));

    TEST_ASSERT_FALSE(blaster.isBlasting());
}

// Regression: a Sony-range code must be emitted exactly SONY_REPEATS times, all at
// the carrier in Hz. The previous inline code sent each Sony code twice per repeat
// (six emissions), one of them at freq/1000 (~38 Hz) — a nonsense carrier.
void test_emitIrBurst_sony_sends_three_times_at_carrier_hz() {
    IRsend sender(0);
    const uint16_t code[] = {600, 600, 1200, 600};
    const uint32_t sonyHz = 38400;

    emitIrBurst(sender, code, 4, sonyHz);

    const auto& calls = sender.getCalls();
    TEST_ASSERT_EQUAL_UINT(SONY_REPEATS, calls.size());
    for (const auto& call : calls) {
        TEST_ASSERT_EQUAL_UINT32(sonyHz, call.freq);        // never the truncated carrier
        TEST_ASSERT_EQUAL_UINT16(4, call.len);
        TEST_ASSERT_NOT_EQUAL(sonyHz / 1000, call.freq);    // the old freq/1000 bug
    }
}

// A non-Sony code is emitted once, at its carrier.
void test_emitIrBurst_non_sony_sends_once() {
    IRsend sender(0);
    const uint16_t code[] = {560, 560};
    const uint32_t hz = 56000;  // outside the Sony 38-40 kHz range

    emitIrBurst(sender, code, 2, hz);

    const auto& calls = sender.getCalls();
    TEST_ASSERT_EQUAL_UINT(1, calls.size());
    TEST_ASSERT_EQUAL_UINT32(hz, calls[0].freq);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_blaster_initialization);
    RUN_TEST(test_blaster_start_region);
    RUN_TEST(test_blaster_progress);
    RUN_TEST(test_blaster_update_cycle);
    RUN_TEST(test_blaster_stop_resets_index);
    RUN_TEST(test_blaster_invalid_region);
    RUN_TEST(test_emitIrBurst_sony_sends_three_times_at_carrier_hz);
    RUN_TEST(test_emitIrBurst_non_sony_sends_once);
    return UNITY_END();
}
