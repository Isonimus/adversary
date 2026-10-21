/**
 * @file test_theme.cpp
 * @brief RGB332-survival regression for the Tron preset (slice-0102).
 *
 * The UI canvas is an 8-bit RGB332 sprite (red/green 3-bit, blue 2-bit), so a theme colour
 * that looks right as RGB888 can quantize to black or to the wrong cell on device. Cyberpunk
 * learned this the hard way (its background ramp is tuned to visible RGB332 cells). This test
 * pins the Tron palette to its intended RGB332 cells so a future edit that breaks a landing
 * fails the build instead of shipping a muddy or black colour.
 *
 * Assertions are on the RGB332 *bucket* (top 3/3/2 bits), not a reconstructed 8-bit value,
 * because the bucket is the cell the panel actually shows and is independent of how M5GFX
 * expands it back to 8 bits.
 */

#include <unity.h>
#include <cstdint>
#include "ui/theme_manager.h"

using namespace adversary;

// One RGB332 cell: the 3-bit red, 3-bit green, 2-bit blue indices the panel resolves to.
struct Rgb332Cell {
    uint8_t r3;
    uint8_t g3;
    uint8_t b2;
    bool operator==(const Rgb332Cell& o) const { return r3 == o.r3 && g3 == o.g3 && b2 == o.b2; }
};

// Mirror the stored-RGB565 -> RGB332 path the canvas takes: decode the 5/6/5 channels, then
// keep the top 3/3/2 bits the 8-bit sprite stores.
static Rgb332Cell toCell(uint16_t rgb565) {
    const uint8_t r5 = (rgb565 >> 11) & 0x1F;
    const uint8_t g6 = (rgb565 >> 5) & 0x3F;
    const uint8_t b5 = rgb565 & 0x1F;
    return Rgb332Cell{ static_cast<uint8_t>(r5 >> 2),
                       static_cast<uint8_t>(g6 >> 3),
                       static_cast<uint8_t>(b5 >> 3) };
}

static bool isBlackCell(const Rgb332Cell& c) { return c.r3 == 0 && c.g3 == 0 && c.b2 == 0; }

void setUp() {}
void tearDown() {}

// The accent is the theme's identity: it must land on the electric-cyan cell, not a muddier
// neighbour and not collapse toward white or black.
void test_accent_is_electric_cyan_cell() {
    const Rgb332Cell accent = toCell(presets::TRON.accent);
    TEST_ASSERT_EQUAL_UINT8(1, accent.r3);  // low red keeps it cyan, not white
    TEST_ASSERT_EQUAL_UINT8(6, accent.g3);  // near-max green
    TEST_ASSERT_EQUAL_UINT8(3, accent.b2);  // max blue
}

// No accent or text colour may quantize to black (that is the Cyberpunk-era failure: a colour
// that is invisible on the RGB332 canvas).
void test_no_foreground_collapses_to_black() {
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.accent)));
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.accentLight)));
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.accentDark)));
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.textPrimary)));
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.textSecondary)));
    TEST_ASSERT_FALSE(isBlackCell(toCell(presets::TRON.textDisabled)));
}

// The three dark backgrounds must land on distinct cells, or the panel/hover/selection layers
// are indistinguishable once quantized.
void test_backgrounds_are_distinct_cells() {
    const Rgb332Cell primary   = toCell(presets::TRON.bgPrimary);
    const Rgb332Cell secondary = toCell(presets::TRON.bgSecondary);
    const Rgb332Cell tertiary  = toCell(presets::TRON.bgTertiary);
    const Rgb332Cell selected  = toCell(presets::TRON.bgSelected);
    TEST_ASSERT_TRUE(isBlackCell(primary));            // the grid void is true black
    TEST_ASSERT_FALSE(secondary == primary);
    TEST_ASSERT_FALSE(tertiary == secondary);
    TEST_ASSERT_FALSE(selected == tertiary);
    TEST_ASSERT_FALSE(isBlackCell(selected));          // selection band must be visible
}

// Primary text is cyan-tinted white, not pure white: red must sit below green so the tint
// reads on device.
void test_primary_text_is_cyan_tinted() {
    const Rgb332Cell text = toCell(presets::TRON.textPrimary);
    TEST_ASSERT_TRUE(text.r3 < text.g3);
    TEST_ASSERT_EQUAL_UINT8(3, text.b2);
}

// The preset is wired into the name table and the count sentinel.
void test_preset_is_registered() {
    TEST_ASSERT_EQUAL_STRING("Tron", getThemeName(ThemePreset::TRON));
    TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(ThemePreset::PRESET_COUNT));
    TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(ThemePreset::TRON));  // indices 0..5 stay stable
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_accent_is_electric_cyan_cell);
    RUN_TEST(test_no_foreground_collapses_to_black);
    RUN_TEST(test_backgrounds_are_distinct_cells);
    RUN_TEST(test_primary_text_is_cyan_tinted);
    RUN_TEST(test_preset_is_registered);
    return UNITY_END();
}
