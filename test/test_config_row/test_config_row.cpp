/**
 * @file test_config_row.cpp
 * @brief Logic regression for the shared ConfigRow (ADR-0096 / slice-0097).
 *
 * The only host-testable logic is the right-aligned value x: ConfigRow places a value so its
 * right edge sits CONFIG_ROW_VALUE_MARGIN from the canvas right edge. These cases pin that
 * invariant across widths and value lengths, and the null/empty edge (margin position, no
 * deref). The render(Canvas&) template is device-only (the label/value colours and cursor
 * placement) and is covered by build + on-device review, as ActionButton::render is.
 */

#include <unity.h>
#include <cstring>
#include "ui/components/config_row.h"

using adversary::ui::configRowValueX;
using adversary::ui::CONFIG_ROW_VALUE_MARGIN;

// A size-1 string cell is theme::FONT_CELL_WIDTH wide; mirror it here so the right-edge
// assertion is independent of the component's internal constant name.
static int16_t valueWidth(const char* v) {
    return static_cast<int16_t>(v ? std::strlen(v) : 0) * adversary::theme::FONT_CELL_WIDTH;
}

void setUp() {}
void tearDown() {}

// The value's right edge sits exactly CONFIG_ROW_VALUE_MARGIN from the canvas right edge.
void test_value_right_edge_is_margin_from_edge() {
    const int16_t width = 240;
    const char* value = "OFF";
    const int16_t x = configRowValueX(width, value);
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, x + valueWidth(value));
}

// The invariant holds on the narrow M5StickC width too.
void test_value_right_edge_on_narrow_width() {
    const int16_t width = 135;
    const char* value = "ON";
    const int16_t x = configRowValueX(width, value);
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, x + valueWidth(value));
}

// A single-character value is still pinned to the right margin.
void test_single_char_value() {
    const int16_t width = 240;
    const char* value = "5";
    const int16_t x = configRowValueX(width, value);
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, x + valueWidth(value));
}

// A null value yields the bare margin position and never dereferences.
void test_null_value_is_safe() {
    const int16_t width = 240;
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, configRowValueX(width, nullptr));
}

// An empty value behaves like a zero-width value: the bare margin position.
void test_empty_value_is_margin() {
    const int16_t width = 240;
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, configRowValueX(width, ""));
}

// A long value shifts left by its full width while keeping the right edge at the margin.
void test_long_value_keeps_right_edge() {
    const int16_t width = 240;
    const char* value = "Universal Key Keyboard";
    const int16_t x = configRowValueX(width, value);
    TEST_ASSERT_EQUAL_INT16(width - CONFIG_ROW_VALUE_MARGIN, x + valueWidth(value));
    TEST_ASSERT_TRUE(x < configRowValueX(width, "ON"));  // longer value starts further left
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_value_right_edge_is_margin_from_edge);
    RUN_TEST(test_value_right_edge_on_narrow_width);
    RUN_TEST(test_single_char_value);
    RUN_TEST(test_null_value_is_safe);
    RUN_TEST(test_empty_value_is_margin);
    RUN_TEST(test_long_value_keeps_right_edge);
    return UNITY_END();
}
