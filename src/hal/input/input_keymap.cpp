/**
 * @file input_keymap.cpp
 * @brief Pure key-normalization for the input path (slice-0030). See input_keymap.h.
 */

#include "hal/input/input_keymap.h"

namespace adversary {

char normalizeCardputerKey(bool enter, bool del, char pressedKey) {
    if (enter) {
        return '\n';
    }
    if (del) {
        return 0x08;  // Backspace
    }
    return pressedKey;  // 0 when no printable key; caller returns early on that
}

char inputActionToKey(InputAction action) {
    switch (action) {
        case InputAction::UP:     return ';';
        case InputAction::DOWN:   return '.';
        case InputAction::LEFT:   return ';';  // Also decrease
        case InputAction::RIGHT:  return '.';  // Also increase
        case InputAction::SELECT: return '\n';
        case InputAction::BACK:   return '`';
        default:                  return '\0';
    }
}

}  // namespace adversary
