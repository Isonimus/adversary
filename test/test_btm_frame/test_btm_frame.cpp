/**
 * @file test_btm_frame.cpp
 * @brief Byte-level unit tests for the pure BTM Request builder and the
 *        autohunt one-shot injection predicate (slice-0155).
 *
 * The builder is the only thing standing between "we know what we're
 * transmitting" and "we hope we're transmitting the right thing." These
 * tests pin every header byte, the WNM action triple, the request mode
 * bit mask, the little-endian disassoc timer, and the tag-52 Neighbor
 * Report body so any future regression on the frame layout fails loud
 * before it ships.
 *
 * The predicate tests cover the DoD cells: BTM injects only when
 * steer-on + PMF-required + arms-solicitation + half-window-elapsed +
 * not-yet-injected. Every other combination must be false.
 */

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "utils/btm_frame.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// ---------- Fixtures ----------

namespace {

constexpr uint8_t kTargetBssid[6]    = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
constexpr uint8_t kCandidateBssid[6] = {0x02, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};  // LAA bit of target.
constexpr uint8_t kBroadcast[6]      = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

BtmRequestParams makeParams() {
    BtmRequestParams p{};
    p.targetBssid       = kTargetBssid;
    p.destMac           = kBroadcast;
    p.candidateBssid    = kCandidateBssid;
    p.dialogToken       = 0x42;
    p.requestMode       = static_cast<uint8_t>(BtmRequestModeBit::PreferredCandidateListIncluded)
                        | static_cast<uint8_t>(BtmRequestModeBit::DisassociationImminent);
    p.disassocTimerTu   = 0x1234;   // 0x34 low byte, 0x12 high byte in LE.
    p.validityIntervalBi = 20;
    p.candidateOpClass  = 81;
    p.candidateChannel  = 6;
    p.candidatePhyType  = 7;
    return p;
}

}  // namespace

// ---------- Builder ----------

void test_builder_returns_expected_length(void) {
    uint8_t frame[64] = {0};
    const auto params = makeParams();
    const size_t written = buildBtmRequestFrame(frame, sizeof(frame), params);
    TEST_ASSERT_EQUAL_size_t(kBtmRequestFrameSize, written);
    TEST_ASSERT_EQUAL_size_t(46U, written);
}

void test_builder_rejects_undersized_buffer(void) {
    uint8_t frame[32] = {0};
    const auto params = makeParams();
    TEST_ASSERT_EQUAL_size_t(0U, buildBtmRequestFrame(frame, sizeof(frame), params));
}

void test_builder_rejects_null_pointers(void) {
    uint8_t frame[64] = {0};
    auto params = makeParams();

    TEST_ASSERT_EQUAL_size_t(0U, buildBtmRequestFrame(nullptr, sizeof(frame), params));

    auto p1 = params; p1.targetBssid = nullptr;
    TEST_ASSERT_EQUAL_size_t(0U, buildBtmRequestFrame(frame, sizeof(frame), p1));

    auto p2 = params; p2.destMac = nullptr;
    TEST_ASSERT_EQUAL_size_t(0U, buildBtmRequestFrame(frame, sizeof(frame), p2));

    auto p3 = params; p3.candidateBssid = nullptr;
    TEST_ASSERT_EQUAL_size_t(0U, buildBtmRequestFrame(frame, sizeof(frame), p3));
}

void test_mac_header_bytes(void) {
    uint8_t frame[64] = {0};
    const auto params = makeParams();
    const size_t written = buildBtmRequestFrame(frame, sizeof(frame), params);
    TEST_ASSERT_EQUAL_size_t(kBtmRequestFrameSize, written);

    // Frame Control: Mgmt type (00) + Action subtype (1101) -> 0xD0, 0x00.
    TEST_ASSERT_EQUAL_UINT8(0xD0, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[1]);
    // Duration must be zero on an injected action frame.
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[3]);
    // Address 1: destination = broadcast.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kBroadcast, &frame[4], 6);
    // Address 2: source = target BSSID (we impersonate it).
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kTargetBssid, &frame[10], 6);
    // Address 3: BSSID = target BSSID.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kTargetBssid, &frame[16], 6);
    // Sequence Control: let the radio stamp; we transmit zeroes.
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[22]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[23]);
}

