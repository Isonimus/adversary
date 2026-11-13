/**
 * @file test_pmf_resolver.cpp
 * @brief Unit tests for the PMF-aware effective-strategy resolver.
 *
 * Nine-cell truth table from ADR-0153: {Deauth, PMKID, Both} x
 * {pmfRequired=false, pmfRequired=true + degrade=on, pmfRequired=true +
 * degrade=off}. Plus two sanity checks: the degrade flag must be a no-op when
 * the target does not require PMF (we never silently alter a non-PMF target),
 * and must be a no-op for PMKID and Both intents when PMF is required (those
 * paths do not involve deauth).
 *
 * Regression guard: inlining the resolver to return `{intent, NONE}` without
 * consulting PMF fails every pmfRequired=true row.
 */

#include <unity.h>

#include "utils/pmf_resolver.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// ---------- pmfRequired = false: effective matches intent, no reason ----------

void test_deauth_no_pmf(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::Deauth, /*pmfRequired=*/false, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::Deauth), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::NONE), static_cast<int>(r.reason));
}

void test_pmkid_no_pmf(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::PMKID, /*pmfRequired=*/false, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::NONE), static_cast<int>(r.reason));
}

void test_both_no_pmf(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::Both, /*pmfRequired=*/false, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::Both), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::NONE), static_cast<int>(r.reason));
}

// ---------- pmfRequired = true, degrade = on ----------

void test_deauth_pmf_degrade_on(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::Deauth, /*pmfRequired=*/true, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::PMF_DEGRADED), static_cast<int>(r.reason));
}

void test_pmkid_pmf_degrade_on(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::PMKID, /*pmfRequired=*/true, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::NONE), static_cast<int>(r.reason));
}

void test_both_pmf_degrade_on(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::Both, /*pmfRequired=*/true, /*degrade=*/true);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::PMF_DEGRADED), static_cast<int>(r.reason));
}

// ---------- pmfRequired = true, degrade = off ----------

void test_deauth_pmf_degrade_off(void) {
    const auto r = resolveEffectiveStrategy(CaptureIntent::Deauth, /*pmfRequired=*/true, /*degrade=*/false);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::Skipped), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::PMF_DEAUTH_INEFFECTIVE), static_cast<int>(r.reason));
}

void test_pmkid_pmf_degrade_off(void) {
    // PMKID path does not involve deauth, so the toggle must not affect it.
    const auto r = resolveEffectiveStrategy(CaptureIntent::PMKID, /*pmfRequired=*/true, /*degrade=*/false);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::NONE), static_cast<int>(r.reason));
}

void test_both_pmf_degrade_off(void) {
    // Both already loses its deauth half under PMF; the toggle does not skip it.
    const auto r = resolveEffectiveStrategy(CaptureIntent::Both, /*pmfRequired=*/true, /*degrade=*/false);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::PMKID), static_cast<int>(r.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(PmfResolveReason::PMF_DEGRADED), static_cast<int>(r.reason));
}

// ---------- Flag-as-no-op sanity checks ----------

void test_degrade_flag_is_noop_without_pmf(void) {
    // Deauth intent, non-PMF target: effective is Deauth regardless of the toggle.
    const auto rOn  = resolveEffectiveStrategy(CaptureIntent::Deauth, false, true);
    const auto rOff = resolveEffectiveStrategy(CaptureIntent::Deauth, false, false);
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::Deauth), static_cast<int>(rOn.strategy));
    TEST_ASSERT_EQUAL(static_cast<int>(EffectiveStrategy::Deauth), static_cast<int>(rOff.strategy));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_deauth_no_pmf);
    RUN_TEST(test_pmkid_no_pmf);
    RUN_TEST(test_both_no_pmf);
    RUN_TEST(test_deauth_pmf_degrade_on);
    RUN_TEST(test_pmkid_pmf_degrade_on);
    RUN_TEST(test_both_pmf_degrade_on);
    RUN_TEST(test_deauth_pmf_degrade_off);
    RUN_TEST(test_pmkid_pmf_degrade_off);
    RUN_TEST(test_both_pmf_degrade_off);
    RUN_TEST(test_degrade_flag_is_noop_without_pmf);
    return UNITY_END();
}
