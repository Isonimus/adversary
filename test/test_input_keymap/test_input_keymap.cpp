/**
 * @file test_input_keymap.cpp
 * @brief Regression tests for the pure key-normalization seam (slice-0030).
 *
 * Guards the two mappings extracted out of handleInput(): the Cardputer modifier+key
 * normalization and the M5Stick InputAction->char table. These are silent-regression-prone
 * (a swapped LEFT/RIGHT or a broken Del is invisible until pressed), so both mappings and their
 * fall-through/no-key cases are asserted here. Fails to compile before this slice (the symbols
 * do not exist) and passes after.
 */

#include <unity.h>
#include "hal/input/input_keymap.h"

using namespace adversary;

void setUp(void) {}
void tearDown(void) {}

// Enter takes precedence and maps to newline.
void test_cardputer_enter_is_newline() {
    TEST_ASSERT_EQUAL_INT('\n', normalizeCardputerKey(true, false, 'x'));
}

// Del maps to backspace (0x08).
void test_cardputer_del_is_backspace() {
    TEST_ASSERT_EQUAL_INT(0x08, normalizeCardputerKey(false, true, 'x'));
}

// A printable key with no modifier passes through unchanged.
void test_cardputer_printable_passes_through() {
    TEST_ASSERT_EQUAL_INT('a', normalizeCardputerKey(false, false, 'a'));
}

// No modifier and no printable key yields 0 ("no key"); handleInput() returns early on that.
void test_cardputer_no_key_is_zero() {
    TEST_ASSERT_EQUAL_INT(0, normalizeCardputerKey(false, false, 0));
}

// Enter wins over Del when both are somehow set (matches the old if/else-if order).
void test_cardputer_enter_beats_del() {
    TEST_ASSERT_EQUAL_INT('\n', normalizeCardputerKey(true, true, 0));
}

// M5Stick navigation: UP/LEFT share ';', DOWN/RIGHT share '.'.
void test_action_navigation_chars() {
    TEST_ASSERT_EQUAL_INT(';', inputActionToKey(InputAction::UP));
    TEST_ASSERT_EQUAL_INT(';', inputActionToKey(InputAction::LEFT));
    TEST_ASSERT_EQUAL_INT('.', inputActionToKey(InputAction::DOWN));
    TEST_ASSERT_EQUAL_INT('.', inputActionToKey(InputAction::RIGHT));
}

// M5Stick primary actions: SELECT -> newline, BACK -> backtick.
void test_action_primary_chars() {
    TEST_ASSERT_EQUAL_INT('\n', inputActionToKey(InputAction::SELECT));
    TEST_ASSERT_EQUAL_INT('`', inputActionToKey(InputAction::BACK));
}

// NONE and any unmapped action fall through to '\0'; handleInput() returns early on that.
void test_action_unmapped_is_null() {
    TEST_ASSERT_EQUAL_INT('\0', inputActionToKey(InputAction::NONE));
    TEST_ASSERT_EQUAL_INT('\0', inputActionToKey(InputAction::MENU));
    TEST_ASSERT_EQUAL_INT('\0', inputActionToKey(InputAction::CHAR));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cardputer_enter_is_newline);
    RUN_TEST(test_cardputer_del_is_backspace);
    RUN_TEST(test_cardputer_printable_passes_through);
    RUN_TEST(test_cardputer_no_key_is_zero);
    RUN_TEST(test_cardputer_enter_beats_del);
    RUN_TEST(test_action_navigation_chars);
    RUN_TEST(test_action_primary_chars);
    RUN_TEST(test_action_unmapped_is_null);
    return UNITY_END();
}
