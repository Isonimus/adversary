#pragma once

/**
 * @file ble_reconnect_guard.h
 * @brief Pure reconnect-storm detector for BadBLE HID (ADR-0134).
 */

#include <cstdint>

namespace adversary {
namespace utils {

// A reconnect storm is this many failed-auth disconnects inside one rolling window. A genuine
// fresh re-pair costs at most a drop or two; a stale-bond collision loops without end, so the
// threshold sits above the former and far below "forever".
constexpr uint8_t BLE_RECONNECT_STORM_THRESHOLD = 4;
constexpr uint32_t BLE_RECONNECT_STORM_WINDOW_MS = 5000;

/**
 * @brief Decides when BadBLE should stop auto-re-advertising after failed-auth disconnects.
 *
 * The device records each failed-auth (unencrypted) disconnect; once THRESHOLD of them fall in
 * one WINDOW_MS window, recordFailure() returns true and the caller stops advertising instead of
 * spinning. A gap longer than the window opens a fresh window (the churn has stopped), and a
 * successful encrypted connection resets the count. Pure and deterministic: the caller supplies
 * the clock, so this is unit-tested in the native build even though BadBLE itself is device-only.
 */
class ReconnectChurnGuard {
public:
    ReconnectChurnGuard(uint8_t threshold = BLE_RECONNECT_STORM_THRESHOLD,
                        uint32_t windowMs = BLE_RECONNECT_STORM_WINDOW_MS);

    // Record one failed-auth disconnect at nowMs. Returns true once the storm threshold is
    // reached within the current window (advertising should stop).
    bool recordFailure(uint32_t nowMs);

    // Clear the count, e.g. on a successful encrypted connection or a fresh BadBLE start.
    void reset();

    uint8_t failureCount() const { return count_; }

private:
    uint8_t threshold_;
    uint32_t windowMs_;
    uint8_t count_;
    uint32_t windowStartMs_;
};

} // namespace utils
} // namespace adversary
