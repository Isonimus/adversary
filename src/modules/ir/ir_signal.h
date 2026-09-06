/**
 * @file ir_signal.h
 * @brief In-memory IR code and its `.ir` (de)serialisation (slice-0004).
 *
 * A captured IR code is stored one of two ways:
 *   - **Parsed** — a protocol frame recognised by IRremoteESP8266 (NEC, RC5,
 *     Sony, …), kept as {protocol, value, bits} and replayed via the generic
 *     IRsend::send(). Clean, tiny, human-readable.
 *   - **Raw** — an unrecognised frame kept as a carrier plus an alternating
 *     mark/space duration list (microseconds, first entry is a mark), replayed
 *     via IRsend::sendRaw(). The universal fallback so nothing is unreplayable.
 *
 * Unlike slice-0003's OOK codec (raw-only, binary), IR decoding is
 * IRremoteESP8266's whole purpose and already linked, so decode wins where it
 * can and raw is the fallback — see slices/0004.
 *
 * This unit is the pure in-memory model and its text serialisation; SD file I/O
 * lives in the screen layer so this stays hardware-free and native-testable
 * (test/test_ir_signal). The `.ir` text format is human-readable and
 * Flipper-*inspired*, but NOT binary-compatible with Flipper `.ir` files — the
 * protocol numbering is IRremoteESP8266's enum, not Flipper's names.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adversary {
namespace ir {

/// Upper bound on raw timing entries in one stored code. A remote frame is
/// ~70-200 edges; this bounds the file, the parse buffer, and the RX buffer.
constexpr size_t IR_MAX_TIMINGS = 1024;

/// Which representation a stored IR code uses.
enum class IrEncoding : uint8_t {
    Parsed,  ///< decoded protocol frame ({protocol, value, bits})
    Raw,     ///< carrier + mark/space durations ({carrierHz, timingsUs})
};

/// One captured IR code.
struct IrSignal {
    IrEncoding encoding = IrEncoding::Raw;

    // Parsed: replayed via IRsend::send(protocol, value, bits). `protocol` is the
    // numeric value of IRremoteESP8266's decode_type_t; 0 (UNKNOWN) never stores
    // as Parsed (it falls back to Raw at capture time).
    uint32_t protocol = 0;
    uint64_t value = 0;
    uint16_t bits = 0;

    // Raw: alternating mark/space widths (us) starting with a mark, replayed via
    // IRsend::sendRaw at `carrierHz`.
    uint32_t carrierHz = 0;
    std::vector<uint16_t> timingsUs;
};

/**
 * @brief Is @p sig a well-formed, replayable code?
 *
 * Parsed needs a known protocol (non-zero) and a non-zero bit count; Raw needs a
 * carrier and 1..IR_MAX_TIMINGS durations. Serialisation and replay both gate on
 * this so a malformed signal fails loud instead of being emitted as noise.
 */
inline bool irSignalValid(const IrSignal& sig) {
    if (sig.encoding == IrEncoding::Parsed) {
        return sig.protocol != 0 && sig.bits > 0;
    }
    return sig.carrierHz != 0 && !sig.timingsUs.empty() &&
           sig.timingsUs.size() <= IR_MAX_TIMINGS;
}

/**
 * @brief Serialise @p sig into the `.ir` text form in @p out.
 * @return true on success; false (leaving @p out untouched) if the signal is
 *         invalid (irSignalValid() is false). Never emits a partial code.
 */
bool serializeIrSignal(const IrSignal& sig, std::string& out);

/**
 * @brief Parse @p data (@p len bytes of `.ir` text) into @p out.
 * @return true on a well-formed code; false (leaving @p out untouched) on a bad
 *         header/version, an unknown or missing type, a missing mandatory field,
 *         a malformed number, or an out-of-range timing count. A truncated or
 *         garbage file fails loud rather than yielding a partial signal.
 */
bool deserializeIrSignal(const char* data, size_t len, IrSignal& out);

} // namespace ir
} // namespace adversary
