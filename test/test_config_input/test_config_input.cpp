/**
 * @file test_config_input.cpp
 * @brief Host tests for the pure config-form input classifier (ADR-0107, slice-0108).
 *
 * Pins the whole ADR-0092 keyboard model as a table: the nav/edit keys and both aliases, ENTER
 * against all four row kinds (the ENTER-by-kind table is the thing that drifted into an
 * ENTER-inert bug before, so it is the core of this suite), and a sample of unowned keys
 * returning None. classifyConfigKey is pure, so no canvas and no mock are needed.
 */

#include <unity.h>
#include "ui/components/config_input.h"

using adversary::ui::classifyConfigKey;
using adversary::ui::ConfigRowKind;
using adversary::ui::ConfigKeyAction;

void setUp() {}
void tearDown() {}

// Row kind does not change the nav/edit keys; use Value as the neutral kind for those.
static ConfigKeyAction onValue(char key) {
    return classifyConfigKey(key, ConfigRowKind::Value);
}

// ';' navigates up, '.' navigates down, regardless of row kind.
void test_semicolon_is_nav_up() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::NavUp, onValue(';'));
    TEST_ASSERT_EQUAL(ConfigKeyAction::NavUp, classifyConfigKey(';', ConfigRowKind::Terminal));
}

void test_dot_is_nav_down() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::NavDown, onValue('.'));
    TEST_ASSERT_EQUAL(ConfigKeyAction::NavDown, classifyConfigKey('.', ConfigRowKind::Terminal));
}

// ',' and its '<' alias both edit the field to its previous value.
void test_comma_and_alias_are_edit_prev() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditPrev, onValue(','));
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditPrev, onValue('<'));
}

// '/' and its '>' alias both edit the field to its next value.
void test_slash_and_alias_are_edit_next() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditNext, onValue('/'));
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditNext, onValue('>'));
}

// ENTER on a value row advances the field (never inert): identical to '/'. Both aliases.
void test_enter_on_value_row_advances() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditNext, classifyConfigKey('\n', ConfigRowKind::Value));
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditNext, classifyConfigKey('\r', ConfigRowKind::Value));
}

// ENTER on the terminal (START) row activates it.
void test_enter_on_terminal_row_activates() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::Activate, classifyConfigKey('\n', ConfigRowKind::Terminal));
    TEST_ASSERT_EQUAL(ConfigKeyAction::Activate, classifyConfigKey('\r', ConfigRowKind::Terminal));
}

// ENTER on a navigation row routes to the other screen (the WiFi scanner).
void test_enter_on_navigation_row_navigates() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::Navigate, classifyConfigKey('\n', ConfigRowKind::Navigation));
    TEST_ASSERT_EQUAL(ConfigKeyAction::Navigate, classifyConfigKey('\r', ConfigRowKind::Navigation));
}

// ENTER on a popup row opens its popup.
void test_enter_on_popup_row_opens_popup() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::Popup, classifyConfigKey('\n', ConfigRowKind::Popup));
    TEST_ASSERT_EQUAL(ConfigKeyAction::Popup, classifyConfigKey('\r', ConfigRowKind::Popup));
}

// The edit keys do NOT depend on row kind: ',' / '/' stay EditPrev / EditNext even on a
// terminal/navigation/popup row (only ENTER's meaning varies by kind).
void test_edit_keys_ignore_row_kind() {
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditPrev, classifyConfigKey(',', ConfigRowKind::Terminal));
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditNext, classifyConfigKey('/', ConfigRowKind::Navigation));
    TEST_ASSERT_EQUAL(ConfigKeyAction::EditPrev, classifyConfigKey('<', ConfigRowKind::Popup));
}

// Keys the classifier does not own return None so the screen keeps its hotkeys/back/state logic.
void test_unowned_keys_are_none() {
    const char unowned[] = {'m', 'M', 'p', 's', 'S', ' ', '`', 27, 'q', 'c', 'l', 'd'};
    for (char k : unowned) {
        TEST_ASSERT_EQUAL(ConfigKeyAction::None, onValue(k));
    }
    // Back ('`') is unowned regardless of row kind: only ENTER's meaning varies by kind.
    TEST_ASSERT_EQUAL(ConfigKeyAction::None, classifyConfigKey('`', ConfigRowKind::Terminal));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_semicolon_is_nav_up);
    RUN_TEST(test_dot_is_nav_down);
    RUN_TEST(test_comma_and_alias_are_edit_prev);
    RUN_TEST(test_slash_and_alias_are_edit_next);
    RUN_TEST(test_enter_on_value_row_advances);
    RUN_TEST(test_enter_on_terminal_row_activates);
    RUN_TEST(test_enter_on_navigation_row_navigates);
    RUN_TEST(test_enter_on_popup_row_opens_popup);
    RUN_TEST(test_edit_keys_ignore_row_kind);
    RUN_TEST(test_unowned_keys_are_none);
    return UNITY_END();
}
