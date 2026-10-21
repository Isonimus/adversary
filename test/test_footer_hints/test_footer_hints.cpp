/**
 * @file test_footer_hints.cpp
 * @brief Logic regression for the footer-hint key labels (ADR-0098 / slice-0099).
 *
 * The host-testable logic is footerKeyLabel: the control-key words, the Cardputer arrow
 * cluster's CP437 directional triangles (the first non-ASCII glyphs the UI prints
 * deliberately), and the uppercased single-char fallback. These cases pin the exact byte
 * strings so the triangle mapping can't silently regress to literal punctuation again. The
 * render(Canvas&) template is device-only and covered by the build + on-device review.
 */

#include <unity.h>
#include "ui/components/footer_hints.h"

using adversary::ui::footerKeyLabel;

void setUp() {}
void tearDown() {}

// The Cardputer up key `;` is the vertical-nav handle: up + down triangle pair (0x1E 0x1F).
void test_up_key_is_up_down_triangle_pair() {
    TEST_ASSERT_EQUAL_STRING("\x1E\x1F", footerKeyLabel(';'));
}

// The down key `.` is a single down triangle (0x1F).
void test_down_key_is_down_triangle() {
    TEST_ASSERT_EQUAL_STRING("\x1F", footerKeyLabel('.'));
}

// The left key `,` is the horizontal-adjust handle: left + right triangle pair (0x11 0x10).
void test_left_key_is_left_right_triangle_pair() {
    TEST_ASSERT_EQUAL_STRING("\x11\x10", footerKeyLabel(','));
}

// The right key `/` is a single right triangle (0x10).
void test_right_key_is_right_triangle() {
    TEST_ASSERT_EQUAL_STRING("\x10", footerKeyLabel('/'));
}

// A bidirectional handle's label is two glyphs wide; a one-way key is one glyph wide.
void test_triangle_label_widths() {
    TEST_ASSERT_EQUAL_size_t(2, strlen(footerKeyLabel(';')));
    TEST_ASSERT_EQUAL_size_t(2, strlen(footerKeyLabel(',')));
    TEST_ASSERT_EQUAL_size_t(1, strlen(footerKeyLabel('.')));
    TEST_ASSERT_EQUAL_size_t(1, strlen(footerKeyLabel('/')));
}

// Enter maps from both carriage forms to the same word.
void test_enter_keys() {
    TEST_ASSERT_EQUAL_STRING("Enter", footerKeyLabel('\n'));
    TEST_ASSERT_EQUAL_STRING("Enter", footerKeyLabel('\r'));
}

// The remaining named control keys.
void test_named_control_keys() {
    TEST_ASSERT_EQUAL_STRING("Esc", footerKeyLabel('`'));
    TEST_ASSERT_EQUAL_STRING("Space", footerKeyLabel(' '));
    TEST_ASSERT_EQUAL_STRING("Tab", footerKeyLabel('\t'));
}

// A lowercase letter is uppercased; an uppercase letter is unchanged.
void test_letter_is_uppercased() {
    TEST_ASSERT_EQUAL_STRING("A", footerKeyLabel('a'));
    TEST_ASSERT_EQUAL_STRING("Z", footerKeyLabel('z'));
    TEST_ASSERT_EQUAL_STRING("Z", footerKeyLabel('Z'));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_up_key_is_up_down_triangle_pair);
    RUN_TEST(test_down_key_is_down_triangle);
    RUN_TEST(test_left_key_is_left_right_triangle_pair);
    RUN_TEST(test_right_key_is_right_triangle);
    RUN_TEST(test_triangle_label_widths);
    RUN_TEST(test_enter_keys);
    RUN_TEST(test_named_control_keys);
    RUN_TEST(test_letter_is_uppercased);
    return UNITY_END();
}
