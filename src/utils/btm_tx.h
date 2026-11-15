#pragma once

/**
 * @file btm_tx.h
 * @brief ESP32-side TX helper for a one-shot BTM Request injection.
 *
 * Separated from btm_frame.h so the pure builder header stays free of any
 * radio headers and the native test can include it without pulling in
 * esp_wifi.h (slice-0155).
 */

#include "utils/btm_frame.h"

namespace adversary::utils {

/**
 * @brief Build a BTM Request from @p p and transmit it as a single frame.
 *
 * On a native (non-ESP32) build this returns false without touching anything
 * so the host test runner stays radio-free.
 *
 * @return true on a successful TX, false on any build error, parameter
 *         rejection, or ESP-IDF TX failure.
 */
bool sendBtmRequest(const BtmRequestParams& p);

}  // namespace adversary::utils
