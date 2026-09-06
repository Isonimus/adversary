/**
 * @file test_cc1101_freq.cpp
 * @brief Regression for cc1101FreqRegs() (slice-0003).
 *
 * The carrier->register math is the one host-testable piece of the CC1101 driver;
 * the register-driving I/O crosses the HAL boundary and is verified on-device.
 * The 433.92 MHz vector is the value confirmed on real hardware by the slice-0003
 * spike ({0x10,0xB0,0x71}); the others pin the formula against a rounding change.
 */

#include <unity.h>
#include "hal/expansion/cc1101.h"

using adversary::hal::Cc1101FreqRegs;
using adversary::hal::cc1101FreqRegs;

void setUp(void) {}
void tearDown(void) {}

static void assertRegs(double mhz, uint8_t f2, uint8_t f1, uint8_t f0) {
    Cc1101FreqRegs r = cc1101FreqRegs(mhz);
    TEST_ASSERT_EQUAL_HEX8(f2, r.freq2);
    TEST_ASSERT_EQUAL_HEX8(f1, r.freq1);
    TEST_ASSERT_EQUAL_HEX8(f0, r.freq0);
}

// 433.92 MHz -> 0x10B071, confirmed on-device (spike). The anchor vector.
void test_freq_43392(void) { assertRegs(433.92, 0x10, 0xB0, 0x71); }

void test_freq_315(void) { assertRegs(315.0, 0x0C, 0x1D, 0x8A); }

void test_freq_86835(void) { assertRegs(868.35, 0x21, 0x65, 0xE8); }

void test_freq_915(void) { assertRegs(915.0, 0x23, 0x31, 0x3B); }

// Rounding is to nearest, not truncation: 433.9002 MHz lands just past a half-LSB
// (FREQ0 0x40 rounded vs 0x3F truncated), so dropping the +0.5 fails here.
void test_freq_rounds_to_nearest(void) { assertRegs(433.9002, 0x10, 0xB0, 0x40); }

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_freq_43392);
    RUN_TEST(test_freq_315);
    RUN_TEST(test_freq_86835);
    RUN_TEST(test_freq_915);
    RUN_TEST(test_freq_rounds_to_nearest);
    return UNITY_END();
}
