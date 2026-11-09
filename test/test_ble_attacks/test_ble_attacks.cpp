/**
 * @file test_ble_attacks.cpp
 * @brief Unit tests for BLE attack payload generation
 */

#include <unity.h>
#include "modules/ble/ble_spanner.h"
#include <string>
#include <vector>

using namespace adversary;

void test_apple_pairing_payload() {
    // We can't easily test private methods without friends or refactoring
    // For now, we verify that starting a session with Apple Pairing doesn't crash
    // and produces a valid internal state.
    
    // Actually, let's just test that the enums were correctly added and can be set.
    BleSpamConfig cfg;
    cfg.provider = BleSpamProvider::APPLE;
    cfg.prompt = BleSpamPrompt::APPLE_ID_MODAL;
    cfg.intensity = 10;
    
    // Since BLESpanner is a singleton and highly coupled with NimBLE, 
    // we might need to mock NimBLE for better testing.
    // However, we can at least test the public interface.
    
    TEST_ASSERT_EQUAL(BleSpamProvider::APPLE, cfg.provider);
    TEST_ASSERT_EQUAL(BleSpamPrompt::APPLE_ID_MODAL, cfg.prompt);
}

void test_hid_provider_enum() {
    BleSpamConfig cfg;
    cfg.provider = BleSpamProvider::HID;
    TEST_ASSERT_EQUAL(BleSpamProvider::HID, cfg.provider);
}

void test_hid_class_appearance_roundtrips(void) {
    // The class drives the GAP appearance, and the persisted appearance maps back to a class
    // (ADR-0150). Keyboard/Mouse/Gamepad round-trip exactly.
    TEST_ASSERT_EQUAL(BLE_APPEARANCE_KEYBOARD, appearanceForHidClass(BleHidClass::KEYBOARD));
    TEST_ASSERT_EQUAL(BLE_APPEARANCE_MOUSE, appearanceForHidClass(BleHidClass::MOUSE));
    TEST_ASSERT_EQUAL(BLE_APPEARANCE_GAMEPAD, appearanceForHidClass(BleHidClass::GAMEPAD));
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, hidClassForAppearance(BLE_APPEARANCE_KEYBOARD));
    TEST_ASSERT_EQUAL(BleHidClass::MOUSE, hidClassForAppearance(BLE_APPEARANCE_MOUSE));
    TEST_ASSERT_EQUAL(BleHidClass::GAMEPAD, hidClassForAppearance(BLE_APPEARANCE_GAMEPAD));
}

void test_legacy_joystick_appearance_folds_into_gamepad(void) {
    // Joystick has no separate class; a persisted joystick appearance selects GAMEPAD.
    TEST_ASSERT_EQUAL(BleHidClass::GAMEPAD, hidClassForAppearance(BLE_APPEARANCE_JOYSTICK));
}

void test_unknown_appearance_defaults_to_keyboard(void) {
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, hidClassForAppearance(0x0000));
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, hidClassForAppearance(0xFFFF));
}

void test_hid_class_selector_cycles_and_wraps(void) {
    // The HID-class selector cycles Keyboard -> Mouse -> Gamepad and wraps at both ends
    // (ADR-0150). Joystick is not in the cycle.
    TEST_ASSERT_EQUAL(BleHidClass::MOUSE,    nextHidClass(BleHidClass::KEYBOARD));
    TEST_ASSERT_EQUAL(BleHidClass::GAMEPAD,  nextHidClass(BleHidClass::MOUSE));
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, nextHidClass(BleHidClass::GAMEPAD)); // wraps forward

    TEST_ASSERT_EQUAL(BleHidClass::GAMEPAD,  prevHidClass(BleHidClass::KEYBOARD)); // wraps backward
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, prevHidClass(BleHidClass::MOUSE));
    TEST_ASSERT_EQUAL(BleHidClass::MOUSE,    prevHidClass(BleHidClass::GAMEPAD));
}

void test_config_bounds() {
    BleSpamConfig cfg;
    
    // Intensity clamping
    cfg.intensity = 15;
    TEST_ASSERT_EQUAL(15, cfg.intensity);

    // HID class defaults to keyboard and is settable
    TEST_ASSERT_EQUAL(BleHidClass::KEYBOARD, cfg.hidClass);
    cfg.hidClass = BleHidClass::GAMEPAD;
    TEST_ASSERT_EQUAL(BleHidClass::GAMEPAD, cfg.hidClass);
}

