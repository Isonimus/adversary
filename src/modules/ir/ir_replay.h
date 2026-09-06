/**
 * @file ir_replay.h
 * @brief IR code replay on a selected emitter (slice-0004). Firmware-only.
 *
 * Emits a stored IrSignal: a Parsed code via IRremoteESP8266's generic typed
 * send (clean, reconstructed carrier timing), a Raw code via sendRaw at its
 * stored carrier. The emitter is constructed for the operation only, so nothing
 * is held across screens. Built-in emitter works on both targets; the cap array
 * needs the cap. Native builds get a stub.
 */

#pragma once

#include "ir_signal.h"

namespace adversary {
namespace ir {

/**
 * @brief Replay @p sig on the emitter at GPIO @p txPin (see irTxPin()).
 * @return true if emitted; false on an invalid signal, an out-of-range carrier,
 *         an unsupported parsed protocol, or when replay is unavailable on this
 *         build. Fails loud rather than emitting noise.
 */
bool replayIrSignal(const IrSignal& sig, int txPin);

} // namespace ir
} // namespace adversary
