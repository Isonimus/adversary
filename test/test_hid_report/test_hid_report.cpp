/**
 * @file test_hid_report.cpp
 * @brief Unit tests for the shared HID report descriptors and byte packers (ADR-0150).
 *
 * These assert the exact on-wire report bytes for each HID class, which is observable
 * behaviour: a host decodes the device purely from these bytes and the matching descriptor.
 */

#include <unity.h>

#include "modules/badusb/hid_report.h"

using namespace adversary::hid_report;

void setUp() {}
void tearDown() {}

// --- Keyboard ---------------------------------------------------------------------------

void test_keyboard_report_places_modifier_and_keycode(void) {
    uint8_t out[KEYBOARD_REPORT_SIZE];
    constexpr uint8_t MOD_LCTRL = 0x01; // left-ctrl modifier bit (USB HID)
    buildKeyboardReport(out, MOD_LCTRL, 0x04 /*'a'*/);
    TEST_ASSERT_EQUAL_UINT8(MOD_LCTRL, out[0]); // modifiers
    TEST_ASSERT_EQUAL_UINT8(0x00, out[1]); // reserved byte, always constant 0
    TEST_ASSERT_EQUAL_UINT8(0x04, out[2]); // keycode in the first key slot
    for (size_t i = 3; i < KEYBOARD_REPORT_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, out[i]);
    }
}

void test_keyboard_release_is_all_zero(void) {
    uint8_t out[KEYBOARD_REPORT_SIZE];
    buildKeyboardReport(out, 0, 0);
    for (size_t i = 0; i < KEYBOARD_REPORT_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, out[i]);
    }
}

// --- Mouse ------------------------------------------------------------------------------

void test_mouse_report_byte_order_and_positive_deltas(void) {
    uint8_t out[MOUSE_REPORT_SIZE];
    buildMouseReport(out, 10, 20, MOUSE_BUTTON_RIGHT, 1);
    TEST_ASSERT_EQUAL_UINT8(MOUSE_BUTTON_RIGHT, out[0]);
    TEST_ASSERT_EQUAL_UINT8(10, out[1]); // dx
    TEST_ASSERT_EQUAL_UINT8(20, out[2]); // dy
    TEST_ASSERT_EQUAL_UINT8(1, out[3]);  // wheel
}

void test_mouse_negative_deltas_are_twos_complement(void) {
    uint8_t out[MOUSE_REPORT_SIZE];
    buildMouseReport(out, -1, -128, 0, -2);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out[1]); // -1
    TEST_ASSERT_EQUAL_UINT8(0x80, out[2]); // -128
    TEST_ASSERT_EQUAL_UINT8(0xFE, out[3]); // -2
}

void test_mouse_button_bits_combine(void) {
    uint8_t out[MOUSE_REPORT_SIZE];
    buildMouseReport(out, 0, 0, MOUSE_BUTTON_LEFT | MOUSE_BUTTON_MIDDLE, 0);
    TEST_ASSERT_EQUAL_UINT8(0x05, out[0]); // 0x01 | 0x04
}

// --- Gamepad ----------------------------------------------------------------------------

void test_gamepad_report_byte_order_and_axes(void) {
    uint8_t out[GAMEPAD_REPORT_SIZE];
    buildGamepadReport(out, 127, -127, 0x81);
    TEST_ASSERT_EQUAL_UINT8(0x81, out[0]);  // buttons bitmask (buttons 1 and 8)
    TEST_ASSERT_EQUAL_UINT8(0x7F, out[1]);  // x = 127
    TEST_ASSERT_EQUAL_UINT8(0x81, out[2]);  // y = -127 two's complement
}

// --- Descriptor well-formedness ---------------------------------------------------------

void test_descriptors_are_usage_page_rooted_and_collection_closed(void) {
    // Every report map opens with a Usage Page item (0x05) and closes a top-level
    // Application Collection with End Collection (0xC0).
    TEST_ASSERT_TRUE(KEYBOARD_REPORT_MAP_SIZE > 0);
    TEST_ASSERT_EQUAL_UINT8(0x05, KEYBOARD_REPORT_MAP[0]);
    TEST_ASSERT_EQUAL_UINT8(0xc0, KEYBOARD_REPORT_MAP[KEYBOARD_REPORT_MAP_SIZE - 1]);

    TEST_ASSERT_TRUE(MOUSE_REPORT_MAP_SIZE > 0);
    TEST_ASSERT_EQUAL_UINT8(0x05, MOUSE_REPORT_MAP[0]);
    TEST_ASSERT_EQUAL_UINT8(0xc0, MOUSE_REPORT_MAP[MOUSE_REPORT_MAP_SIZE - 1]);

    TEST_ASSERT_TRUE(GAMEPAD_REPORT_MAP_SIZE > 0);
    TEST_ASSERT_EQUAL_UINT8(0x05, GAMEPAD_REPORT_MAP[0]);
    TEST_ASSERT_EQUAL_UINT8(0xc0, GAMEPAD_REPORT_MAP[GAMEPAD_REPORT_MAP_SIZE - 1]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_keyboard_report_places_modifier_and_keycode);
    RUN_TEST(test_keyboard_release_is_all_zero);
    RUN_TEST(test_mouse_report_byte_order_and_positive_deltas);
    RUN_TEST(test_mouse_negative_deltas_are_twos_complement);
    RUN_TEST(test_mouse_button_bits_combine);
    RUN_TEST(test_gamepad_report_byte_order_and_axes);
    RUN_TEST(test_descriptors_are_usage_page_rooted_and_collection_closed);
    return UNITY_END();
}
