/**
 * @file inactivity.h
 * @brief Pure policy for inactivity screen sleep (ADR-0114).
 *
 * The dim decision is kept dependency-free so it is unit-testable on the host; the
 * backlight and input wiring that act on it live in SystemManager and main.cpp.
 */

#pragma once

#include <cstdint>

namespace adversary {

/// screenTimeoutSec sentinel: 0 disables inactivity dimming entirely (the default).
constexpr uint16_t SCREEN_SLEEP_DISABLED = 0;

/**
 * @brief Decide whether the backlight should dim now for inactivity.
 *
 * Returns true only on the transition into the dimmed state: when sleep is enabled
 * (timeoutSec != 0), the screen is not already dimmed, and at least timeoutSec has elapsed
 * since the last input. The subtraction is unsigned so a millis() wraparound stays correct
 * (a 49-day uptime yields a single early wake at worst, never a stuck timer).
 *
 * This never decides to sleep the CPU: acting on a true result means setting the backlight
 * to zero while loop() keeps running, so an active attack/scan/wardriving session continues
 * in the dark (ADR-0114).
 */
constexpr bool inactivityShouldDim(uint32_t nowMs, uint32_t lastActivityMs,
                                   uint16_t timeoutSec, bool alreadyDimmed) {
    return timeoutSec != SCREEN_SLEEP_DISABLED && !alreadyDimmed &&
           (nowMs - lastActivityMs) >= static_cast<uint32_t>(timeoutSec) * 1000u;
}

} // namespace adversary
