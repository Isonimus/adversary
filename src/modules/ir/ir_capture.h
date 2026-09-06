/**
 * @file ir_capture.h
 * @brief IR frame capture on the cap's receiver (slice-0004). Firmware-only.
 *
 * Binds IRremoteESP8266's IRrecv to CAP_IR_RX for one capture window, decoding
 * the frame when the protocol is recognised and falling back to a raw timing
 * list otherwise. The receiver is enabled only for the operation and disabled
 * before returning, so it never holds the peripheral across screens (the RMT
 * coexistence rule in slices/0004). The pure IrSignal model it fills is
 * native-tested; this binding is manual/on-device only.
 *
 * Cap-only: IR receive exists solely on the multi-radio cap (Cardputer). Native
 * and M5Stick builds get a stub that reports "no capture".
 */

#pragma once

#include <cstdint>

#include "ir_signal.h"

namespace adversary {
namespace ir {

/// Carrier stored for a raw-fallback code. The receiver demodulates the carrier
/// away, so it can't be measured; 38 kHz is the near-universal consumer-IR value.
constexpr uint32_t IR_DEFAULT_CARRIER_HZ = 38000;

/**
 * @brief Capture one IR frame within @p windowMs into @p out.
 *
 * Fills @p out as Parsed when IRremoteESP8266 recognises the protocol, else as a
 * Raw timing train at IR_DEFAULT_CARRIER_HZ. The receiver is released before
 * returning.
 * @return true if a frame was captured; false on timeout (no frame) or when no
 *         IR receiver is available — never a partial/garbage signal.
 */
bool captureIrSignal(uint32_t windowMs, IrSignal& out);

} // namespace ir
} // namespace adversary
