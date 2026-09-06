/**
 * @file ir_capture.cpp
 * @brief IR frame capture implementation (slice-0004). Cap-only (Cardputer).
 */

#include "ir_capture.h"

#include "../../config/pins.h"

#if defined(TARGET_CARDPUTER)
#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

namespace adversary {
namespace ir {

namespace {

// A remote frame settles within tens of ms; this idle gap marks its end.
constexpr uint8_t IR_END_OF_FRAME_MS = 15;
// Poll cadence while waiting; delay() yields so the watchdog stays fed.
constexpr uint32_t IR_POLL_MS = 5;

}  // namespace

bool captureIrSignal(uint32_t windowMs, IrSignal& out) {
    // Buffer sized to the model's timing cap so a long raw frame still fits.
    IRrecv receiver(static_cast<uint16_t>(pins::CAP_IR_RX), IR_MAX_TIMINGS,
                    IR_END_OF_FRAME_MS, false);
    receiver.enableIRIn();

    decode_results results;
    bool decoded = false;
    const uint32_t start = millis();
    while (millis() - start < windowMs) {
        if (receiver.decode(&results)) {
            decoded = true;
            break;
        }
        delay(IR_POLL_MS);
    }
    // NOTE: do NOT call receiver.disableIRIn() here. The IRrecv destructor calls
    // it at scope exit (RAII), and on ESP32-core-v3 disableIRIn() runs
    // timerEnd(), which frees the hardware timer but leaves the library's `timer`
    // pointer dangling — a second disableIRIn() then does timerWrite() on freed
    // memory and panics (LoadProhibited). One teardown, via the destructor.

    if (!decoded) return false;

    // A buffer overflow means the frame ran longer than IR_MAX_TIMINGS and was
    // truncated mid-capture: the stored fragment replays as a single unrecognised
    // blink the target ignores. Reject it (fail loud) rather than save a broken
    // code — this covers both the decoded and raw paths below.
    if (results.overflow) {
        Serial.println("[IR] Capture rejected: RX buffer overflow (frame too long)");
        return false;
    }

    // A recognised protocol stores as a clean Parsed code; UNKNOWN/UNUSED fall
    // back to the raw timing train so nothing is unreplayable.
    if (results.decode_type != decode_type_t::UNKNOWN &&
        results.decode_type != decode_type_t::UNUSED) {
        IrSignal sig;
        sig.encoding = IrEncoding::Parsed;
        sig.protocol = static_cast<uint32_t>(results.decode_type);
        sig.value = results.value;
        sig.bits = results.bits;
        if (!irSignalValid(sig)) return false;
        out = sig;
        return true;
    }

    const uint16_t length = getCorrectedRawLength(&results);
    // Reject an empty or over-length train instead of silently truncating it: a
    // clipped frame is an incomplete code. irSignalValid() enforces the same
    // 1..IR_MAX_TIMINGS bound, so assigning the full length keeps that gate honest.
    if (length == 0 || length > IR_MAX_TIMINGS) {
        Serial.printf("[IR] Capture rejected: raw length %u out of range\n", length);
        return false;
    }
    uint16_t* raw = resultToRawArray(&results);  // us, mark-first; heap-allocated
    if (!raw) return false;

    IrSignal sig;
    sig.encoding = IrEncoding::Raw;
    sig.carrierHz = IR_DEFAULT_CARRIER_HZ;
    sig.timingsUs.assign(raw, raw + length);
    delete[] raw;

    if (!irSignalValid(sig)) return false;
    out = sig;
    return true;
}

} // namespace ir
} // namespace adversary

#else  // !TARGET_CARDPUTER — no cap receiver on this build

namespace adversary {
namespace ir {

bool captureIrSignal(uint32_t, IrSignal&) { return false; }

} // namespace ir
} // namespace adversary

#endif  // TARGET_CARDPUTER
