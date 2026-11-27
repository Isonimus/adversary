/**
 * @file test_handshake_quality.cpp
 * @brief Regression for the crackability-based quality tier (slice-0173).
 *
 * The old code had two scales for the same concept: save-path persisted
 * msgCount*25 (so lone M1 = 25 but full 4-way = 100); list render recomputed
 * PMKID=100 / M1+M2+M3=90 / M2-present=70 / else=50 (so full 4-way shown as
 * 90 regardless of M4, lone M2 shown as 70 though not crackable). These
 * assert the single helper's tiers, which fail against either old code and
 * pass after.
 */

#include <unity.h>

#include "utils/handshake_quality.h"

using adversary::utils::HandshakeQualityFlags;
using adversary::utils::computeHandshakeQuality;

void setUp(void) {}
void tearDown(void) {}

namespace {

HandshakeQualityFlags flags(bool m1, bool m2, bool m3, bool m4, bool pmkid) {
    return HandshakeQualityFlags{m1, m2, m3, m4, pmkid};
}

}  // namespace

void test_pmkid_alone_is_100() {
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(false, false, false, false, true)));
}

void test_m1_m2_pair_is_100() {
    // ANonce (M1) + MIC (M2) = hashcat-crackable minimum.
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(true, true, false, false, false)));
}

void test_m2_m3_pair_is_100() {
    // When M1 is missed, M3 also carries the ANonce; M2 carries the MIC.
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(false, true, true, false, false)));
}

void test_full_4way_is_100() {
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(true, true, true, true, false)));
}

void test_lone_m1_is_25_fragment() {
    // ANonce alone, no MIC: a fragment, not crackable. Old save path over-credited
    // this to 25 and old list rendered it 50.
    TEST_ASSERT_EQUAL_INT(25, computeHandshakeQuality(flags(true, false, false, false, false)));
}

void test_lone_m2_is_25_fragment() {
    // MIC alone, no ANonce: a fragment, not crackable. Old list rendered 70.
    TEST_ASSERT_EQUAL_INT(25, computeHandshakeQuality(flags(false, true, false, false, false)));
}

void test_lone_m3_is_25_fragment() {
    TEST_ASSERT_EQUAL_INT(25, computeHandshakeQuality(flags(false, false, true, false, false)));
}

void test_lone_m4_is_25_fragment() {
    TEST_ASSERT_EQUAL_INT(25, computeHandshakeQuality(flags(false, false, false, true, false)));
}

void test_m1_m3_without_m2_is_50_partial() {
    // Two ANonce messages but no MIC: partial, not crackable.
    TEST_ASSERT_EQUAL_INT(50, computeHandshakeQuality(flags(true, false, true, false, false)));
}

void test_m1_m4_without_m2_or_m3_is_50_partial() {
    TEST_ASSERT_EQUAL_INT(50, computeHandshakeQuality(flags(true, false, false, true, false)));
}

void test_nothing_is_0() {
    TEST_ASSERT_EQUAL_INT(0, computeHandshakeQuality(flags(false, false, false, false, false)));
}

void test_pmkid_dominates_any_message_combo() {
    // PMKID is crackable standalone; a captured PMKID + any fragment is still 100.
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(true, false, false, false, true)));
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(false, false, false, true, true)));
    TEST_ASSERT_EQUAL_INT(100, computeHandshakeQuality(flags(true, true, true, true, true)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pmkid_alone_is_100);
    RUN_TEST(test_m1_m2_pair_is_100);
    RUN_TEST(test_m2_m3_pair_is_100);
    RUN_TEST(test_full_4way_is_100);
    RUN_TEST(test_lone_m1_is_25_fragment);
    RUN_TEST(test_lone_m2_is_25_fragment);
    RUN_TEST(test_lone_m3_is_25_fragment);
    RUN_TEST(test_lone_m4_is_25_fragment);
    RUN_TEST(test_m1_m3_without_m2_is_50_partial);
    RUN_TEST(test_m1_m4_without_m2_or_m3_is_50_partial);
    RUN_TEST(test_nothing_is_0);
    RUN_TEST(test_pmkid_dominates_any_message_combo);
    return UNITY_END();
}
