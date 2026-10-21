/**
 * @file test_footer_hints.cpp
 * @brief Logic regression for the footer-hint key rendering (ADR-0100 / slice-0101).
 *
 * Two host-testable pieces: footerKeyGlyphCells, which classifies how many 6px cells a key's
 * drawn affordance occupies (the arrow keys draw filled triangles as geometry, so they are no
 * longer text labels), and footerKeyLabel, which still maps the control keys to words and a
 * letter to its uppercase. getHintWidth depends on both agreeing, so these pin the contract.
 * The triangle drawing itself (drawTriGlyph) is a canvas template, device-only, covered by the
 * build + on-device review.
 */

#include <unity.h>
#include "ui/components/footer_hints.h"

using adversary::ui::footerKeyLabel;
using adversary::ui::footerKeyGlyphCells;

void setUp() {}
void tearDown() {}

// The bidirectional nav handles occupy two glyph cells (a triangle pair); the one-way keys
// occupy one. This is what reserves the hint width for the geometry drawn at render time.
void test_arrow_keys_are_glyph_cells() {
    TEST_ASSERT_EQUAL_INT(2, footerKeyGlyphCells(';'));  // up + down
    TEST_ASSERT_EQUAL_INT(2, footerKeyGlyphCells(','));  // left + right
    TEST_ASSERT_EQUAL_INT(1, footerKeyGlyphCells('.'));  // down
    TEST_ASSERT_EQUAL_INT(1, footerKeyGlyphCells('/'));  // right
}

// A text key draws through footerKeyLabel, not as a triangle, so it reports zero glyph cells.
void test_text_keys_are_zero_glyph_cells() {
    TEST_ASSERT_EQUAL_INT(0, footerKeyGlyphCells('\n'));
    TEST_ASSERT_EQUAL_INT(0, footerKeyGlyphCells('`'));
    TEST_ASSERT_EQUAL_INT(0, footerKeyGlyphCells(' '));
    TEST_ASSERT_EQUAL_INT(0, footerKeyGlyphCells('a'));
    TEST_ASSERT_EQUAL_INT(0, footerKeyGlyphCells('S'));
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
    RUN_TEST(test_arrow_keys_are_glyph_cells);
    RUN_TEST(test_text_keys_are_zero_glyph_cells);
    RUN_TEST(test_enter_keys);
    RUN_TEST(test_named_control_keys);
    RUN_TEST(test_letter_is_uppercased);
    return UNITY_END();
}
