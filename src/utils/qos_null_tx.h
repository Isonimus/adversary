#pragma once

/**
 * @file qos_null_tx.h
 * @brief ESP32-side TX helper for a QoS Null-Data stimulation burst.
 *
 * Separated from qos_null_frame.h so the pure builder header stays free of
 * any radio headers and the native test can include it without pulling in
 * esp_wifi.h (slice-0160, mirroring the slice-0155 BTM split).
 */

#include <cstddef>
#include <cstdint>

#include "utils/qos_null_frame.h"

namespace adversary::utils::qos_null_tx {

/// ADR-0159 default: 20 frames per burst, 10 pairs of PWR_MGT=1/0.
constexpr size_t   kDefaultBurstCount    = 20;
/// ADR-0159 default: 10 ms (10000 us) inter-frame spacing, so one burst is ~200 ms.
constexpr uint32_t kDefaultInterFrameUs  = 10000;

/**
 * @brief Transmit @p count QoS Null-Data frames client->AP, alternating PWR_MGT on parity.
 *
 * Each iteration builds a fresh 26-byte frame with:
 *   - TA (A2) = @p ta (spoofed client MAC)
 *   - RA (A1) = @p bssid, A3 = @p bssid
 *   - PWR_MGT bit = (i & 1 == 0) ? 1 : 0  (burst starts with PwrMgt=1 so the AP
 *     buffers immediately, then flushes on the following PwrMgt=0)
 *   - sequence number incremented monotonically via a static counter
 *
 * Between frames the caller task sleeps @p inter_frame_us microseconds.
 *
 * On a native (non-ESP32) build this is a no-op and returns true so the host
 * test runner stays radio-free. The caller is responsible for parking the
 * radio on the target channel before the call.
 *
 * @return true if every frame transmitted successfully; false on any TX error
 *         or parameter failure (@p ta or @p bssid null, @p count == 0).
 */
bool send_burst(const uint8_t ta[6],
                const uint8_t bssid[6],
                size_t count = kDefaultBurstCount,
                uint32_t inter_frame_us = kDefaultInterFrameUs);

}  // namespace adversary::utils::qos_null_tx
