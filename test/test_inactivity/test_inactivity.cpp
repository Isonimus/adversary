/**
 * @file test_inactivity.cpp
 * @brief Regression for the pure two-stage inactivity policy (ADR-0116, slice-0117).
 *
 * inactivityStage() is the one host-testable seam of screen sleep: the backlight and input
 * wiring are device-only. It must stay ACTIVE when disabled (dim-after 0), reach DIMMED then
 * OFF at the two configured thresholds, treat off-after 0 as dim-only, and survive a millis()
 * wraparound (unsigned subtraction). Getting the truth table wrong either dims a screen the
 * operator is using or skips the readable stage straight to black.
 */

#include <unity.h>

#include "modules/system/inactivity.h"

using adversary::inactivityStage;
using adversary::ScreenSleepStage;
using adversary::SCREEN_SLEEP_DISABLED;

void setUp(void) {}
void tearDown(void) {}

// dim-after 0 (the default) disables the feature no matter how long idle.
void test_disabled_stays_active(void) {
    TEST_ASSERT_TRUE(ScreenSleepStage::ACTIVE ==
                     inactivityStage(1000000u, 0u, SCREEN_SLEEP_DISABLED, 0u));
}

// Dim after 30 s, off after 60 s: walk the three stages across the thresholds.
void test_three_stage_progression(void) {
    const uint16_t dim = 30, off = 60;
    TEST_ASSERT_TRUE(ScreenSleepStage::ACTIVE == inactivityStage(29000u, 0u, dim, off));
    TEST_ASSERT_TRUE(ScreenSleepStage::DIMMED == inactivityStage(30000u, 0u, dim, off));
    TEST_ASSERT_TRUE(ScreenSleepStage::DIMMED == inactivityStage(59000u, 0u, dim, off));
    TEST_ASSERT_TRUE(ScreenSleepStage::OFF    == inactivityStage(60000u, 0u, dim, off));
    TEST_ASSERT_TRUE(ScreenSleepStage::OFF    == inactivityStage(90000u, 0u, dim, off));
}

// off-after 0 means dim-only: the screen dims and never goes fully off.
void test_off_zero_is_dim_only(void) {
    TEST_ASSERT_TRUE(ScreenSleepStage::DIMMED == inactivityStage(300000u, 0u, 30u, 0u));
}

// Degenerate config (off < dim) must not strand the screen: OFF past its threshold wins,
// the readable stage is simply skipped. Callers clamp off >= dim, but the policy is safe.
void test_off_below_dim_prefers_off(void) {
    TEST_ASSERT_TRUE(ScreenSleepStage::OFF == inactivityStage(20000u, 0u, 30u, 10u));
}

// millis() wraparound: lastActivity just before the 32-bit rollover, now just after.
// Unsigned subtraction yields the true small elapsed time, so neither stage fires.
void test_wraparound_elapsed_is_small(void) {
    const uint32_t lastActivity = 0xFFFFFF00u;  // ~256 ms before rollover
    const uint32_t now = 0x00000100u;           // ~256 ms after rollover => ~512 ms elapsed
    TEST_ASSERT_TRUE(ScreenSleepStage::ACTIVE == inactivityStage(now, lastActivity, 30u, 60u));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_stays_active);
    RUN_TEST(test_three_stage_progression);
    RUN_TEST(test_off_zero_is_dim_only);
    RUN_TEST(test_off_below_dim_prefers_off);
    RUN_TEST(test_wraparound_elapsed_is_small);
    return UNITY_END();
}
