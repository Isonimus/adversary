/**
 * @file test_band_sweep.cpp
 * @brief Regression for the CC1101 band-sweep peak-hold model (slice-0019).
 *
 * The sweep samples one band per frame, so the model must (a) keep the strongest
 * reading a band ever showed (peak-hold, not last-value) and (b) name the
 * strongest band for the highlight. These tests pin both against the weaker-reading
 * and empty-sweep edges.
 */

#include <unity.h>
#include "modules/rf/band_sweep.h"

using adversary::rf::BandSweep;
using adversary::rf::bandSweepStrongest;
using adversary::rf::BAND_RSSI_NONE;
using adversary::rf::BAND_SWEEP_COUNT;

void setUp(void) {}
void tearDown(void) {}

static BandSweep freshSweep() {
    BandSweep s;
    s.reset();
    return s;
}

// A reset sweep has no sampled band, so there is no strongest yet.
void test_empty_sweep_has_no_strongest(void) {
    BandSweep s = freshSweep();
    TEST_ASSERT_EQUAL_INT(-1, bandSweepStrongest(s));
    for (size_t b = 0; b < BAND_SWEEP_COUNT; ++b) {
        TEST_ASSERT_EQUAL_INT16(BAND_RSSI_NONE, s.peakDbm[b]);
    }
}

// The first reading of any band always takes, beating the NONE floor.
void test_first_reading_sets_peak(void) {
    BandSweep s = freshSweep();
    s.observe(2, -90);
    TEST_ASSERT_EQUAL_INT16(-90, s.peakDbm[2]);
    TEST_ASSERT_EQUAL_INT(2, bandSweepStrongest(s));
}

// Peak-hold: a stronger later reading raises the peak; a weaker one does not lower
// it (the defining behaviour — last-value would fail the second assertion).
void test_peak_hold_keeps_strongest(void) {
    BandSweep s = freshSweep();
    s.observe(1, -80);
    s.observe(1, -55);  // stronger: raises
    TEST_ASSERT_EQUAL_INT16(-55, s.peakDbm[1]);
    s.observe(1, -70);  // weaker: must not lower
    TEST_ASSERT_EQUAL_INT16(-55, s.peakDbm[1]);
}

// Strongest picks the highest dBm across sampled bands.
void test_strongest_across_bands(void) {
    BandSweep s = freshSweep();
    s.observe(0, -95);
    s.observe(1, -60);
    s.observe(3, -72);
    TEST_ASSERT_EQUAL_INT(1, bandSweepStrongest(s));
}

// Ties resolve to the lowest index for a deterministic highlight.
void test_ties_resolve_to_lowest_index(void) {
    BandSweep s = freshSweep();
    s.observe(3, -65);
    s.observe(1, -65);
    TEST_ASSERT_EQUAL_INT(1, bandSweepStrongest(s));
}

// Out-of-range observe is ignored, not a crash or a stray peak.
void test_out_of_range_observe_ignored(void) {
    BandSweep s = freshSweep();
    s.observe(BAND_SWEEP_COUNT, -10);
    TEST_ASSERT_EQUAL_INT(-1, bandSweepStrongest(s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_sweep_has_no_strongest);
    RUN_TEST(test_first_reading_sets_peak);
    RUN_TEST(test_peak_hold_keeps_strongest);
    RUN_TEST(test_strongest_across_bands);
    RUN_TEST(test_ties_resolve_to_lowest_index);
    RUN_TEST(test_out_of_range_observe_ignored);
    return UNITY_END();
}
