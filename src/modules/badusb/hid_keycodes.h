#pragma once
#include <stdint.h>

namespace adversary {
namespace hid_keycodes {

// Modifier bit masks (USB HID spec)
constexpr uint8_t MOD_NONE      = 0x00;
constexpr uint8_t MOD_LCTRL     = 0x01;
constexpr uint8_t MOD_LSHIFT    = 0x02;
constexpr uint8_t MOD_LALT      = 0x04;
constexpr uint8_t MOD_LGUI      = 0x08;  // Win/Cmd
constexpr uint8_t MOD_RALT      = 0x40;  // AltGr


struct HidEntry {
    uint8_t modifiers;
    uint8_t usage;
};

// ASCII (0-127) → {modifiers, HID usage} for US and ES layouts.
// Usage=0 means "no mapping" (non-printable or unsupported).
extern const HidEntry asciiToHid_US[128];
extern const HidEntry asciiToHid_ES[128];

// Resolve a DuckyScript key name (e.g. "ENTER", "F5", "a") → HID usage.
// Returns 0 for unrecognised names.
uint8_t keyNameToUsage(const char* name);

// Resolve a DuckyScript modifier name → modifier bit mask.
// Returns 0 for unrecognised names.
uint8_t modNameToBit(const char* name);

} // namespace hid_keycodes
} // namespace adversary
