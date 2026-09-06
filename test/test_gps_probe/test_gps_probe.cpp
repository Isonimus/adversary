/**
 * @file test_gps_probe.cpp
 * @brief Regression for the pure GPS carrier-presence policy (slice-0007).
 *
 * gpsProbePhase() is the one host-testable seam of the boot pre-check. Two
 * properties the whole fix rests on are pinned here: a single byte flips to
 * Present immediately (the speed win — a live GPS is never made to wait out the
 * window), and Absent is never declared before the window fully elapses (the
 * no-false-negative guard — a GPS between its 1 Hz bursts must not be handed off
 * as "nothing attached"). The window boundary is exercised on both sides.
 */

#include <unity.h>

#include "modules/gps/gps_probe.h"

using adversary::gps::gpsProbePhase;
using adversary::gps::ProbePhase;

namespace {
constexpr uint32_t WINDOW_MS = 1200;  // mirrors GPS_PRESENCE_WINDOW_MS
}

void setUp(void) {}
void tearDown(void) {}

// --- A byte means present, immediately, at any point in the window -------------

void test_byte_at_start_is_present(void) {
    TEST_ASSERT_EQUAL(ProbePhase::Present, gpsProbePhase(true, 0, WINDOW_MS));
}

void test_byte_midwindow_is_present_not_waiting(void) {
    // A live GPS seen partway through must break out now, not wait out the window.
    TEST_ASSERT_EQUAL(ProbePhase::Present, gpsProbePhase(true, WINDOW_MS / 2, WINDOW_MS));
}

void test_byte_after_window_is_still_present(void) {
    // Presence dominates elapsed time — a byte is a device regardless of when.
    TEST_ASSERT_EQUAL(ProbePhase::Present, gpsProbePhase(true, WINDOW_MS + 500, WINDOW_MS));
}

// --- Silence keeps waiting until the window is fully spent ---------------------

void test_silent_before_window_keeps_waiting(void) {
    // The crux of the no-false-negative guard: silence short of the window must
    // NOT declare Absent — a bursty GPS may simply not have spoken yet.
    TEST_ASSERT_EQUAL(ProbePhase::Waiting, gpsProbePhase(false, 0, WINDOW_MS));
    TEST_ASSERT_EQUAL(ProbePhase::Waiting, gpsProbePhase(false, WINDOW_MS - 1, WINDOW_MS));
}

// --- Absent only at/after the window boundary ---------------------------------

void test_silent_at_window_is_absent(void) {
    // Boundary: an off-by-one here would either never terminate or cut a live GPS
    // short. Absence begins exactly when elapsed reaches the window.
    TEST_ASSERT_EQUAL(ProbePhase::Absent, gpsProbePhase(false, WINDOW_MS, WINDOW_MS));
}

void test_silent_past_window_is_absent(void) {
    TEST_ASSERT_EQUAL(ProbePhase::Absent, gpsProbePhase(false, WINDOW_MS + 1000, WINDOW_MS));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_byte_at_start_is_present);
    RUN_TEST(test_byte_midwindow_is_present_not_waiting);
    RUN_TEST(test_byte_after_window_is_still_present);
    RUN_TEST(test_silent_before_window_keeps_waiting);
    RUN_TEST(test_silent_at_window_is_absent);
    RUN_TEST(test_silent_past_window_is_absent);
    return UNITY_END();
}
