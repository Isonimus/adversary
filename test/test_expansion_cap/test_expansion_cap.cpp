/**
 * @file test_expansion_cap.cpp
 * @brief Truth-table regression for resolveExpansionCap() (slice-0002).
 *
 * The SPI probes cross the HAL boundary and are verified on-device; this covers
 * the pure precedence logic they feed into: override wins over the probe, and on
 * Auto the multi-radio cap is present iff either radio answered.
 */

#include <unity.h>
#include "hal/expansion/expansion_cap.h"

using adversary::hal::CapOverride;
using adversary::hal::ExpansionCap;
using adversary::hal::resolveExpansionCap;

void setUp(void) {}
void tearDown(void) {}

// --- Auto: multi-radio iff either radio answers -------------------------------

void test_auto_no_radio_is_none(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(false, false, CapOverride::Auto) ==
                     ExpansionCap::None);
}

void test_auto_cc1101_only_is_multiradio(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(true, false, CapOverride::Auto) ==
                     ExpansionCap::MultiRadio);
}

void test_auto_nrf24_only_is_multiradio(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(false, true, CapOverride::Auto) ==
                     ExpansionCap::MultiRadio);
}

void test_auto_both_radios_is_multiradio(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(true, true, CapOverride::Auto) ==
                     ExpansionCap::MultiRadio);
}

// --- ForceNone: None regardless of probe --------------------------------------

void test_forcenone_ignores_present_probes(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(true, true, CapOverride::ForceNone) ==
                     ExpansionCap::None);
    TEST_ASSERT_TRUE(resolveExpansionCap(false, false, CapOverride::ForceNone) ==
                     ExpansionCap::None);
}

// --- ForceMultiRadio: MultiRadio even when the probe fails --------------------
// This is DoD scenario 3: a re-seated cap probes false, the override still wins.

void test_forcemultiradio_wins_over_failed_probe(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(false, false,
                                         CapOverride::ForceMultiRadio) ==
                     ExpansionCap::MultiRadio);
}

void test_forcemultiradio_with_present_probes(void) {
    TEST_ASSERT_TRUE(resolveExpansionCap(true, true,
                                         CapOverride::ForceMultiRadio) ==
                     ExpansionCap::MultiRadio);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_auto_no_radio_is_none);
    RUN_TEST(test_auto_cc1101_only_is_multiradio);
    RUN_TEST(test_auto_nrf24_only_is_multiradio);
    RUN_TEST(test_auto_both_radios_is_multiradio);
    RUN_TEST(test_forcenone_ignores_present_probes);
    RUN_TEST(test_forcemultiradio_wins_over_failed_probe);
    RUN_TEST(test_forcemultiradio_with_present_probes);
    return UNITY_END();
}
