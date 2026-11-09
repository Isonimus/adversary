#include "modules/badusb/hid_report.h"

namespace adversary {
namespace hid_report {

// --- Keyboard ---------------------------------------------------------------------------

// Boot-keyboard descriptor: 8-bit modifiers, a reserved byte, and a 6-byte key array.
// Verbatim from the original BadBLE inline descriptor so the on-wire keyboard behaviour is
// unchanged (ADR-0150): the device enumerates identically, it is only the *home* of these
// bytes that moved here to be shared.
const uint8_t KEYBOARD_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, // Usage Page (Generic Desktop), Usage (Keyboard), Collection (Application)
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, // Usage Page (Key Codes), Usage Min (224), Usage Max (231)
    0x15, 0x00, 0x25, 0x01,             // Logical Min (0), Logical Max (1)
    0x75, 0x01, 0x95, 0x08,             // Report Size (1), Report Count (8)
    0x81, 0x02,                         // Input (Data, Variable, Absolute) -> modifiers
    0x95, 0x01, 0x75, 0x08,             // Report Count (1), Report Size (8)
    0x81, 0x01,                         // Input (Constant) -> reserved byte
    0x95, 0x06, 0x75, 0x08,             // Report Count (6), Report Size (8)
    0x15, 0x00, 0x25, 0x65,             // Logical Min (0), Logical Max (101)
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, // Usage Page (Key Codes), Usage Min (0), Usage Max (101)
    0x81, 0x00,                         // Input (Data, Array) -> key array
    0xc0                                // End Collection
};
const size_t KEYBOARD_REPORT_MAP_SIZE = sizeof(KEYBOARD_REPORT_MAP);

void buildKeyboardReport(uint8_t out[KEYBOARD_REPORT_SIZE], uint8_t modifiers, uint8_t keycode) {
    out[0] = modifiers;
    out[1] = 0; // reserved byte, constant per the descriptor
    out[2] = keycode;
    out[3] = 0;
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
}

// --- Mouse ------------------------------------------------------------------------------

// 3 button bits + 5 padding bits (1 byte), then relative X, Y, Wheel (int8 each): a 4-byte
// report [buttons, dx, dy, wheel].
const uint8_t MOUSE_REPORT_MAP[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xa1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Buttons)
    0x19, 0x01,       //     Usage Min (1)
    0x29, 0x03,       //     Usage Max (3)
    0x15, 0x00,       //     Logical Min (0)
    0x25, 0x01,       //     Logical Max (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute) -> 3 button bits
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Constant) -> 5 padding bits
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Min (-127)
    0x25, 0x7f,       //     Logical Max (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data, Variable, Relative) -> X, Y, Wheel
    0xc0,             //   End Collection
    0xc0              // End Collection
};
const size_t MOUSE_REPORT_MAP_SIZE = sizeof(MOUSE_REPORT_MAP);

void buildMouseReport(uint8_t out[MOUSE_REPORT_SIZE], int8_t dx, int8_t dy, uint8_t buttons,
                      int8_t wheel) {
    out[0] = buttons;
    out[1] = static_cast<uint8_t>(dx);
    out[2] = static_cast<uint8_t>(dy);
    out[3] = static_cast<uint8_t>(wheel);
}

// --- Gamepad ----------------------------------------------------------------------------

// 8 button bits (1 byte), then absolute X, Y axes (int8 each): a 3-byte report
// [buttons, x, y].
const uint8_t GAMEPAD_REPORT_MAP[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xa1, 0x01,       // Collection (Application)
    0x05, 0x09,       //   Usage Page (Buttons)
    0x19, 0x01,       //   Usage Min (1)
    0x29, 0x08,       //   Usage Max (8)
    0x15, 0x00,       //   Logical Min (0)
    0x25, 0x01,       //   Logical Max (1)
    0x95, 0x08,       //   Report Count (8)
    0x75, 0x01,       //   Report Size (1)
    0x81, 0x02,       //   Input (Data, Variable, Absolute) -> 8 buttons
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x15, 0x81,       //   Logical Min (-127)
    0x25, 0x7f,       //   Logical Max (127)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data, Variable, Absolute) -> X, Y
    0xc0              // End Collection
};
const size_t GAMEPAD_REPORT_MAP_SIZE = sizeof(GAMEPAD_REPORT_MAP);

void buildGamepadReport(uint8_t out[GAMEPAD_REPORT_SIZE], int8_t x, int8_t y, uint8_t buttons) {
    out[0] = buttons;
    out[1] = static_cast<uint8_t>(x);
    out[2] = static_cast<uint8_t>(y);
}

} // namespace hid_report
} // namespace adversary
