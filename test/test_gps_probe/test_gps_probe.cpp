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
#include "modules/gps/gps_manager.h"

using adversary::gps::gpsProbePhase;
using adversary::gps::ProbePhase;
using adversary::gps::fixTransition;
using adversary::gps::FixTransition;
using adversary::gps::gpsBlocksRfid;

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

// --- Fix-transition seam: only the edges emit an event ------------------------
//
// GPSManager::update() emits GPS_FIX_ACQUIRED/LOST off this pure decision. The
// invariant that keeps the bus quiet is that an unchanged state yields None, so
// a steady fix (or steady no-fix) never re-fires per NMEA sentence.

void test_no_transition_when_still_no_fix(void) {
    TEST_ASSERT_EQUAL(FixTransition::None, fixTransition(false, false));
}

void test_no_transition_when_fix_held(void) {
    TEST_ASSERT_EQUAL(FixTransition::None, fixTransition(true, true));
}

void test_acquired_on_rising_edge(void) {
    TEST_ASSERT_EQUAL(FixTransition::Acquired, fixTransition(false, true));
}

void test_lost_on_falling_edge(void) {
    TEST_ASSERT_EQUAL(FixTransition::Lost, fixTransition(true, false));
}

// --- RFID coexistence: only a Grove-port GPS blocks the reader ----------------
//
// RFID (MFRC522 I2C) and a Grove GPS both need G1/G2, so they cannot coexist and
// RFID is skipped. A cap GPS on G13/G15 shares nothing with the reader, so it
// must leave RFID enabled — this gate is what keeps RFID available while
// wardriving off the GNSS/LoRa cap. The literal source strings mirror exactly
// what GPSManager::getDetectedPinSet() reports at runtime.

void test_grove_gps_blocks_rfid(void) {
    TEST_ASSERT_TRUE(gpsBlocksRfid(true, "Grove"));
}

void test_cap_gps_does_not_block_rfid(void) {
    // The fix: a cap GPS shares no pins with the reader, so RFID stays enabled.
    // Under the old "skip on any GPS" behaviour this would (wrongly) be true.
    TEST_ASSERT_FALSE(gpsBlocksRfid(true, "Cap"));
}

void test_no_gps_does_not_block_rfid(void) {
    TEST_ASSERT_FALSE(gpsBlocksRfid(false, nullptr));
}

void test_unknown_source_blocks_rfid_failsafe(void) {
    // Fail-safe: a GPS detected with no reported source (shouldn't happen) must
    // skip RFID rather than risk stomping a possibly-active Grove GPS on G1/G2.
    TEST_ASSERT_TRUE(gpsBlocksRfid(true, nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_byte_at_start_is_present);
    RUN_TEST(test_byte_midwindow_is_present_not_waiting);
    RUN_TEST(test_byte_after_window_is_still_present);
    RUN_TEST(test_silent_before_window_keeps_waiting);
    RUN_TEST(test_silent_at_window_is_absent);
    RUN_TEST(test_silent_past_window_is_absent);
    RUN_TEST(test_no_transition_when_still_no_fix);
    RUN_TEST(test_no_transition_when_fix_held);
    RUN_TEST(test_acquired_on_rising_edge);
    RUN_TEST(test_lost_on_falling_edge);
    RUN_TEST(test_grove_gps_blocks_rfid);
    RUN_TEST(test_cap_gps_does_not_block_rfid);
    RUN_TEST(test_no_gps_does_not_block_rfid);
    RUN_TEST(test_unknown_source_blocks_rfid_failsafe);
    return UNITY_END();
}
