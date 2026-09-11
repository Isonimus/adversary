/**
 * @file test_cc1101_fsk.cpp
 * @brief Regression for the CC1101 FSK modem register math (slice-0019 Phase 2).
 *
 * Deviation, data rate and RX bandwidth must be programmed to match an FSK
 * transmitter, and a wrong mantissa/exponent mistunes the demod into noise as
 * silently as a wrong carrier. These are the pure, host-testable core (the driver
 * I/O and the screen stay hardware-only). Anchor vectors are SmartRF-documented
 * values; the composed MDMCFG4 (0x87) also matches the hand-written OOK block, so
 * a formula regression is caught against a known-good register.
 */

#include <unity.h>
#include "hal/expansion/cc1101.h"

using adversary::hal::Cc1101DrateRegs;
using adversary::hal::Cc1101ModemRegs;
using adversary::hal::cc1101ChanbwNibble;
using adversary::hal::cc1101DeviatnReg;
using adversary::hal::cc1101DrateRegs;
using adversary::hal::cc1101FskConfigValid;
using adversary::hal::cc1101ModemRegs;
using adversary::hal::CC1101_MOD_2FSK;
using adversary::hal::CC1101_MOD_GFSK;
using adversary::hal::CC1101_MOD_MSK;

void setUp(void) {}
void tearDown(void) {}

// --- DEVIATN -----------------------------------------------------------------

// 47.6 kHz -> E=4,M=7 -> 0x47. The SmartRF default and the OOK block's DEVIATN.
void test_deviatn_47k(void) {
    TEST_ASSERT_EQUAL_HEX8(0x47, cc1101DeviatnReg(47607.0));
}

// 5.157 kHz -> E=1,M=5 -> 0x15 (SmartRF low-deviation preset).
void test_deviatn_5k(void) {
    TEST_ASSERT_EQUAL_HEX8(0x15, cc1101DeviatnReg(5157.0));
}

// 380.859 kHz -> E=7,M=7 -> 0x77 (top of the representable range).
void test_deviatn_max(void) {
    TEST_ASSERT_EQUAL_HEX8(0x77, cc1101DeviatnReg(380859.0));
}

// --- DRATE -------------------------------------------------------------------

// 4.8 kBaud -> DRATE_E=7, DRATE_M=0x83.
void test_drate_4k8(void) {
    Cc1101DrateRegs r = cc1101DrateRegs(4800.0);
    TEST_ASSERT_EQUAL_UINT8(7, r.drateE);
    TEST_ASSERT_EQUAL_HEX8(0x83, r.drateM);
}

// 250 kBaud -> DRATE_E=13, DRATE_M=0x3B (SmartRF 250k GFSK preset).
void test_drate_250k(void) {
    Cc1101DrateRegs r = cc1101DrateRegs(250000.0);
    TEST_ASSERT_EQUAL_UINT8(13, r.drateE);
    TEST_ASSERT_EQUAL_HEX8(0x3B, r.drateM);
}

// 1.2 kBaud shares the mantissa with 4.8k, one octave down (E=5).
void test_drate_1k2(void) {
    Cc1101DrateRegs r = cc1101DrateRegs(1200.0);
    TEST_ASSERT_EQUAL_UINT8(5, r.drateE);
    TEST_ASSERT_EQUAL_HEX8(0x83, r.drateM);
}

// --- CHANBW (returned in the low nibble; occupies MDMCFG4[7:4]) ---------------

// 203.125 kHz -> E=2,M=0 -> 0x8 (matches the OOK block's MDMCFG4 high nibble).
void test_chanbw_203k(void) {
    TEST_ASSERT_EQUAL_HEX8(0x8, cc1101ChanbwNibble(203125.0));
}

// 101.5625 kHz -> E=3,M=0 -> 0xC.
void test_chanbw_101k(void) {
    TEST_ASSERT_EQUAL_HEX8(0xC, cc1101ChanbwNibble(101562.0));
}

// 58.036 kHz (narrowest) -> E=3,M=3 -> 0xF.
void test_chanbw_58k(void) {
    TEST_ASSERT_EQUAL_HEX8(0xF, cc1101ChanbwNibble(58036.0));
}

// 812.5 kHz (widest) -> E=0,M=0 -> 0x0.
void test_chanbw_812k(void) {
    TEST_ASSERT_EQUAL_HEX8(0x0, cc1101ChanbwNibble(812500.0));
}

// --- Composed triple ---------------------------------------------------------

// (47.6 kHz, 4.8 kBaud, 203 kHz) -> {0x47, 0x87, 0x83}. The MDMCFG4 0x87 is the
// load-bearing cross-check against the OOK block's hand-written value.
void test_modem_regs_compose(void) {
    Cc1101ModemRegs r = cc1101ModemRegs(47607.0, 4800.0, 203125.0);
    TEST_ASSERT_EQUAL_HEX8(0x47, r.deviatn);
    TEST_ASSERT_EQUAL_HEX8(0x87, r.mdmcfg4);
    TEST_ASSERT_EQUAL_HEX8(0x83, r.mdmcfg3);
}

// --- MSK data-rate floor (the guard the screen + HAL both consult) ------------

// MSK below ~26 kBaud is unrealisable on the CC1101 -> rejected.
void test_msk_below_floor_invalid(void) {
    TEST_ASSERT_FALSE(cc1101FskConfigValid(CC1101_MOD_MSK, 4800.0));
    TEST_ASSERT_FALSE(cc1101FskConfigValid(CC1101_MOD_MSK, 25999.0));
}

// MSK at or above the floor is valid.
void test_msk_at_floor_valid(void) {
    TEST_ASSERT_TRUE(cc1101FskConfigValid(CC1101_MOD_MSK, 26000.0));
    TEST_ASSERT_TRUE(cc1101FskConfigValid(CC1101_MOD_MSK, 250000.0));
}

// 2-FSK/GFSK have no such floor: low data rates stay valid.
void test_fsk_gfsk_have_no_floor(void) {
    TEST_ASSERT_TRUE(cc1101FskConfigValid(CC1101_MOD_2FSK, 1200.0));
    TEST_ASSERT_TRUE(cc1101FskConfigValid(CC1101_MOD_GFSK, 2400.0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_deviatn_47k);
    RUN_TEST(test_deviatn_5k);
    RUN_TEST(test_deviatn_max);
    RUN_TEST(test_drate_4k8);
    RUN_TEST(test_drate_250k);
    RUN_TEST(test_drate_1k2);
    RUN_TEST(test_chanbw_203k);
    RUN_TEST(test_chanbw_101k);
    RUN_TEST(test_chanbw_58k);
    RUN_TEST(test_chanbw_812k);
    RUN_TEST(test_modem_regs_compose);
    RUN_TEST(test_msk_below_floor_invalid);
    RUN_TEST(test_msk_at_floor_valid);
    RUN_TEST(test_fsk_gfsk_have_no_floor);
    return UNITY_END();
}
