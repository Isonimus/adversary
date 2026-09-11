/**
 * @file test_cc1101_rssi.cpp
 * @brief Regression for cc1101RssiDbm() (slice-0019).
 *
 * The raw-RSSI->dBm conversion is the host-testable half of the band sweep; the
 * register read crosses the HAL boundary and is verified on-device. A wrong sign
 * or offset here silently mis-ranks the bands, so these vectors pin the
 * two's-complement split (>=128 is weak/negative, <128 is strong/positive) and
 * the half-dB scaling against the CC1101 §17.3 formula with offset 74.
 */

#include <unity.h>
#include "hal/expansion/cc1101.h"

using adversary::hal::cc1101RssiDbm;

void setUp(void) {}
void tearDown(void) {}

// raw 0 -> 0/2 - 74.
void test_rssi_zero(void) {
    TEST_ASSERT_EQUAL_INT16(-74, cc1101RssiDbm(0));
}

// raw 127 (0x7F) is the strongest positive code: 63 - 74 = -11 dBm.
void test_rssi_strongest_positive(void) {
    TEST_ASSERT_EQUAL_INT16(-11, cc1101RssiDbm(127));
}

// raw 128 (0x80) wraps to the most-negative code: (128-256)/2 - 74 = -138 dBm.
void test_rssi_most_negative_code(void) {
    TEST_ASSERT_EQUAL_INT16(-138, cc1101RssiDbm(128));
}

// A mid-range positive code: 80/2 - 74 = -34 dBm.
void test_rssi_midrange(void) {
    TEST_ASSERT_EQUAL_INT16(-34, cc1101RssiDbm(80));
}

// The two's-complement split: 128 must read weaker than 127, not stronger — this
// is the assertion that fails if the >=128 branch is dropped.
void test_rssi_sign_split_orders_bands(void) {
    TEST_ASSERT_TRUE(cc1101RssiDbm(127) > cc1101RssiDbm(128));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rssi_zero);
    RUN_TEST(test_rssi_strongest_positive);
    RUN_TEST(test_rssi_most_negative_code);
    RUN_TEST(test_rssi_midrange);
    RUN_TEST(test_rssi_sign_split_orders_bands);
    return UNITY_END();
}