void test_wnm_action_body_bytes(void) {
    uint8_t frame[64] = {0};
    const auto params = makeParams();
    buildBtmRequestFrame(frame, sizeof(frame), params);

    TEST_ASSERT_EQUAL_UINT8(10, frame[24]);         // Category = WNM.
    TEST_ASSERT_EQUAL_UINT8(7,  frame[25]);         // Action   = BSS Transition Management Request.
    TEST_ASSERT_EQUAL_UINT8(0x42, frame[26]);       // Dialog Token echoed.

    // Request Mode: bit 0 and bit 2 set, bits 1/3/4 clear.
    const uint8_t expectedMode =
        static_cast<uint8_t>(BtmRequestModeBit::PreferredCandidateListIncluded)
      | static_cast<uint8_t>(BtmRequestModeBit::DisassociationImminent);
    TEST_ASSERT_EQUAL_UINT8(expectedMode, frame[27]);
    TEST_ASSERT_EQUAL_UINT8(0x01 | 0x04, frame[27]);   // Explicit numeric sanity.

    // Disassoc Timer: 0x1234 little-endian.
    TEST_ASSERT_EQUAL_UINT8(0x34, frame[28]);
    TEST_ASSERT_EQUAL_UINT8(0x12, frame[29]);

    // Validity Interval (in beacon intervals).
    TEST_ASSERT_EQUAL_UINT8(20, frame[30]);
}

void test_neighbor_report_ie_bytes(void) {
    uint8_t frame[64] = {0};
    const auto params = makeParams();
    buildBtmRequestFrame(frame, sizeof(frame), params);

    TEST_ASSERT_EQUAL_UINT8(52, frame[31]);                 // Tag ID: Neighbor Report Element.
    TEST_ASSERT_EQUAL_UINT8(13, frame[32]);                 // Length of the body that follows.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kCandidateBssid, &frame[33], 6);
    // BSSID Info left zero (no reachability/security hints at this point).
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[39]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[40]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[41]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[42]);
    TEST_ASSERT_EQUAL_UINT8(81, frame[43]);                 // Operating Class.
    TEST_ASSERT_EQUAL_UINT8(6,  frame[44]);                 // Channel.
    TEST_ASSERT_EQUAL_UINT8(7,  frame[45]);                 // PHY Type.
}

void test_builder_zero_timer_and_cleared_mode(void) {
    // Negative controls: an unset timer and request mode must serialize as zeroes.
    uint8_t frame[64] = {0};
    auto params = makeParams();
    params.disassocTimerTu = 0;
    params.requestMode = 0;
    buildBtmRequestFrame(frame, sizeof(frame), params);

    TEST_ASSERT_EQUAL_UINT8(0, frame[27]);
    TEST_ASSERT_EQUAL_UINT8(0, frame[28]);
    TEST_ASSERT_EQUAL_UINT8(0, frame[29]);
}

// ---------- Predicate ----------

namespace {

// The exact DoD cell: steer on, PMF-required target, solicitation armed,
// half-window elapsed, not-yet-injected.
BtmInjectInputs cannedPositive() {
    BtmInjectInputs in{};
    in.pmfRequired       = true;
    in.steerEnabled      = true;
    in.alreadyInjected   = false;
    in.halfWindowElapsed = true;
    in.armsSolicitation  = true;
    return in;
}

}  // namespace

void test_predicate_fires_on_the_exact_dod_cell(void) {
    TEST_ASSERT_TRUE(shouldInjectBtm(cannedPositive()));
}

void test_predicate_default_off_wins_over_everything(void) {
    auto in = cannedPositive();
    in.steerEnabled = false;
    TEST_ASSERT_FALSE(shouldInjectBtm(in));
}

void test_predicate_skips_non_pmf_targets(void) {
    auto in = cannedPositive();
    in.pmfRequired = false;
    TEST_ASSERT_FALSE(shouldInjectBtm(in));
}

void test_predicate_skips_if_already_injected(void) {
    auto in = cannedPositive();
    in.alreadyInjected = true;
    TEST_ASSERT_FALSE(shouldInjectBtm(in));
}

void test_predicate_waits_for_half_window(void) {
    auto in = cannedPositive();
    in.halfWindowElapsed = false;
    TEST_ASSERT_FALSE(shouldInjectBtm(in));
}

void test_predicate_requires_solicitation_arm(void) {
    // Under Deauth-only or Skipped the PMKID path is not armed, so a BTM
    // steer has no capture it could hand off to, so we do not inject it.
    auto in = cannedPositive();
    in.armsSolicitation = false;
    TEST_ASSERT_FALSE(shouldInjectBtm(in));
}

// ---------- Main ----------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_builder_returns_expected_length);
    RUN_TEST(test_builder_rejects_undersized_buffer);
    RUN_TEST(test_builder_rejects_null_pointers);
    RUN_TEST(test_mac_header_bytes);
    RUN_TEST(test_wnm_action_body_bytes);
    RUN_TEST(test_neighbor_report_ie_bytes);
    RUN_TEST(test_builder_zero_timer_and_cleared_mode);
    RUN_TEST(test_predicate_fires_on_the_exact_dod_cell);
    RUN_TEST(test_predicate_default_off_wins_over_everything);
    RUN_TEST(test_predicate_skips_non_pmf_targets);
    RUN_TEST(test_predicate_skips_if_already_injected);
    RUN_TEST(test_predicate_waits_for_half_window);
    RUN_TEST(test_predicate_requires_solicitation_arm);
    return UNITY_END();
}
