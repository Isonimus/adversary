/**
 * @file test_proximity_tracker.cpp
 * @brief Regression for the pure RSSI hot/cold tracker core (ADR-0128, slice-0129).
 *
 * The device shells only feed live RSSI and draw; the testable behaviour is the decision
 * logic: smoothing damps jitter, the band reflects the smoothed level, the trend reports
 * direction of travel, and the Acquiring/Tracking/Lost/Dropped state machine fires on the
 * configured stale/drop windows. Getting the state machine wrong strands the operator on a
 * dead screen or drops a target that was merely between advertisements; getting the honesty
 * knobs wrong is caught here before it becomes a misleading on-device readout.
 */

#include <unity.h>

#include "core/proximity_tracker.h"

using adversary::kDefaultProximityConfig;
using adversary::ProximityBand;
using adversary::ProximityConfig;
using adversary::ProximityReading;
using adversary::ProximityState;
using adversary::ProximityTracker;
using adversary::ProximityTrend;

void setUp(void) {}
void tearDown(void) {}

// Before any sample arrives the tracker is Acquiring, not pretending to have a reading.
void test_acquiring_before_first_sample(void) {
    ProximityTracker tracker;
    ProximityReading r = tracker.evaluate(1000);
    TEST_ASSERT_EQUAL(ProximityState::Acquiring, r.state);
    TEST_ASSERT_FALSE(tracker.hasSample());
}

// The first sample seeds the needle exactly (no crawl up from zero), so the band is right
// immediately.
void test_first_sample_seeds_needle(void) {
    ProximityTracker tracker;
    tracker.addSample(-50, 1000);
    ProximityReading r = tracker.evaluate(1000);
    TEST_ASSERT_EQUAL(ProximityState::Tracking, r.state);
    TEST_ASSERT_EQUAL_INT(-50, r.smoothedRssi);
    TEST_ASSERT_EQUAL_INT(-50, r.lastRawRssi);
    TEST_ASSERT_EQUAL(ProximityBand::Close, r.band);  // -50 >= closeDbm (-55)
}

// Smoothing damps a single outlier: after a steady run the needle barely moves on one spike,
// so the band does not flicker at rest.
void test_smoothing_damps_outlier(void) {
    ProximityTracker tracker;
    uint32_t t = 0;
    for (int i = 0; i < 20; ++i) tracker.addSample(-70, t += 200);  // settle at -70 (Mid)
    tracker.addSample(-30, t += 200);                               // one strong spike
    ProximityReading r = tracker.evaluate(t);
    // With needleAlpha 0.35 the needle moves ~14 dB on a 40 dB spike, nowhere near Close.
    TEST_ASSERT_EQUAL(ProximityBand::Mid, r.band);
    TEST_ASSERT_TRUE(r.smoothedRssi < kDefaultProximityConfig.closeDbm);
    TEST_ASSERT_EQUAL_INT(-30, r.lastRawRssi);  // raw is still surfaced
}

// Bands map to the smoothed level: far (<-75), mid (between), close (>=-55).
void test_bands_track_level(void) {
    {
        ProximityTracker tracker;
        tracker.addSample(-85, 1000);
        TEST_ASSERT_EQUAL(ProximityBand::Far, tracker.evaluate(1000).band);
    }
    {
        ProximityTracker tracker;
        tracker.addSample(-65, 1000);
        TEST_ASSERT_EQUAL(ProximityBand::Mid, tracker.evaluate(1000).band);
    }
    {
        ProximityTracker tracker;
        tracker.addSample(-40, 1000);
        TEST_ASSERT_EQUAL(ProximityBand::Close, tracker.evaluate(1000).band);
    }
}

// A rising signal (walking closer) reads Approaching; a falling one reads Receding.
void test_trend_follows_direction(void) {
    ProximityTracker approaching;
    uint32_t t = 0;
    for (int i = 0; i < 10; ++i) approaching.addSample(-80, t += 200);  // settle low
    for (int i = 0; i < 10; ++i) approaching.addSample(-50, t += 200);  // climb
    TEST_ASSERT_EQUAL(ProximityTrend::Approaching, approaching.evaluate(t).trend);

    ProximityTracker receding;
    t = 0;
    for (int i = 0; i < 10; ++i) receding.addSample(-50, t += 200);  // settle high
    for (int i = 0; i < 10; ++i) receding.addSample(-80, t += 200);  // fall
    TEST_ASSERT_EQUAL(ProximityTrend::Receding, receding.evaluate(t).trend);
}

