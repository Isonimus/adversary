/**
 * @file test_ook_jam.cpp
 * @brief Regression for planJamNoise() (slice-0017).
 *
 * The jammer is otherwise pure I/O; the noise burst plan is its one host-testable
 * piece. A ms/µs units slip would make the burst 1000x too short or long, and an
 * out-of-range half-period must be rejected rather than silently driving a bad
 * timer — these tests pin both. Mirrors test_cc1101_freq: the pure math is tested,
 * the GDO0 drive is verified on-device.
 */

#include <unity.h>
#include "modules/rf/ook_rmt.h"

using adversary::rf::JamNoisePlan;
using adversary::rf::planJamNoise;
using adversary::rf::JAM_NOISE_HALF_PERIOD_US;
using adversary::rf::JAM_MAX_HALF_PERIOD_US;

void setUp(void) {}
void tearDown(void) {}

// 80 ms chunk at the shipped 20 µs half-period -> 80000 µs / 40 µs = 2000 cycles.
// This is the anchor: it is computed at compile time, proving purity, and catches
// a ms/µs mixup (which would give 2 cycles, not 2000).
static_assert(planJamNoise(80, 20).cycles == 2000, "80ms @ 20us must be 2000 cycles");
static_assert(planJamNoise(80, 20).valid, "80ms @ 20us is a valid plan");

void test_plan_anchor_80ms(void) {
    JamNoisePlan p = planJamNoise(80, 20);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_UINT32(2000, p.cycles);
}

void test_plan_scales_with_duration(void) {
    // Half the chunk -> half the cycles: guards the duration term.
    TEST_ASSERT_EQUAL_UINT32(1000, planJamNoise(40, 20).cycles);
    TEST_ASSERT_EQUAL_UINT32(25, planJamNoise(1, 20).cycles);
}

void test_plan_uses_shipped_half_period(void) {
    // The value the driver actually toggles at must produce a sane, valid plan.
    JamNoisePlan p = planJamNoise(80, JAM_NOISE_HALF_PERIOD_US);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_GREATER_THAN_UINT32(0, p.cycles);
}

void test_plan_rejects_zero_half_period(void) {
    JamNoisePlan p = planJamNoise(80, 0);
    TEST_ASSERT_FALSE(p.valid);
    TEST_ASSERT_EQUAL_UINT32(0, p.cycles);
}

void test_plan_rejects_oversized_half_period(void) {
    // A half-period past the 16-bit micro-timer field is invalid, not clamped.
    JamNoisePlan p = planJamNoise(80, JAM_MAX_HALF_PERIOD_US + 1);
    TEST_ASSERT_FALSE(p.valid);
}

void test_plan_rejects_zero_cycle_chunk(void) {
    // A chunk too short to complete one cycle is not a valid burst.
    JamNoisePlan p = planJamNoise(0, 20);
    TEST_ASSERT_FALSE(p.valid);
    TEST_ASSERT_EQUAL_UINT32(0, p.cycles);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plan_anchor_80ms);
    RUN_TEST(test_plan_scales_with_duration);
    RUN_TEST(test_plan_uses_shipped_half_period);
    RUN_TEST(test_plan_rejects_zero_half_period);
    RUN_TEST(test_plan_rejects_oversized_half_period);
    RUN_TEST(test_plan_rejects_zero_cycle_chunk);
    return UNITY_END();
}
