/**
 * @file test_sd_mount_policy.cpp
 * @brief Regression for the pure SD remount short-circuit policy (slice-0020).
 *
 * sdShouldAttemptRemount() is the one host-testable seam of the no-card graceful
 * boot. The board has no card-detect pin, so a mount attempt costs the full FSPI
 * ladder (~5.6 s); the policy decides when remount() may skip it. Two properties
 * the fix rests on are pinned here: a cached no-card verdict short-circuits an
 * ordinary remount (so a cardless boot pays the ladder once, not on every
 * shared-bus re-sync), yet an explicit operator retry always re-attempts (so a
 * card inserted after boot is still picked up). Getting this truth table wrong
 * either reintroduces the triple-mount boot cost or strands a real retry.
 */

#include <unity.h>

#include "hal/storage/sd_mount_policy.h"

using adversary::sdShouldAttemptRemount;

void setUp(void) {}
void tearDown(void) {}

// No prior verdict (first boot, or a card was present): always attempt.
void test_attempts_when_no_verdict(void) {
    TEST_ASSERT_TRUE(sdShouldAttemptRemount(/*noCardVerdict=*/false, /*forceRetry=*/false));
    TEST_ASSERT_TRUE(sdShouldAttemptRemount(/*noCardVerdict=*/false, /*forceRetry=*/true));
}

// Cached no-card verdict + ordinary remount (shared-bus re-sync): skip the
// ladder. This is the guard that stops the cardless triple-mount boot cost.
void test_skips_on_cached_no_card_verdict(void) {
    TEST_ASSERT_FALSE(sdShouldAttemptRemount(/*noCardVerdict=*/true, /*forceRetry=*/false));
}

// Cached no-card verdict + operator's explicit retry: attempt anyway, so a card
// inserted after a cardless boot mounts without a reboot.
void test_force_retry_overrides_verdict(void) {
    TEST_ASSERT_TRUE(sdShouldAttemptRemount(/*noCardVerdict=*/true, /*forceRetry=*/true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_attempts_when_no_verdict);
    RUN_TEST(test_skips_on_cached_no_card_verdict);
    RUN_TEST(test_force_retry_overrides_verdict);
    return UNITY_END();
}
