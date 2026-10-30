/**
 * @file ble_reconnect_guard.cpp
 * @brief Reconnect-storm detector implementation (ADR-0134).
 */

#include "ble_reconnect_guard.h"

namespace adversary {
namespace utils {

ReconnectChurnGuard::ReconnectChurnGuard(uint8_t threshold, uint32_t windowMs)
    : threshold_(threshold), windowMs_(windowMs), count_(0), windowStartMs_(0) {}

bool ReconnectChurnGuard::recordFailure(uint32_t nowMs) {
    // Tumbling window: the first failure opens a window; a failure past its end opens a new one.
    // Unsigned subtraction wraps modulo 2^32, so this stays correct across a millis() rollover.
    if (count_ == 0 || (nowMs - windowStartMs_) > windowMs_) {
        windowStartMs_ = nowMs;
        count_ = 1;
    } else if (count_ < 0xFF) {
        // Saturate: a very long storm must never wrap the counter back under the threshold.
        count_ = count_ + 1;
    }
    return count_ >= threshold_;
}

void ReconnectChurnGuard::reset() {
    count_ = 0;
    windowStartMs_ = 0;
}

} // namespace utils
} // namespace adversary
