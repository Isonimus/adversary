#pragma once
#include <stddef.h>
#include <stdint.h>

namespace adversary {
namespace hid_report {

// Transport-agnostic HID report descriptors and report byte packers, shared by the BadBLE
// path (NimBLE GATT notify) and the planned BadUSB path (USB HID endpoint). Keeping the
// descriptors and the byte layout in one place is the single source of truth ADR-0150
// requires: the two transports differ only in how they ship the identical report bytes.
//
// All three classes use Report ID 0 (no report-ID prefix), matching the single-class-per-
// session model of ADR-0150, so a report is exactly its payload bytes.

// --- Keyboard: 8-byte boot-keyboard report [modifiers, reserved, key1..key6] ------------

constexpr size_t KEYBOARD_REPORT_SIZE = 8;

extern const uint8_t KEYBOARD_REPORT_MAP[];
extern const size_t KEYBOARD_REPORT_MAP_SIZE;

// Fills a single-key keyboard report: out[0]=modifiers, out[1]=0 (reserved), out[2]=keycode,
// out[3..7]=0. An all-zero report (keycode 0, modifiers 0) is the key-release report.
void buildKeyboardReport(uint8_t out[KEYBOARD_REPORT_SIZE], uint8_t modifiers, uint8_t keycode);

// --- Mouse: 4-byte report [buttons, dx, dy, wheel] --------------------------------------

constexpr size_t MOUSE_REPORT_SIZE = 4;

// Button bitmask for byte 0 of the mouse report (HID Button usage page, buttons 1..3).
constexpr uint8_t MOUSE_BUTTON_LEFT   = 0x01;
constexpr uint8_t MOUSE_BUTTON_RIGHT  = 0x02;
constexpr uint8_t MOUSE_BUTTON_MIDDLE = 0x04;

extern const uint8_t MOUSE_REPORT_MAP[];
extern const size_t MOUSE_REPORT_MAP_SIZE;

// Fills a mouse report. dx/dy/wheel are relative, signed (-127..127); they are stored as
// two's-complement bytes. buttons is a bitmask of the MOUSE_BUTTON_* constants.
void buildMouseReport(uint8_t out[MOUSE_REPORT_SIZE], int8_t dx, int8_t dy, uint8_t buttons,
                      int8_t wheel);

// --- Gamepad: 3-byte report [buttons, x, y] ---------------------------------------------

constexpr size_t GAMEPAD_REPORT_SIZE = 3;

// The gamepad button byte is a bitmask of buttons 1..8 (bit 0 = button 1). Named buttons
// are the caller's domain; the report core only owns the count and the byte layout.
constexpr uint8_t GAMEPAD_BUTTON_COUNT = 8;

extern const uint8_t GAMEPAD_REPORT_MAP[];
extern const size_t GAMEPAD_REPORT_MAP_SIZE;

// Fills a gamepad report. x/y are absolute axis positions, signed (-127..127), stored as
// two's-complement bytes. buttons is a bitmask of buttons 1..8.
void buildGamepadReport(uint8_t out[GAMEPAD_REPORT_SIZE], int8_t x, int8_t y, uint8_t buttons);

} // namespace hid_report
} // namespace adversary
