/**
 * @file rolling_status.h
 * @brief Autohunt rolling-status buffer + priority-with-dwell overlap policy.
 *
 * ADR-0164, slice-0165. A one-string display-free buffer that holds the most
 * recent engine event for inline rendering after the "Xs remaining" countdown.
 * Policy: higher priority preempts; same-or-lower waits for dwell expiry;
 * mid-dwell same-or-lower messages are dropped (no queue).
 *
 * Clock is passed in to `emit`/`visible` so the policy stays pure and the
 * native test can drive it without millis(). Device code passes `millis()`.
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace adversary::ui {

constexpr uint8_t STATUS_PRIO_IDLE = 1;  // Waiting for EAPOL M3...
constexpr uint8_t STATUS_PRIO_LOW  = 2;  // Client seen, QoS-Null, deauth burst
constexpr uint8_t STATUS_PRIO_MED  = 3;  // PMKID solicit, BTM steer, EAPOL M2-M4
constexpr uint8_t STATUS_PRIO_HIGH = 4;  // PMKID captured, Handshake captured

constexpr uint32_t STATUS_DWELL_LOW_MS  = 2000;
constexpr uint32_t STATUS_DWELL_HIGH_MS = 4000;

constexpr size_t STATUS_MSG_CAP = 48;

struct RollingStatus {
    char msg[STATUS_MSG_CAP] = {0};
    uint8_t prio = 0;
    uint32_t expiryMs = 0;

    void reset() {
        msg[0] = '\0';
        prio = 0;
        expiryMs = 0;
    }

    // Apply the ADR-0164 overlap policy. Returns true if the new message landed.
    bool emit(const char* s, uint8_t priority, uint32_t now) {
        if (!s) return false;
        const bool dwellExpired = (static_cast<int32_t>(now - expiryMs) >= 0);
        if (priority > prio || dwellExpired) {
            strncpy(msg, s, STATUS_MSG_CAP - 1);
            msg[STATUS_MSG_CAP - 1] = '\0';
            prio = priority;
            const uint32_t dwellMs =
                (priority >= STATUS_PRIO_HIGH) ? STATUS_DWELL_HIGH_MS : STATUS_DWELL_LOW_MS;
            expiryMs = now + dwellMs;
            return true;
        }
        return false;
    }

    bool visible(uint32_t now) const {
        return msg[0] != '\0' && static_cast<int32_t>(now - expiryMs) < 0;
    }

    const char* text() const { return msg; }
};

}  // namespace adversary::ui
