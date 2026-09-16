/**
 * @file input_keymap.h
 * @brief Pure key-normalization for the input path (slice-0030).
 *
 * The routing char that handleInput() feeds to screens/menus is derived from raw platform
 * input by two pure mappings — Cardputer modifier+key state, and the M5Stick logical
 * InputAction. Those mappings are the only hardware-free, silent-regression-prone logic in the
 * input path (a swapped LEFT/RIGHT or a broken Del is invisible until someone presses it), so
 * they live here as pure functions and are unit-tested natively. The reading, the Fn+S hotkey,
 * and the screen/menu routing stay in main.cpp — they are single-caller orchestration bound to
 * hardware singletons, not logic worth relocating.
 */

#pragma once

#include "hal/input/input_manager.h"  // adversary::InputAction (canonical enum; DRY)

namespace adversary {

/**
 * @brief Derive the routing char from Cardputer modifier state and the pressed key.
 * @param enter       Enter modifier held this frame.
 * @param del         Del (backspace) modifier held this frame.
 * @param pressedKey  The printable character, or 0 when none.
 * @return '\n' for Enter, 0x08 for Del, otherwise @p pressedKey (0 = no key; caller ignores).
 *
 * Enter/Del take precedence over the printable key, matching the original inline chain.
 */
char normalizeCardputerKey(bool enter, bool del, char pressedKey);

/**
 * @brief Map an M5Stick logical InputAction to the char the router/menus consume.
 * @return ';' (UP/LEFT), '.' (DOWN/RIGHT), '\n' (SELECT), '`' (BACK); '\0' for NONE and any
 *         action with no char mapping (caller ignores '\0').
 */
char inputActionToKey(InputAction action);

}  // namespace adversary
