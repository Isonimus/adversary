/**
 * @file return_target_slot.h
 * @brief One-shot "where to return on exit" breadcrumb (slice-0031).
 *
 * When an attack is launched from a list (Scanner/Sniffer → pick target → attack), the list is
 * torn down before the attack inits. This slot remembers the originating screen and the AppState
 * that was interrupted, so the attack's ESC returns there instead of dumping to the root menu.
 *
 * Deliberately NOT an object stack: the firmware keeps one screen resident at a time (No-PSRAM
 * invariant, CLAUDE.md), so we remember only where to go, not the parent instance. The parent is
 * rebuilt via its factory on return. Display-free / HAL-free, so it is unit-tested natively.
 */

#pragma once

#include "core/state_machine.h"           // AppState
#include "ui/screens/screen_interface.h"  // ScreenId

namespace adversary {

/// Where to return when the current screen exits, and the state to restore there.
struct ReturnTarget {
    ScreenId screen;
    AppState state;
};

/**
 * @brief One-shot return breadcrumb.
 *
 * The default {MENU, IDLE} encodes "no drill-down": a plain exit goes to the root menu. set()
 * arms a target; consume() returns it exactly once and then reverts to the default, so a stale
 * breadcrumb can never bounce the *next* normal exit back into a list.
 */
class ReturnTargetSlot {
public:
    void set(ScreenId screen, AppState state) { target_ = {screen, state}; }

    /// Return the armed target once (then revert to the default), or {MENU, IDLE} if unset.
    ReturnTarget consume() {
        ReturnTarget consumed = target_;
        target_ = NO_TARGET;
        return consumed;
    }

    void clear() { target_ = NO_TARGET; }

private:
    // "No drill-down" default. Named NO_TARGET, not DEFAULT: Arduino.h #defines DEFAULT as a macro.
    static constexpr ReturnTarget NO_TARGET{ScreenId::MENU, AppState::IDLE};
    ReturnTarget target_ = NO_TARGET;
};

} // namespace adversary
