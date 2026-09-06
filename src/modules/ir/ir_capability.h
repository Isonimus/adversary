/**
 * @file ir_capability.h
 * @brief IR receive/transmit capability model (slice-0004).
 *
 * A tiny capability resolved next to the pins it describes — deliberately NOT
 * folded into a (non-existent) HardwareManager, same speculative-generality
 * reasoning as slices 0002/0003. It migrates there if that ever ships.
 *
 * IR *receive* exists only on the multi-radio cap (CAP_IR_RX; the platform has no
 * built-in receiver), so an IR receiver has no feasible independent probe —
 * idle-high is indistinguishable from a floating pull-up — and presence is
 * deduced from the resolved cap. IR *transmit* is always available on the
 * built-in emitter, with an operator override to the cap's 3-emitter array.
 *
 * The pure pieces (capHasIrRx, irTxPin) are native-testable (test/test_ir_
 * capability); the firmware convenience hasIrRx() reads the resolved cap.
 */

#pragma once

#include "../../config/pins.h"
#include "../../hal/expansion/expansion_cap.h"

namespace adversary {
namespace ir {

/// Which emitter a replay drives. Persisted in WirelessSettings.
enum class IrTxSource : uint8_t {
    BuiltIn,   ///< the proven built-in IR LED (pins::IR_TX)
    CapArray,  ///< the cap's 3-emitter array (pins::CAP_IR_TX) — more range/angle
};

/// Pure: does this resolved cap provide an IR receiver? (native-testable)
inline bool capHasIrRx(hal::ExpansionCap cap) {
    return cap == hal::ExpansionCap::MultiRadio;
}

/// The GPIO an IR transmission drives for @p source. (native-testable)
inline int irTxPin(IrTxSource source) {
    return source == IrTxSource::CapArray ? pins::CAP_IR_TX : pins::IR_TX;
}

/// Firmware convenience: is an IR receiver available right now? Reads the cap
/// resolved by the last detectExpansionCap() (firmware-only accessor).
inline bool hasIrRx() { return capHasIrRx(hal::resolvedExpansionCap()); }

/// IR transmit is always available (the built-in emitter).
inline bool hasIrTx() { return true; }

} // namespace ir
} // namespace adversary
