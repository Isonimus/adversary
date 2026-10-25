/**
 * @file test_version_marker.cpp
 * @brief Unit tests for the what's-new version comparison (ADR-0121, slice-0122).
 *
 * Covers versionDiffers(), the one branch behind the first-boot-after-update auto-show. The
 * NVS read/write is device-only (stubbed natively, see version_marker.cpp) and is verified
 * on-device, matching the slice-0119 split: the bug-prone logic is the pure helper.
 */

#include <unity.h>
#include "core/version_marker.h"

using namespace adversary;

void setUp() {}
void tearDown() {}

/** Same stored and running version: not new, so the screen does not auto-show. */
void test_same_version_is_not_new(void) {
    TEST_ASSERT_FALSE(versionDiffers("0.6.0-alpha", "0.6.0-alpha"));
}

/** A bumped version: new, so the screen auto-shows once. */
void test_different_version_is_new(void) {
    TEST_ASSERT_TRUE(versionDiffers("0.5.0-alpha", "0.6.0-alpha"));
}

/** First-ever boot has no stored version (empty default): must read as new. */
void test_empty_seen_is_new(void) {
    TEST_ASSERT_TRUE(versionDiffers("", "0.6.0-alpha"));
}

/** A stored value that is a prefix of the running one still differs (no false match). */
void test_prefix_is_new(void) {
    TEST_ASSERT_TRUE(versionDiffers("0.6.0", "0.6.0-alpha"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_same_version_is_not_new);
    RUN_TEST(test_different_version_is_new);
    RUN_TEST(test_empty_seen_is_new);
    RUN_TEST(test_prefix_is_new);
    return UNITY_END();
}