// A flat signal reads Steady: needle and baseline converge, drift stays inside the dead-band.
void test_steady_signal_has_no_false_direction(void) {
    ProximityTracker tracker;
    uint32_t t = 0;
    for (int i = 0; i < 40; ++i) tracker.addSample(-65, t += 200);
    TEST_ASSERT_EQUAL(ProximityTrend::Steady, tracker.evaluate(t).trend);
}

// Past the stale window the target is Lost (held, dimmed) but not yet Dropped; past the drop
// window the shell is told to release the lock. The age counter keeps advancing.
void test_lost_then_dropped_on_silence(void) {
    ProximityTracker tracker;
    tracker.addSample(-60, 1000);

    // Fresh.
    TEST_ASSERT_EQUAL(ProximityState::Tracking,
                      tracker.evaluate(1000 + kDefaultProximityConfig.staleMs).state);
    // Just past stale -> Lost.
    ProximityReading lost = tracker.evaluate(1000 + kDefaultProximityConfig.staleMs + 1);
    TEST_ASSERT_EQUAL(ProximityState::Lost, lost.state);
    TEST_ASSERT_EQUAL_UINT32(kDefaultProximityConfig.staleMs + 1, lost.ageMs);
    // Just past drop -> Dropped.
    TEST_ASSERT_EQUAL(ProximityState::Dropped,
                      tracker.evaluate(1000 + kDefaultProximityConfig.dropMs + 1).state);
}

// A fresh sample after a Lost spell recovers to Tracking (the target reappeared).
void test_new_sample_recovers_from_lost(void) {
    ProximityTracker tracker;
    tracker.addSample(-60, 1000);
    TEST_ASSERT_EQUAL(ProximityState::Lost,
                      tracker.evaluate(1000 + kDefaultProximityConfig.staleMs + 1).state);
    tracker.addSample(-60, 10000);
    TEST_ASSERT_EQUAL(ProximityState::Tracking, tracker.evaluate(10000).state);
}

// reset() re-arms for a new lock: back to Acquiring, old samples forgotten.
void test_reset_rearms(void) {
    ProximityTracker tracker;
    tracker.addSample(-60, 1000);
    tracker.reset();
    TEST_ASSERT_FALSE(tracker.hasSample());
    TEST_ASSERT_EQUAL(ProximityState::Acquiring, tracker.evaluate(2000).state);
}

// The meter clamps to 0..100 at and beyond the configured floor/ceil.
void test_meter_clamps_to_range(void) {
    {
        ProximityTracker tracker;
        tracker.addSample(kDefaultProximityConfig.ceilDbm + 20, 1000);  // stronger than ceil
        TEST_ASSERT_EQUAL_UINT8(100, tracker.evaluate(1000).meterPercent);
    }
    {
        ProximityTracker tracker;
        tracker.addSample(kDefaultProximityConfig.floorDbm - 20, 1000);  // weaker than floor
        TEST_ASSERT_EQUAL_UINT8(0, tracker.evaluate(1000).meterPercent);
    }
    {
        ProximityTracker tracker;
        // Midpoint of [-90, -40] is -65 -> 50%.
        tracker.addSample(-65, 1000);
        TEST_ASSERT_EQUAL_UINT8(50, tracker.evaluate(1000).meterPercent);
    }
}

// A caller passing an earlier instant than the last sample must not underflow the age into a
// huge unsigned value that would trip Dropped spuriously.
void test_age_never_underflows(void) {
    ProximityTracker tracker;
    tracker.addSample(-60, 5000);
    ProximityReading r = tracker.evaluate(4000);  // earlier than the sample
    TEST_ASSERT_EQUAL_UINT32(0, r.ageMs);
    TEST_ASSERT_EQUAL(ProximityState::Tracking, r.state);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_acquiring_before_first_sample);
    RUN_TEST(test_first_sample_seeds_needle);
    RUN_TEST(test_smoothing_damps_outlier);
    RUN_TEST(test_bands_track_level);
    RUN_TEST(test_trend_follows_direction);
    RUN_TEST(test_steady_signal_has_no_false_direction);
    RUN_TEST(test_lost_then_dropped_on_silence);
    RUN_TEST(test_new_sample_recovers_from_lost);
    RUN_TEST(test_reset_rearms);
    RUN_TEST(test_meter_clamps_to_range);
    RUN_TEST(test_age_never_underflows);
    return UNITY_END();
}
