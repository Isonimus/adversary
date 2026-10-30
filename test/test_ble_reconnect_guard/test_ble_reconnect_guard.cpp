/**
 * @file test_ble_reconnect_guard.cpp
 * @brief Unit tests for the BadBLE reconnect-storm detector (ADR-0134).
 */

#include <unity.h>

#include "utils/ble_reconnect_guard.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// A small, deterministic guard: 3 failures within 1000 ms is a storm.
static ReconnectChurnGuard makeGuard() { return ReconnectChurnGuard(3, 1000); }

void test_single_failure_does_not_trip(void) {
    ReconnectChurnGuard g = makeGuard();
    TEST_ASSERT_FALSE(g.recordFailure(0));
    TEST_ASSERT_EQUAL_UINT8(1, g.failureCount());
}

void test_trips_exactly_at_threshold_within_window(void) {
    ReconnectChurnGuard g = makeGuard();
    TEST_ASSERT_FALSE(g.recordFailure(0));
    TEST_ASSERT_FALSE(g.recordFailure(200));
    // The third failure inside the window is the storm.
    TEST_ASSERT_TRUE(g.recordFailure(400));
    TEST_ASSERT_EQUAL_UINT8(3, g.failureCount());
}

void test_failures_spread_beyond_window_never_trip(void) {
    ReconnectChurnGuard g = makeGuard();
    // Each failure lands more than one window after the previous, so each opens a fresh window.
    TEST_ASSERT_FALSE(g.recordFailure(0));
    TEST_ASSERT_FALSE(g.recordFailure(1500));
    TEST_ASSERT_FALSE(g.recordFailure(3000));
    TEST_ASSERT_FALSE(g.recordFailure(4500));
    TEST_ASSERT_EQUAL_UINT8(1, g.failureCount());
}

void test_gap_resets_the_window(void) {
    ReconnectChurnGuard g = makeGuard();
    TEST_ASSERT_FALSE(g.recordFailure(0));
    TEST_ASSERT_FALSE(g.recordFailure(300));
    // A gap past the window drops us back to a one-failure fresh window, not a trip.
    TEST_ASSERT_FALSE(g.recordFailure(2000));
    TEST_ASSERT_EQUAL_UINT8(1, g.failureCount());
}

void test_reset_clears_the_count(void) {
    ReconnectChurnGuard g = makeGuard();
    g.recordFailure(0);
    g.recordFailure(100);
    g.reset();
    TEST_ASSERT_EQUAL_UINT8(0, g.failureCount());
    // After a reset the next failure opens a fresh window and does not trip.
    TEST_ASSERT_FALSE(g.recordFailure(150));
}

void test_stays_tripped_while_storm_continues(void) {
    ReconnectChurnGuard g = makeGuard();
    g.recordFailure(0);
    g.recordFailure(100);
    TEST_ASSERT_TRUE(g.recordFailure(200));
    // Further failures inside the window keep reporting the storm.
    TEST_ASSERT_TRUE(g.recordFailure(300));
}

void test_window_boundary_is_inclusive(void) {
    ReconnectChurnGuard g = makeGuard();
    TEST_ASSERT_FALSE(g.recordFailure(0));
    // Exactly at the window edge (delta == windowMs) is still the same window, not past it.
    TEST_ASSERT_FALSE(g.recordFailure(1000));
    TEST_ASSERT_TRUE(g.recordFailure(1000));
}

void test_millis_rollover_is_handled(void) {
    ReconnectChurnGuard g = makeGuard();
    const uint32_t start = 0xFFFFFF00u;  // 256 ms before the 32-bit rollover
    TEST_ASSERT_FALSE(g.recordFailure(start));
    // These timestamps wrap past zero; unsigned subtraction keeps the elapsed delta correct, so
    // the three still count as one window and trip the storm.
    TEST_ASSERT_FALSE(g.recordFailure((uint32_t)(start + 300u)));  // wraps to 0x0000002C
    TEST_ASSERT_TRUE(g.recordFailure((uint32_t)(start + 600u)));   // wraps to 0x00000158
}

void test_default_threshold_takes_four(void) {
    ReconnectChurnGuard g;  // defaults: BLE_RECONNECT_STORM_THRESHOLD within WINDOW_MS
    TEST_ASSERT_FALSE(g.recordFailure(0));
    TEST_ASSERT_FALSE(g.recordFailure(100));
    TEST_ASSERT_FALSE(g.recordFailure(200));
    TEST_ASSERT_TRUE(g.recordFailure(300));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_failure_does_not_trip);
    RUN_TEST(test_trips_exactly_at_threshold_within_window);
    RUN_TEST(test_failures_spread_beyond_window_never_trip);
    RUN_TEST(test_gap_resets_the_window);
    RUN_TEST(test_reset_clears_the_count);
    RUN_TEST(test_stays_tripped_while_storm_continues);
    RUN_TEST(test_window_boundary_is_inclusive);
    RUN_TEST(test_millis_rollover_is_handled);
    RUN_TEST(test_default_threshold_takes_four);
    return UNITY_END();
}
