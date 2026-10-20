/**
 * @file test_action_button.cpp
 * @brief Logic regression for the shared ActionButton (ADR-0087 / slice-0088).
 *
 * The only host-testable logic is the label framing: call sites pass a bare verb and the
 * component owns the ">> VERB <<" decoration. These cases pin the framing, the empty/null
 * edge, and safe truncation at the buffer bound. The render(Canvas&) template is device-only
 * (the SUCCESS band and the BG_PRIMARY contrast fix) and is covered by build + on-device
 * review, as ActionMenu::render and ConfirmDialog::render are.
 */

#include <unity.h>
#include <cstring>
#include "ui/components/action_button.h"

using adversary::ui::formatActionLabel;

void setUp() {}
void tearDown() {}

// A bare verb is framed into the house decoration.
void test_frames_bare_verb() {
    char out[adversary::ui::ACTION_LABEL_MAX];
    formatActionLabel(out, sizeof(out), "START AUDIT");
    TEST_ASSERT_EQUAL_STRING(">> START AUDIT <<", out);
}

// The longest migrated verb still fits the buffer untruncated.
void test_frames_longest_verb() {
    char out[adversary::ui::ACTION_LABEL_MAX];
    formatActionLabel(out, sizeof(out), "START AUTO HUNT");
    TEST_ASSERT_EQUAL_STRING(">> START AUTO HUNT <<", out);
}

// An empty verb frames to the bare frame rather than writing garbage.
void test_frames_empty_verb() {
    char out[adversary::ui::ACTION_LABEL_MAX];
    formatActionLabel(out, sizeof(out), "");
    TEST_ASSERT_EQUAL_STRING(">>  <<", out);
}

// A null verb is treated as empty, never dereferenced.
void test_null_verb_is_safe() {
    char out[adversary::ui::ACTION_LABEL_MAX];
    formatActionLabel(out, sizeof(out), nullptr);
    TEST_ASSERT_EQUAL_STRING(">>  <<", out);
}

// A short buffer truncates without overflow and stays null-terminated.
void test_truncates_without_overflow() {
    char out[8];
    out[sizeof(out) - 1] = '\xFF';  // canary: must be overwritten by the terminator
    formatActionLabel(out, sizeof(out), "START AUDIT");
    TEST_ASSERT_EQUAL_CHAR('\0', out[sizeof(out) - 1]);
    TEST_ASSERT_TRUE(std::strlen(out) < sizeof(out));
    TEST_ASSERT_EQUAL_STRING(">> STAR", out);
}

// A one-byte buffer receives only the terminator.
void test_one_byte_buffer_is_terminated() {
    char out[1] = {'\xFF'};
    formatActionLabel(out, sizeof(out), "START AUDIT");
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_frames_bare_verb);
    RUN_TEST(test_frames_longest_verb);
    RUN_TEST(test_frames_empty_verb);
    RUN_TEST(test_null_verb_is_safe);
    RUN_TEST(test_truncates_without_overflow);
    RUN_TEST(test_one_byte_buffer_is_terminated);
    return UNITY_END();
}
