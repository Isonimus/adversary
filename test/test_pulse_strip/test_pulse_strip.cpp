/**
 * @file test_pulse_strip.cpp
 * @brief Regression for the pure pulse-strip downsampler (slice-0005).
 *
 * pulseStripColumns() maps an alternating on/off duration train onto a fixed
 * pixel width as a per-column high-duty value, so a captured signal can be
 * eyeballed for structure before it is trusted. These tests pin the honest
 * behaviours the preview relies on: proportional gap width, duty (not
 * point-sample) fill for dense bursts, level offset by the first-edge polarity,
 * graceful handling of degenerate trains, and never writing past `width`.
 */

#include <unity.h>
#include <cstdint>
#include <vector>
#include "ui/components/pulse_strip.h"

using adversary::ui::pulseStripColumns;
using adversary::ui::PULSE_STRIP_FULL;

void setUp(void) {}
void tearDown(void) {}

// --- Degenerate inputs fail loud (false), never crash -------------------------

void test_rejects_null_durations(void) {
    uint8_t cols[8] = {0};
    TEST_ASSERT_FALSE(pulseStripColumns(nullptr, 4, true, cols, 8));
}

void test_rejects_zero_count(void) {
    uint16_t d[1] = {100};
    uint8_t cols[8] = {0};
    TEST_ASSERT_FALSE(pulseStripColumns(d, 0, true, cols, 8));
}

void test_rejects_zero_width(void) {
    uint16_t d[2] = {100, 100};
    uint8_t cols[1] = {0};
    TEST_ASSERT_FALSE(pulseStripColumns(d, 2, true, cols, 0));
}

void test_rejects_all_zero_train(void) {
    // A train that sums to zero carries no structure to draw.
    uint16_t d[3] = {0, 0, 0};
    uint8_t cols[4] = {0};
    TEST_ASSERT_FALSE(pulseStripColumns(d, 3, true, cols, 4));
}

// --- Single edge is a defined, uniform strip ----------------------------------

void test_single_high_edge_fills_strip(void) {
    uint16_t d[1] = {500};
    uint8_t cols[6] = {0};
    TEST_ASSERT_TRUE(pulseStripColumns(d, 1, true, cols, 6));
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_UINT8(PULSE_STRIP_FULL, cols[i]);
}

void test_single_low_edge_empty_strip(void) {
    uint16_t d[1] = {500};
    uint8_t cols[6] = {0xFF};
    TEST_ASSERT_TRUE(pulseStripColumns(d, 1, false, cols, 6));
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_UINT8(0, cols[i]);
}

// --- Level tracks the first-edge polarity -------------------------------------

void test_first_level_low_offsets_levels(void) {
    // firstLevelHigh=false: low, high, low. Width 3, one segment per column.
    uint16_t d[3] = {100, 100, 100};
    uint8_t cols[3] = {0};
    TEST_ASSERT_TRUE(pulseStripColumns(d, 3, false, cols, 3));
    TEST_ASSERT_EQUAL_UINT8(0, cols[0]);
    TEST_ASSERT_EQUAL_UINT8(PULSE_STRIP_FULL, cols[1]);
    TEST_ASSERT_EQUAL_UINT8(0, cols[2]);
}

// --- A long gap stays proportionally wide -------------------------------------

void test_long_gap_stays_wide(void) {
    // 100us mark then 900us space over 10 columns of 100us each: exactly one
    // column of mark, nine of gap — the gap keeps its 9:1 share of the width.
    uint16_t d[2] = {100, 900};
    uint8_t cols[10] = {0};
    TEST_ASSERT_TRUE(pulseStripColumns(d, 2, true, cols, 10));
    TEST_ASSERT_EQUAL_UINT8(PULSE_STRIP_FULL, cols[0]);
    int zeros = 0;
    for (int i = 1; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, cols[i]);
        ++zeros;
    }
    TEST_ASSERT_EQUAL_INT(9, zeros);
}

// --- A dense burst reads as partial fill, not an aliased on/off ----------------

void test_dense_burst_is_partial_fill(void) {
    // Four equal mark/space edges collapsed into a single column: 20us of the
    // 40us window is high -> half fill. A point-sample would have lied 0 or 255.
    uint16_t d[4] = {10, 10, 10, 10};
    uint8_t cols[1] = {0};
    TEST_ASSERT_TRUE(pulseStripColumns(d, 4, true, cols, 1));
    // 20/40 * 255 = 127 (integer). Allow the exact integer result.
    TEST_ASSERT_EQUAL_UINT8(127, cols[0]);
}

// --- Downsampling many edges never writes past width --------------------------

void test_never_indexes_out_of_width(void) {
    std::vector<uint16_t> d(900, 40);  // 900 edges, well past the target width
    const int width = 230;
    std::vector<uint8_t> buf(width + 4, 0xAB);  // 4 sentinel guard bytes
    TEST_ASSERT_TRUE(pulseStripColumns(d.data(), d.size(), true, buf.data(), width));
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xAB, buf[width + i]);  // guards intact
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_null_durations);
    RUN_TEST(test_rejects_zero_count);
    RUN_TEST(test_rejects_zero_width);
    RUN_TEST(test_rejects_all_zero_train);
    RUN_TEST(test_single_high_edge_fills_strip);
    RUN_TEST(test_single_low_edge_empty_strip);
    RUN_TEST(test_first_level_low_offsets_levels);
    RUN_TEST(test_long_gap_stays_wide);
    RUN_TEST(test_dense_burst_is_partial_fill);
    RUN_TEST(test_never_indexes_out_of_width);
    return UNITY_END();
}