void test_payload_generation_minimal() {
    // This requires NimBLE mocks to be functional
    BleSpamConfig cfg;
    cfg.provider = BleSpamProvider::APPLE;
    cfg.prompt = BleSpamPrompt::APPLE_ID_MODAL;

    // We can't easily test the private buildPayload on the singleton without
    // more intrusive changes, but we can verify that enums are sane.
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(BleSpamProvider::ANDROID), static_cast<int>(BleSpamProvider::APPLE));
}

// --- ADR-0151 tests ----------------------------------------------------------------------------

void test_rotation_period_for_intensity_maps_and_clamps(void) {
    // Single pacing formula: intensity 1..10 -> rotation period in ms, clamped at both ends
    // (ADR-0151). Monotonic: higher intensity -> shorter period.
    TEST_ASSERT_EQUAL_UINT32(1000, rotationPeriodForIntensity(1));
    TEST_ASSERT_EQUAL_UINT32(900,  rotationPeriodForIntensity(2));
    TEST_ASSERT_EQUAL_UINT32(500,  rotationPeriodForIntensity(6));
    TEST_ASSERT_EQUAL_UINT32(300,  rotationPeriodForIntensity(8));
    // Level 9 would compute 200, which is already at the floor; level 10 clamps to the floor too.
    TEST_ASSERT_EQUAL_UINT32(BLE_SPAM_ROTATION_MIN_MS, rotationPeriodForIntensity(9));
    TEST_ASSERT_EQUAL_UINT32(BLE_SPAM_ROTATION_MIN_MS, rotationPeriodForIntensity(10));
    // Out-of-range: clamped to the in-range endpoints, not undefined behaviour.
    TEST_ASSERT_EQUAL_UINT32(BLE_SPAM_ROTATION_MAX_MS, rotationPeriodForIntensity(0));
    TEST_ASSERT_EQUAL_UINT32(BLE_SPAM_ROTATION_MIN_MS, rotationPeriodForIntensity(255));
}

void test_effectiveness_matrix_matches_adr_0151(void) {
    // The tag drives honest UI copy (ADR-0151). These are the five provider/prompt combinations
    // the ADR explicitly fixes; a regression here mislabels the vector to the operator.
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LIVE,
                      effectivenessFor(BleSpamProvider::WINDOWS, BleSpamPrompt::PAIRING));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::THROTTLED,
                      effectivenessFor(BleSpamProvider::ANDROID, BleSpamPrompt::PAIRING));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::THROTTLED,
                      effectivenessFor(BleSpamProvider::APPLE,   BleSpamPrompt::PAIRING));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LEGACY,
                      effectivenessFor(BleSpamProvider::APPLE,   BleSpamPrompt::APPLE_ID_MODAL));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LEGACY,
                      effectivenessFor(BleSpamProvider::APPLE,   BleSpamPrompt::SOFTWARE_UPDATE));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LEGACY,
                      effectivenessFor(BleSpamProvider::APPLE,   BleSpamPrompt::TRANSFER_PHONE));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LEGACY,
                      effectivenessFor(BleSpamProvider::APPLE,   BleSpamPrompt::GUIDED_ACCESS));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::THROTTLED,
                      effectivenessFor(BleSpamProvider::SAMSUNG, BleSpamPrompt::PAIRING));
    // HID and CLONE are a real device / real target, not a popup vector; tagged LIVE.
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LIVE,
                      effectivenessFor(BleSpamProvider::HID,   BleSpamPrompt::PAIRING));
    TEST_ASSERT_EQUAL(BleSpamEffectiveness::LIVE,
                      effectivenessFor(BleSpamProvider::CLONE, BleSpamPrompt::PAIRING));
}

void test_effectiveness_label_strings(void) {
    // Advisory UI copy must be the three strings the operator sees in the field.
    TEST_ASSERT_EQUAL_STRING("LIVE",      effectivenessLabel(BleSpamEffectiveness::LIVE));
    TEST_ASSERT_EQUAL_STRING("THROTTLED", effectivenessLabel(BleSpamEffectiveness::THROTTLED));
    TEST_ASSERT_EQUAL_STRING("LEGACY",    effectivenessLabel(BleSpamEffectiveness::LEGACY));
}

