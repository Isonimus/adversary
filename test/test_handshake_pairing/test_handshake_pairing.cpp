/**
 * @file test_handshake_pairing.cpp
 * @brief Regression for the 4-way completeness invariant (ADR-0145).
 *
 * A capture is complete only when its ANonce message (M1/M3) and MIC message
 * (M2) share a client MAC and corresponding replay counters, i.e. belong to one
 * exchange. Before ADR-0145, isMinimumValid() checked only that the messages
 * were present, so M1 from one station (e.g. a clientless-PMKID solicitation)
 * paired with an unrelated client's M2 counted as valid and produced a handshake
 * WPA-SEC rejects. These assert the pairing checks; the mismatch cases fail
 * against the old presence-only predicate and pass after.
 */

#include <cstring>

#include <unity.h>

#include "modules/capture/handshake_capture.h"

using adversary::CapturedHandshake;

void setUp(void) {}
void tearDown(void) {}

namespace {

const uint8_t kStaA[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
const uint8_t kStaB[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

// A capture carrying M1 and M2 with the given stations and replay counters.
CapturedHandshake withM1M2(const uint8_t* m1Client, uint64_t m1Replay,
                           const uint8_t* m2Client, uint64_t m2Replay) {
    CapturedHandshake hs{};
    hs.hasMsg1 = true;
    hs.hasMsg2 = true;
    memcpy(hs.msg1Client, m1Client, 6);
    memcpy(hs.msg2Client, m2Client, 6);
    hs.msg1Replay = m1Replay;
    hs.msg2Replay = m2Replay;
    return hs;
}

}  // namespace

void test_m1m2_same_client_same_replay_is_valid(void) {
    CapturedHandshake hs = withM1M2(kStaA, 5, kStaA, 5);
    TEST_ASSERT_TRUE(hs.hasM1M2Pair());
    TEST_ASSERT_TRUE(hs.isMinimumValid());
}

void test_m1m2_different_client_is_invalid(void) {
    // The Frankenstein case: M1 solicited to one STA, M2 from an unrelated client.
    CapturedHandshake hs = withM1M2(kStaA, 5, kStaB, 5);
    TEST_ASSERT_FALSE(hs.hasM1M2Pair());
    TEST_ASSERT_FALSE(hs.isMinimumValid());
}

void test_m1m2_mismatched_replay_is_invalid(void) {
    // Field case (DIGIFIBRA/MERCUSYS): same-ish frames, different replay counters.
    CapturedHandshake hs = withM1M2(kStaA, 2, kStaA, 1);
    TEST_ASSERT_FALSE(hs.hasM1M2Pair());
    TEST_ASSERT_FALSE(hs.isMinimumValid());
}

void test_m2m3_pairs_when_m3_replay_is_m2_plus_one(void) {
    CapturedHandshake hs{};
    hs.hasMsg2 = true;
    hs.hasMsg3 = true;
    memcpy(hs.msg2Client, kStaA, 6);
    memcpy(hs.msg3Client, kStaA, 6);
    hs.msg2Replay = 7;
    hs.msg3Replay = 8;  // M3 is one past M2 in the same exchange
    TEST_ASSERT_TRUE(hs.hasM2M3Pair());
    TEST_ASSERT_TRUE(hs.isMinimumValid());
}

void test_m2m3_wrong_replay_delta_is_invalid(void) {
    CapturedHandshake hs{};
    hs.hasMsg2 = true;
    hs.hasMsg3 = true;
    memcpy(hs.msg2Client, kStaA, 6);
    memcpy(hs.msg3Client, kStaA, 6);
    hs.msg2Replay = 7;
    hs.msg3Replay = 7;  // not +1
    TEST_ASSERT_FALSE(hs.hasM2M3Pair());
    TEST_ASSERT_FALSE(hs.isMinimumValid());
}

void test_m2_alone_is_invalid(void) {
    CapturedHandshake hs{};
    hs.hasMsg2 = true;
    memcpy(hs.msg2Client, kStaA, 6);
    hs.msg2Replay = 3;
    TEST_ASSERT_FALSE(hs.isMinimumValid());
}

void test_wpasec_valid_requires_beacon_and_m1m2_pair(void) {
    CapturedHandshake hs = withM1M2(kStaA, 5, kStaA, 5);
    TEST_ASSERT_FALSE(hs.isWpaSecValid());  // no beacon yet

    const uint8_t beacon[16] = {0x80, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
    hs.setBeacon(beacon, sizeof(beacon));
    TEST_ASSERT_TRUE(hs.isWpaSecValid());

    // A mismatched pair with a beacon is still not WPA-SEC valid.
    CapturedHandshake bad = withM1M2(kStaA, 2, kStaB, 1);
    bad.setBeacon(beacon, sizeof(beacon));
    TEST_ASSERT_FALSE(bad.isWpaSecValid());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_m1m2_same_client_same_replay_is_valid);
    RUN_TEST(test_m1m2_different_client_is_invalid);
    RUN_TEST(test_m1m2_mismatched_replay_is_invalid);
    RUN_TEST(test_m2m3_pairs_when_m3_replay_is_m2_plus_one);
    RUN_TEST(test_m2m3_wrong_replay_delta_is_invalid);
    RUN_TEST(test_m2_alone_is_invalid);
    RUN_TEST(test_wpasec_valid_requires_beacon_and_m1m2_pair);
    return UNITY_END();
}
