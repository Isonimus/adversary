/**
 * @file test_confirm_dialog.cpp
 * @brief Logic regression for the shared ConfirmDialog (ADR-0085 / slice-0086).
 *
 * The component's whole point is safe-by-default confirmation: the selection starts on Cancel
 * so a reflexive Enter cancels rather than executing an irreversible action. These cases pin
 * that default, the explicit-confirm paths, the cancel paths, and the severity contract. The
 * render(Canvas&) template is device-only and is covered by build + on-device review.
 */

#include <unity.h>
#include "ui/components/confirm_dialog.h"

using adversary::ui::ConfirmDialog;

static int s_confirms = 0;
static int s_cancels = 0;

void setUp() {
    s_confirms = 0;
    s_cancels = 0;
}

void tearDown() {}

static ConfirmDialog makeShownDialog() {
    ConfirmDialog dialog;
    dialog.setOnConfirm([] { ++s_confirms; });
    dialog.setOnCancel([] { ++s_cancels; });
    dialog.setQuestion("Delete handshake?");
    dialog.setSeverity(ConfirmDialog::Severity::Destructive);
    dialog.show();
    return dialog;
}

// The default highlight is Cancel, so an immediate Enter cancels and never confirms.
void test_default_is_cancel_so_enter_cancels() {
    ConfirmDialog dialog = makeShownDialog();
    TEST_ASSERT_FALSE(dialog.isConfirmSelected());

    TEST_ASSERT_TRUE(dialog.handleInput('\n'));
    TEST_ASSERT_EQUAL_INT(0, s_confirms);
    TEST_ASSERT_EQUAL_INT(1, s_cancels);
    TEST_ASSERT_FALSE(dialog.isVisible());
}

// Moving the selection to Confirm and pressing Enter confirms exactly once.
void test_navigate_to_confirm_then_enter_confirms() {
    ConfirmDialog dialog = makeShownDialog();

    TEST_ASSERT_TRUE(dialog.handleInput('.'));  // move to Confirm
    TEST_ASSERT_TRUE(dialog.isConfirmSelected());

    TEST_ASSERT_TRUE(dialog.handleInput('\n'));
    TEST_ASSERT_EQUAL_INT(1, s_confirms);
    TEST_ASSERT_EQUAL_INT(0, s_cancels);
    TEST_ASSERT_FALSE(dialog.isVisible());
}

// The Y shortcut confirms regardless of which option is highlighted.
void test_y_shortcut_confirms_from_default() {
    ConfirmDialog dialog = makeShownDialog();

    TEST_ASSERT_TRUE(dialog.handleInput('Y'));
    TEST_ASSERT_EQUAL_INT(1, s_confirms);
    TEST_ASSERT_EQUAL_INT(0, s_cancels);
    TEST_ASSERT_FALSE(dialog.isVisible());
}

// N, ESC (27), and backtick each cancel without ever confirming.
void test_n_esc_and_backtick_cancel() {
    ConfirmDialog dialog = makeShownDialog();

    TEST_ASSERT_TRUE(dialog.handleInput('n'));
    dialog.show();
    TEST_ASSERT_TRUE(dialog.handleInput((char)27));
    dialog.show();
    TEST_ASSERT_TRUE(dialog.handleInput('`'));

    TEST_ASSERT_EQUAL_INT(3, s_cancels);
    TEST_ASSERT_EQUAL_INT(0, s_confirms);
}

// Navigation keys move the selection but fire nothing and keep the dialog open.
void test_navigation_does_not_fire_a_callback() {
    ConfirmDialog dialog = makeShownDialog();

    TEST_ASSERT_TRUE(dialog.handleInput('.'));  // Confirm
    TEST_ASSERT_TRUE(dialog.handleInput(';'));  // back to Cancel
    TEST_ASSERT_FALSE(dialog.isConfirmSelected());
    TEST_ASSERT_EQUAL_INT(0, s_confirms);
    TEST_ASSERT_EQUAL_INT(0, s_cancels);
    TEST_ASSERT_TRUE(dialog.isVisible());
}

// show() resets the default to Cancel even after a prior Confirm selection.
void test_show_resets_selection_to_cancel() {
    ConfirmDialog dialog = makeShownDialog();
    dialog.handleInput('.');  // Confirm selected
    dialog.hide();

    dialog.show();
    TEST_ASSERT_FALSE(dialog.isConfirmSelected());
}

// A hidden dialog consumes no input and fires no callback.
void test_hidden_dialog_ignores_input() {
    ConfirmDialog dialog;
    dialog.setOnConfirm([] { ++s_confirms; });
    dialog.setOnCancel([] { ++s_cancels; });

    TEST_ASSERT_FALSE(dialog.handleInput('\n'));
    TEST_ASSERT_FALSE(dialog.handleInput('Y'));
    TEST_ASSERT_EQUAL_INT(0, s_confirms);
    TEST_ASSERT_EQUAL_INT(0, s_cancels);
}

// Severity drives the "This cannot be undone." line that render() emits.
void test_severity_controls_undo_warning() {
    ConfirmDialog dialog;
    dialog.setSeverity(ConfirmDialog::Severity::Warning);
    TEST_ASSERT_FALSE(dialog.showsUndoWarning());

    dialog.setSeverity(ConfirmDialog::Severity::Destructive);
    TEST_ASSERT_TRUE(dialog.showsUndoWarning());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_cancel_so_enter_cancels);
    RUN_TEST(test_navigate_to_confirm_then_enter_confirms);
    RUN_TEST(test_y_shortcut_confirms_from_default);
    RUN_TEST(test_n_esc_and_backtick_cancel);
    RUN_TEST(test_navigation_does_not_fire_a_callback);
    RUN_TEST(test_show_resets_selection_to_cancel);
    RUN_TEST(test_hidden_dialog_ignores_input);
    RUN_TEST(test_severity_controls_undo_warning);
    return UNITY_END();
}