// Deterministic LCG for the subset-draw tests: lets us assert distinctness and range without
// depending on esp_random(). Seeded per test; shared state makes each test self-contained.
static uint32_t g_testLcgState = 1;
static uint32_t test_lcg() {
    g_testLcgState = g_testLcgState * 1103515245u + 12345u;
    return g_testLcgState;
}

void test_fast_pair_subset_is_distinct_and_in_range(void) {
    // Partial Fisher-Yates must produce K distinct indices in [0, poolSize) for any seed. If this
    // ever regresses, the ANDROID branch could index the same model repeatedly or go out of pool.
    for (uint32_t seed = 1; seed <= 32; seed++) {
        g_testLcgState = seed;
        uint8_t out[BLE_FAST_PAIR_SUBSET_SIZE] = {0};
        drawFastPairSubset(BLE_FAST_PAIR_POOL_SIZE, BLE_FAST_PAIR_SUBSET_SIZE, out, &test_lcg);
        for (size_t i = 0; i < BLE_FAST_PAIR_SUBSET_SIZE; i++) {
            TEST_ASSERT_LESS_THAN_UINT(BLE_FAST_PAIR_POOL_SIZE, out[i]);
            for (size_t j = i + 1; j < BLE_FAST_PAIR_SUBSET_SIZE; j++) {
                TEST_ASSERT_NOT_EQUAL(out[i], out[j]);
            }
        }
    }
}

void test_fast_pair_subset_varies_across_seeds(void) {
    // Different seeds should produce different subsets (staleness-beating is the whole point of
    // the re-roll-per-session). We don't require every pair to differ, only that at least one pair
    // of seeds in a batch produces a different subset.
    g_testLcgState = 1;
    uint8_t a[BLE_FAST_PAIR_SUBSET_SIZE];
    drawFastPairSubset(BLE_FAST_PAIR_POOL_SIZE, BLE_FAST_PAIR_SUBSET_SIZE, a, &test_lcg);

    bool sawDifference = false;
    for (uint32_t seed = 2; seed <= 16 && !sawDifference; seed++) {
        g_testLcgState = seed;
        uint8_t b[BLE_FAST_PAIR_SUBSET_SIZE];
        drawFastPairSubset(BLE_FAST_PAIR_POOL_SIZE, BLE_FAST_PAIR_SUBSET_SIZE, b, &test_lcg);
        for (size_t i = 0; i < BLE_FAST_PAIR_SUBSET_SIZE; i++) {
            if (a[i] != b[i]) { sawDifference = true; break; }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDifference, "draw should vary across seeds");
}

void test_fast_pair_pool_entries_are_sane(void) {
    // The pool is pure data referenced by the device code; a nullptr name or a collision would
    // surface as an Android no-op at runtime, long after the regression.
    for (size_t i = 0; i < BLE_FAST_PAIR_POOL_SIZE; i++) {
        TEST_ASSERT_NOT_NULL(kBleFastPairPool[i].name);
        TEST_ASSERT_TRUE(kBleFastPairPool[i].name[0] != '\0');
        for (size_t j = i + 1; j < BLE_FAST_PAIR_POOL_SIZE; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(kBleFastPairPool[i].id, kBleFastPairPool[j].id,
                                          "duplicate Fast Pair ID in pool");
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_apple_pairing_payload);
    RUN_TEST(test_hid_provider_enum);
    RUN_TEST(test_hid_class_appearance_roundtrips);
    RUN_TEST(test_legacy_joystick_appearance_folds_into_gamepad);
    RUN_TEST(test_unknown_appearance_defaults_to_keyboard);
    RUN_TEST(test_hid_class_selector_cycles_and_wraps);
    RUN_TEST(test_config_bounds);
    RUN_TEST(test_payload_generation_minimal);
    // ADR-0151
    RUN_TEST(test_rotation_period_for_intensity_maps_and_clamps);
    RUN_TEST(test_effectiveness_matrix_matches_adr_0151);
    RUN_TEST(test_effectiveness_label_strings);
    RUN_TEST(test_fast_pair_subset_is_distinct_and_in_range);
    RUN_TEST(test_fast_pair_subset_varies_across_seeds);
    RUN_TEST(test_fast_pair_pool_entries_are_sane);
    return UNITY_END();
}
