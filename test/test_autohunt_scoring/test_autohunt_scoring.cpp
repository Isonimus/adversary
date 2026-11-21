/**
 * @file test_autohunt_scoring.cpp
 * @brief Unit tests for the Autohunt per-target scoring function (ADR-0161).
 *
 * Each weight is pinned independently, then the composite scenario from the
 * DoD-#1 case (two WPA2 targets at the same RSSI, one PMF-required with a
 * Deauth-only intent) verifies the end-to-end preference.
 *
 * Regression guard: inlining `scoreTarget` to return `rssi + 100` alone fails
 * every weighted case below.
 */

#include <unity.h>

#include "utils/autohunt_scoring.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

static TargetScoreInputs baseline() {
    // The baseline is a plain WPA2-PSK target with no prior state.
    return TargetScoreInputs{
        /*rssi=*/-40,
        /*isWpa2Psk=*/true,
        /*pmfRequired=*/false,
        /*intentIsDeauthOnly=*/true,
        /*observedClientsHint=*/0,
        /*priorFailures=*/0,
    };
}

// ---------- RSSI normalization ----------

void test_rssi_minus_40_without_bonuses_is_60(void) {
    TargetScoreInputs in = baseline();
    in.isWpa2Psk = false;
    TEST_ASSERT_EQUAL_INT16(60, scoreTarget(in));
}

void test_rssi_minus_90_without_bonuses_is_10(void) {
    TargetScoreInputs in = baseline();
    in.isWpa2Psk = false;
    in.rssi = -90;
    TEST_ASSERT_EQUAL_INT16(10, scoreTarget(in));
}

// ---------- WPA2 preference ----------

void test_wpa2_adds_exactly_ten(void) {
    TargetScoreInputs a = baseline();
    a.isWpa2Psk = false;
    TargetScoreInputs b = a;
    b.isWpa2Psk = true;
    TEST_ASSERT_EQUAL_INT16(10, scoreTarget(b) - scoreTarget(a));
}

// ---------- PMF + Deauth-only demote ----------

void test_pmf_with_deauth_only_demotes_by_twenty(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.pmfRequired = true;
    TEST_ASSERT_EQUAL_INT16(-20, scoreTarget(b) - scoreTarget(a));
}

void test_pmf_without_deauth_only_is_neutral(void) {
    TargetScoreInputs a = baseline();
    a.intentIsDeauthOnly = false;
    TargetScoreInputs b = a;
    b.pmfRequired = true;
    TEST_ASSERT_EQUAL_INT16(0, scoreTarget(b) - scoreTarget(a));
}

// ---------- Observed-clients hint ----------

void test_observed_clients_hint_adds_fifteen(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.observedClientsHint = 1;
    TEST_ASSERT_EQUAL_INT16(15, scoreTarget(b) - scoreTarget(a));
}

void test_observed_clients_zero_is_neutral(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.observedClientsHint = 0;
    TEST_ASSERT_EQUAL_INT16(0, scoreTarget(b) - scoreTarget(a));
}

void test_observed_clients_hint_saturates_above_one(void) {
    // Any non-zero hint is treated the same: we are not weighting multiple clients
    // higher than one, because one associated client is what the four-way needs.
    TargetScoreInputs a = baseline();
    a.observedClientsHint = 1;
    TargetScoreInputs b = a;
    b.observedClientsHint = 7;
    TEST_ASSERT_EQUAL_INT16(0, scoreTarget(b) - scoreTarget(a));
}

// ---------- Prior-failure demote ----------

void test_one_prior_failure_demotes_by_five(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.priorFailures = 1;
    TEST_ASSERT_EQUAL_INT16(-5, scoreTarget(b) - scoreTarget(a));
}

void test_four_prior_failures_demote_by_twenty(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.priorFailures = 4;
    TEST_ASSERT_EQUAL_INT16(-20, scoreTarget(b) - scoreTarget(a));
}

void test_prior_failures_floor_at_minus_twenty(void) {
    TargetScoreInputs a = baseline();
    TargetScoreInputs b = a;
    b.priorFailures = 100;
    TEST_ASSERT_EQUAL_INT16(-20, scoreTarget(b) - scoreTarget(a));
}

// ---------- DoD-#1 composite ----------

void test_two_wpa2_same_rssi_pmf_vs_non_pmf_non_pmf_wins_by_twenty(void) {
    // Given two WPA2-PSK targets at the same RSSI, Deauth-only intent, one PMF-required.
    TargetScoreInputs non_pmf = baseline();
    TargetScoreInputs pmf     = baseline();
    pmf.pmfRequired = true;

    // When: score both.
    const int16_t non_pmf_score = scoreTarget(non_pmf);
    const int16_t pmf_score     = scoreTarget(pmf);

    // Then: non-PMF wins, by exactly the demote weight.
    TEST_ASSERT_TRUE(non_pmf_score > pmf_score);
    TEST_ASSERT_EQUAL_INT16(20, non_pmf_score - pmf_score);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rssi_minus_40_without_bonuses_is_60);
    RUN_TEST(test_rssi_minus_90_without_bonuses_is_10);
    RUN_TEST(test_wpa2_adds_exactly_ten);
    RUN_TEST(test_pmf_with_deauth_only_demotes_by_twenty);
    RUN_TEST(test_pmf_without_deauth_only_is_neutral);
    RUN_TEST(test_observed_clients_hint_adds_fifteen);
    RUN_TEST(test_observed_clients_zero_is_neutral);
    RUN_TEST(test_observed_clients_hint_saturates_above_one);
    RUN_TEST(test_one_prior_failure_demotes_by_five);
    RUN_TEST(test_four_prior_failures_demote_by_twenty);
    RUN_TEST(test_prior_failures_floor_at_minus_twenty);
    RUN_TEST(test_two_wpa2_same_rssi_pmf_vs_non_pmf_non_pmf_wins_by_twenty);
    return UNITY_END();
}
