/**
 * @file inactivity.h
 * @brief Pure policy for two-stage inactivity screen sleep (ADR-0116).
 *
 * The stage decision is kept dependency-free so it is unit-testable on the host; the
 * backlight and input wiring that act on it live in SystemManager and main.cpp.
 */

#pragma once

#include <cstdint>

namespace adversary {

/// screenDimAfterSec sentinel: 0 disables inactivity screen sleep entirely (the default).
constexpr uint16_t SCREEN_SLEEP_DISABLED = 0;

/// Backlight percentage for the readable intermediate (stage 1) dim. Chosen in the
/// 10-20% band the operator asked for: low enough to save most of the backlight draw,
/// high enough that live-status screens stay legible without waking (ADR-0116).
constexpr uint8_t SCREEN_DIM_BRIGHTNESS_PCT = 15;

/// Inactivity stages: full brightness, readable dim, or backlight off (ADR-0116).
enum class ScreenSleepStage : uint8_t { ACTIVE, DIMMED, OFF };

/**
 * @brief Decide the screen stage from how long the device has been idle.
 *
 * dimAfterSec is the master switch (0 => always ACTIVE, the default). When set, the
 * screen reaches DIMMED after dimAfterSec and, if offAfterSec is nonzero, OFF after
 * offAfterSec (0 => dim-only, never fully off). The elapsed subtraction is unsigned so a
 * millis() wraparound stays correct.
 *
 * This never sleeps the CPU: acting on DIMMED/OFF means lowering the backlight while
 * loop() keeps running, so an active attack/scan/wardriving session continues (ADR-0116,
 * carrying forward ADR-0114's backlight-only invariant). Callers enforce
 * offAfterSec >= dimAfterSec; if a stored config violates that, OFF past its threshold
 * still wins, which is a safe degenerate (the screen simply skips the readable stage).
 */
constexpr ScreenSleepStage inactivityStage(uint32_t nowMs, uint32_t lastActivityMs,
                                           uint16_t dimAfterSec, uint16_t offAfterSec) {
    if (dimAfterSec == SCREEN_SLEEP_DISABLED) {
        return ScreenSleepStage::ACTIVE;
    }
    const uint32_t elapsedMs = nowMs - lastActivityMs;
    if (offAfterSec != 0 && elapsedMs >= static_cast<uint32_t>(offAfterSec) * 1000u) {
        return ScreenSleepStage::OFF;
    }
    if (elapsedMs >= static_cast<uint32_t>(dimAfterSec) * 1000u) {
        return ScreenSleepStage::DIMMED;
    }
    return ScreenSleepStage::ACTIVE;
}

} // namespace adversary
