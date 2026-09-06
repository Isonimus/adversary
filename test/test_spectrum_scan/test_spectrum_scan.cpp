/**
 * @file test_spectrum_scan.cpp
 * @brief Regression for the pure spectrum-occupancy downsampler (slice-0006).
 *
 * spectrumColumns() maps a per-channel occupancy tally onto a fixed bar width for
 * the NRF24 analyzer. These tests pin the honest behaviours the live display
 * relies on: normalisation against the sweep count, MAX (not mean) bucketing so a
 * single busy channel still lights its bar, fail-loud on degenerate input, and
 * never writing past `width`.
 */

#include <unity.h>

#include <cstring>

#include "modules/rf/spectrum_scan.h"

using adversary::rf::SpectrumScan;
using adversary::rf::spectrumColumns;
using adversary::rf::SPECTRUM_CHANNELS;
using adversary::rf::SPECTRUM_FULL;

void setUp(void) {}
void tearDown(void) {}

// --- Degenerate inputs fail loud (false), never write --------------------------

void test_rejects_null_out(void) {
    SpectrumScan scan;
    scan.reset();
    scan.sweeps = 1;
    TEST_ASSERT_FALSE(spectrumColumns(scan, nullptr, 8));
}

void test_rejects_zero_width(void) {
    SpectrumScan scan;
    scan.reset();
    scan.sweeps = 1;
    uint8_t cols[1] = {0};
    TEST_ASSERT_FALSE(spectrumColumns(scan, cols, 0));
}

void test_rejects_no_sweeps_yet(void) {
    // No sweep has run, so there is no denominator and nothing to draw.
    SpectrumScan scan;
    scan.reset();
    uint8_t cols[4] = {0xAB, 0xAB, 0xAB, 0xAB};
    TEST_ASSERT_FALSE(spectrumColumns(scan, cols, 4));
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT8(0xAB, cols[i]);  // untouched
}

// --- Normalisation against the sweep count ------------------------------------

void test_full_and_half_occupancy_normalise(void) {
    // One column per channel: a channel hit every sweep is full-scale; a channel
    // hit half the sweeps is ~half. A quiet channel stays zero.
    SpectrumScan scan;
    scan.reset();
    scan.sweeps = 10;
    scan.hits[3] = 10;  // occupied every sweep
    scan.hits[4] = 5;   // occupied half the sweeps

    uint8_t cols[SPECTRUM_CHANNELS] = {0};
    TEST_ASSERT_TRUE(spectrumColumns(scan, cols, SPECTRUM_CHANNELS));
    TEST_ASSERT_EQUAL_UINT8(SPECTRUM_FULL, cols[3]);
    TEST_ASSERT_EQUAL_UINT8(127, cols[4]);  // 5*255/10 = 127 (integer)
    TEST_ASSERT_EQUAL_UINT8(0, cols[0]);
}

// --- MAX bucketing: one busy channel lights the whole bucket -------------------

void test_bucket_takes_max_not_mean(void) {
    // 126 channels folded onto 63 columns => two channels per column. A single
    // busy channel beside a quiet one must drive the column full, not half.
    SpectrumScan scan;
    scan.reset();
    scan.sweeps = 4;
    scan.hits[0] = 4;  // channel 0 busy, channel 1 quiet -> column 0
    // channel 1 left at 0

    uint8_t cols[63] = {0};
    TEST_ASSERT_TRUE(spectrumColumns(scan, cols, 63));
    TEST_ASSERT_EQUAL_UINT8(SPECTRUM_FULL, cols[0]);  // max(4,0) normalised = full
}

// --- Never writes past width --------------------------------------------------

void test_never_writes_past_width(void) {
    SpectrumScan scan;
    scan.reset();
    scan.sweeps = 2;
    for (size_t i = 0; i < SPECTRUM_CHANNELS; ++i) scan.hits[i] = 2;

    const size_t width = 200;
    uint8_t buf[width + 4];
    std::memset(buf, 0xCD, sizeof(buf));  // guard bytes past width
    TEST_ASSERT_TRUE(spectrumColumns(scan, buf, width));
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT8(0xCD, buf[width + i]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_null_out);
    RUN_TEST(test_rejects_zero_width);
    RUN_TEST(test_rejects_no_sweeps_yet);
    RUN_TEST(test_full_and_half_occupancy_normalise);
    RUN_TEST(test_bucket_takes_max_not_mean);
    RUN_TEST(test_never_writes_past_width);
    return UNITY_END();
}
