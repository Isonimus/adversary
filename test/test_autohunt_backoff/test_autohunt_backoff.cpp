/**
 * @file test_autohunt_backoff.cpp
 * @brief Unit tests for the Autohunt per-target exponential backoff (ADR-0161).
 *
 * The clock is hand-driven: `now` is a plain `uint32_t`, no millis(). This covers
 * the cooldown schedule (60s doubling to a 15-min cap), the `isInBackoff` gate,
 * the capture-resets-attempts behaviour, and the millis() wrap handling.
 *
 * Regression guard: dropping the shift cap in `nextEligibleAfterMiss` overflows at
 * `attempts >= 32` and fails the cap test below.
 */

#include <unity.h>

#include "utils/autohunt_scoring.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// ---------- Schedule ----------

void test_first_miss_is_sixty_seconds(void) {
    TEST_ASSERT_EQUAL_UINT32(1000u + 60u * 1000u, nextEligibleAfterMiss(1, 1000u));
}

void test_doubling_schedule_sixty_to_cap(void) {
    const uint32_t now = 0;
    TEST_ASSERT_EQUAL_UINT32( 60u * 1000u, nextEligibleAfterMiss(1, now));
    TEST_ASSERT_EQUAL_UINT32(120u * 1000u, nextEligibleAfterMiss(2, now));
    TEST_ASSERT_EQUAL_UINT32(240u * 1000u, nextEligibleAfterMiss(3, now));
    TEST_ASSERT_EQUAL_UINT32(480u * 1000u, nextEligibleAfterMiss(4, now));
    // Fifth miss: 60 << 4 == 960 s, clamped to MAX_COOLDOWN_MS (900 s).
    TEST_ASSERT_EQUAL_UINT32(MAX_COOLDOWN_MS, nextEligibleAfterMiss(5, now));
    // Sixth and beyond: capped at MAX_COOLDOWN_MS, no overflow.
    TEST_ASSERT_EQUAL_UINT32(MAX_COOLDOWN_MS, nextEligibleAfterMiss(6, now));
    TEST_ASSERT_EQUAL_UINT32(MAX_COOLDOWN_MS, nextEligibleAfterMiss(32, now));
    TEST_ASSERT_EQUAL_UINT32(MAX_COOLDOWN_MS, nextEligibleAfterMiss(255, now));
}

void test_defensive_zero_attempts_treated_as_one(void) {
    // A caller that somehow records zero still gets the first-miss schedule.
    TEST_ASSERT_EQUAL_UINT32(60u * 1000u, nextEligibleAfterMiss(0, 0));
}

// ---------- isInBackoff ----------

void test_fresh_slot_is_not_in_backoff(void) {
    TargetMemory slot{};
    TEST_ASSERT_FALSE(isInBackoff(slot, 100));
}

void test_in_backoff_true_before_eligibility(void) {
    TargetMemory slot{};
    slot.nextEligibleMs = 1000;
    TEST_ASSERT_TRUE(isInBackoff(slot, 999));
}

void test_in_backoff_false_at_or_after_eligibility(void) {
    TargetMemory slot{};
    slot.nextEligibleMs = 1000;
    TEST_ASSERT_FALSE(isInBackoff(slot, 1000));
    TEST_ASSERT_FALSE(isInBackoff(slot, 1001));
}

void test_in_backoff_survives_millis_wrap(void) {
    // Pre-wrap: now is near UINT32_MAX, nextEligibleMs just past the wrap. Without the
    // signed-delta handling, (now - nextEligibleMs) would be a huge unsigned positive
    // and `isInBackoff` would wrongly return false. With it, the delta is negative and
    // the target stays in backoff.
    TargetMemory slot{};
    slot.nextEligibleMs = 100; // post-wrap
    const uint32_t now_prewrap = 0xFFFFFF00u;
    TEST_ASSERT_TRUE(isInBackoff(slot, now_prewrap));
}

// ---------- recordAttemptMiss / recordCaptureSuccess ----------

void test_record_miss_increments_attempts_and_sets_eligibility(void) {
    TargetMemory slot{};
    recordAttemptMiss(slot, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, slot.attempts);
    TEST_ASSERT_EQUAL_UINT32(1000, slot.lastAttemptMs);
    TEST_ASSERT_EQUAL_UINT32(1000u + 60u * 1000u, slot.nextEligibleMs);

    recordAttemptMiss(slot, 70000);
    TEST_ASSERT_EQUAL_UINT8(2, slot.attempts);
    TEST_ASSERT_EQUAL_UINT32(70000, slot.lastAttemptMs);
    TEST_ASSERT_EQUAL_UINT32(70000u + 120u * 1000u, slot.nextEligibleMs);
}

void test_capture_clears_backoff_preserves_observed_clients(void) {
    TargetMemory slot{};
    slot.observedClients = 3;
    recordAttemptMiss(slot, 1000);
    recordAttemptMiss(slot, 70000);
    TEST_ASSERT_EQUAL_UINT8(2, slot.attempts);

    recordCaptureSuccess(slot, 200000);
    TEST_ASSERT_EQUAL_UINT8(0, slot.attempts);
    TEST_ASSERT_EQUAL_UINT32(0, slot.nextEligibleMs);
    TEST_ASSERT_EQUAL_UINT32(200000, slot.lastAttemptMs);
    // observedClients kept: the AP still had clients even if the window missed.
    TEST_ASSERT_EQUAL_UINT8(3, slot.observedClients);
    TEST_ASSERT_FALSE(isInBackoff(slot, 200000));
}

// ---------- recordObservedClients ----------

void test_observed_clients_saturates_at_255(void) {
    TargetMemory slot{};
    recordObservedClients(slot, 10);
    TEST_ASSERT_EQUAL_UINT8(10, slot.observedClients);
    recordObservedClients(slot, 999);
    TEST_ASSERT_EQUAL_UINT8(255, slot.observedClients);
}

// ---------- slot helpers ----------

void test_slot_free_detection(void) {
    TargetMemory slot{};
    TEST_ASSERT_TRUE(isSlotFree(slot));
    slot.bssid[2] = 0x42;
    TEST_ASSERT_FALSE(isSlotFree(slot));
}

void test_bssid_equals(void) {
    const uint8_t a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t c[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00};
    TEST_ASSERT_TRUE(bssidEquals(a, b));
    TEST_ASSERT_FALSE(bssidEquals(a, c));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_miss_is_sixty_seconds);
    RUN_TEST(test_doubling_schedule_sixty_to_cap);
    RUN_TEST(test_defensive_zero_attempts_treated_as_one);
    RUN_TEST(test_fresh_slot_is_not_in_backoff);
    RUN_TEST(test_in_backoff_true_before_eligibility);
    RUN_TEST(test_in_backoff_false_at_or_after_eligibility);
    RUN_TEST(test_in_backoff_survives_millis_wrap);
    RUN_TEST(test_record_miss_increments_attempts_and_sets_eligibility);
    RUN_TEST(test_capture_clears_backoff_preserves_observed_clients);
    RUN_TEST(test_observed_clients_saturates_at_255);
    RUN_TEST(test_slot_free_detection);
    RUN_TEST(test_bssid_equals);
    return UNITY_END();
}
