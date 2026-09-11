/**
 * @file ook_signal.h
 * @brief Sub-GHz pulse train and its on-SD serialisation (slice-0003 / 0019).
 *
 * A captured signal is a carrier frequency plus a run-length list of edge
 * durations (microseconds), starting from a known logic level. This unit is the
 * pure in-memory model and its binary (de)serialisation to a byte buffer; the SD
 * file I/O lives in the screen/driver layer so this stays hardware-free and
 * native-testable (test/test_ook_signal).
 *
 * The same edge-list model carries **FSK-demodulated** captures (slice-0019): the
 * CC1101's FSK demod drives GDO0 with the recovered NRZ data line, so the RMT
 * capture path times its edges exactly as it does for OOK. The one difference is
 * that FSK replay must *reconstruct the demodulator* (deviation, data rate, RX
 * bandwidth), so an FSK signal carries a modulation descriptor that OOK does not.
 *
 * On-SD format is a compact custom binary, NOT the Flipper text .sub format — the
 * .sub extension is just "sub-GHz". Two magics:
 *   "SUB1" (v1) — OOK/ASK: magic(4)+ver(1)+flags(1)+freq(4)+count(2), 12-byte header.
 *   "SUB2" (v2) — FSK: the SUB1 header plus a 13-byte modulation descriptor
 *                 {modFormat(1)+deviation(4)+dataRate(4)+rxBw(4)} before count.
 * The reader branches on magic, so existing SUB1 captures keep loading unchanged.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace adversary {
namespace rf {

/// Upper bound on edges in one stored signal. A raw remote frame is ~50-130
/// edges; a few repeats fit well under this. Bounds the file and the RMT buffer.
constexpr size_t OOK_MAX_PULSES = 2048;

/// Modulation a captured signal was taken under. OOK slices the carrier envelope
/// (no demod parameters); the FSK schemes need deviation/data-rate/RX-bandwidth to
/// re-tune the demod on replay, carried in the SUB2 descriptor. Values are the
/// on-SD encoding of the SUB2 modFormat byte, independent of the CC1101 register
/// bits (the HAL maps enum -> MOD_FORMAT).
enum class Modulation : uint8_t {
    Ook = 0,   ///< SUB1
    Fsk2 = 1,  ///< SUB2 — 2-FSK
    Gfsk = 2,  ///< SUB2 — GFSK (received on the 2-FSK demod path)
    Msk = 3,   ///< SUB2 — MSK
};

/// One captured sub-GHz signal: carrier + edge-duration run-length list, plus a
/// modulation descriptor that is meaningful only for the FSK schemes (the deviation
/// /data-rate/RX-bandwidth fields stay 0 for OOK).
struct OokSignal {
    uint32_t frequencyHz = 0;
    bool firstLevelHigh = false;              ///< logic level of durations[0]
    Modulation modulation = Modulation::Ook;
    uint32_t deviationHz = 0;                 ///< FSK only (0 for OOK)
    uint32_t dataRateBaud = 0;                ///< FSK only
    uint32_t rxBwHz = 0;                      ///< FSK only
    std::vector<uint16_t> durationsUs;        ///< alternating-level edge widths (us)
};

/// SUB1 (OOK) header: magic(4)+ver(1)+flags(1)+freq(4)+count(2).
constexpr size_t SUB1_HEADER_SIZE = 12;
/// SUB2 (FSK) header: SUB1 header + modFormat(1)+deviation(4)+dataRate(4)+rxBw(4).
constexpr size_t SUB2_HEADER_SIZE = 25;

/// Serialised size in bytes for @p pulseCount edges under @p mod (header + 2/edge).
constexpr size_t subGhzSignalSerializedSize(Modulation mod, size_t pulseCount) {
    return (mod == Modulation::Ook ? SUB1_HEADER_SIZE : SUB2_HEADER_SIZE)
           + 2 * pulseCount;
}

/// SUB1 (OOK) serialised size. Retained for the OOK callers and the largest a SUB1
/// file can be; SUB2 files are 13 bytes larger (see subGhzSignalSerializedSize).
constexpr size_t ookSignalSerializedSize(size_t pulseCount) {
    return subGhzSignalSerializedSize(Modulation::Ook, pulseCount);
}

/// The largest any valid signal file can be — the SUB2 header (the bigger of the
/// two) plus a full edge list. Read guards size the upper bound against this.
constexpr size_t subGhzSignalMaxSerializedSize() {
    return subGhzSignalSerializedSize(Modulation::Fsk2, OOK_MAX_PULSES);
}

/**
 * @brief Serialise @p sig into @p out (capacity @p cap bytes).
 * @return bytes written, or 0 if the signal is invalid (no edges, or more than
 *         OOK_MAX_PULSES) or @p out is too small. Zero is a hard failure, never
 *         a truncated write.
 */
size_t serializeOokSignal(const OokSignal& sig, uint8_t* out, size_t cap);

/**
 * @brief Parse @p in (@p len bytes) into @p out.
 * @return true on a well-formed buffer; false (leaving @p out untouched) on a
 *         bad magic/version, a length that disagrees with the edge count, or an
 *         out-of-range count. A malformed file fails loud rather than yielding a
 *         partial signal.
 */
bool deserializeOokSignal(const uint8_t* in, size_t len, OokSignal& out);

} // namespace rf
} // namespace adversary
