/**
 * @file test_inactivity.cpp
 * @brief Regression for the pure inactivity-dim policy (ADR-0114, slice-0115).
 *
 * inactivityShouldDim() is the one host-testable seam of screen sleep: the backlight
 * and input wiring are device-only. It must dim only on the transition into the dimmed
 * state, treat timeout 0 as "never", not re-dim when already dark, and survive a millis()
 * wraparound (unsigned subtraction). Getting the truth table wrong either dims a screen the
 * operator is actively using or never dims at all, defeating the battery saving.
 */

#include <unity.h>

#include "modules/system/inactivity.h"

using adversary::inactivityShouldDim;
using adversary::SCREEN_SLEEP_DISABLED;

void setUp(void) {}
void tearDown(void) {}

// Timeout 0 (the default) disables dimming no matter how long idle.
void test_disabled_never_dims(void) {
    TEST_ASSERT_FALSE(inactivityShouldDim(1000000u, 0u, SCREEN_SLEEP_DISABLED, false));
}

// Before the timeout elapses, the screen stays lit.
void test_does_not_dim_before_timeout(void) {
    // 29 s idle, 30 s timeout: not yet.
    TEST_ASSERT_FALSE(inactivityShouldDim(29000u, 0u, 30u, false));
}

// At and past the timeout, the dim transition fires.
void test_dims_at_and_after_timeout(void) {
    TEST_ASSERT_TRUE(inactivityShouldDim(30000u, 0u, 30u, false));
    TEST_ASSERT_TRUE(inactivityShouldDim(45000u, 0u, 30u, false));
}

// Already dimmed: the predicate must not re-fire (the dim is a one-shot transition;
// the input hook, not this predicate, clears the dimmed state).
void test_does_not_redim_when_already_dimmed(void) {
    TEST_ASSERT_FALSE(inactivityShouldDim(999999u, 0u, 30u, true));
}

// millis() wraparound: lastActivity just before the 32-bit rollover, now just after.
// Unsigned subtraction yields the true small elapsed time, so a 30 s timer does not
// dim one tick after a 49-day rollover.
void test_wraparound_elapsed_is_small(void) {
    const uint32_t lastActivity = 0xFFFFFF00u;  // ~256 ms before rollover
    const uint32_t now = 0x00000100u;           // ~256 ms after rollover => ~512 ms elapsed
    TEST_ASSERT_FALSE(inactivityShouldDim(now, lastActivity, 30u, false));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_never_dims);
    RUN_TEST(test_does_not_dim_before_timeout);
    RUN_TEST(test_dims_at_and_after_timeout);
    RUN_TEST(test_does_not_redim_when_already_dimmed);
    RUN_TEST(test_wraparound_elapsed_is_small);
    return UNITY_END();
}
