/**
 * @file ir_replay.cpp
 * @brief IR replay implementation (slice-0004). Firmware-only.
 */

#include "ir_replay.h"

#if defined(ESP32)
#include <Arduino.h>
#include <IRsend.h>

namespace adversary {
namespace ir {

namespace {

// Consumer IR carriers span ~30-60 kHz; reject anything outside as malformed
// (IRsend's carrier argument is 16-bit, so a garbage value would also truncate).
constexpr uint32_t IR_CARRIER_MIN_HZ = 30000;
constexpr uint32_t IR_CARRIER_MAX_HZ = 60000;

}  // namespace

bool replayIrSignal(const IrSignal& sig, int txPin) {
    if (!irSignalValid(sig) || txPin < 0) return false;

    IRsend sender(static_cast<uint16_t>(txPin));
    sender.begin();

    if (sig.encoding == IrEncoding::Parsed) {
        // Generic typed send reconstructs clean carrier-modulated timing.
        return sender.send(static_cast<decode_type_t>(sig.protocol), sig.value,
                           sig.bits);
    }

    if (sig.carrierHz < IR_CARRIER_MIN_HZ || sig.carrierHz > IR_CARRIER_MAX_HZ) {
        return false;  // out-of-range carrier: fail loud, don't truncate
    }
    sender.sendRaw(sig.timingsUs.data(), static_cast<uint16_t>(sig.timingsUs.size()),
                   static_cast<uint16_t>(sig.carrierHz));
    return true;
}

} // namespace ir
} // namespace adversary

#else  // !ESP32 — no IR emitter on the native build

namespace adversary {
namespace ir {

bool replayIrSignal(const IrSignal&, int) { return false; }

} // namespace ir
} // namespace adversary

#endif  // ESP32
