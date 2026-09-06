/**
 * @file ook_signal.h
 * @brief Raw OOK pulse train and its on-SD serialisation (slice-0003).
 *
 * A captured OOK signal is a carrier frequency plus a run-length list of edge
 * durations (microseconds), starting from a known logic level. This unit is the
 * pure in-memory model and its binary (de)serialisation to a byte buffer; the SD
 * file I/O lives in the screen/driver layer so this stays hardware-free and
 * native-testable (test/test_ook_signal).
 *
 * The format is a compact custom binary (magic "SUB1"), NOT the Flipper text .sub
 * format — the .sub extension here is just "sub-GHz", no cross-tool compatibility
 * is claimed.
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

/// One captured OOK signal: carrier + edge-duration run-length list.
struct OokSignal {
    uint32_t frequencyHz = 0;
    bool firstLevelHigh = false;         ///< logic level of durations[0]
    std::vector<uint16_t> durationsUs;   ///< alternating-level edge widths (us)
};

/// Serialised size in bytes for @p pulseCount edges (header + 2 bytes/edge).
constexpr size_t ookSignalSerializedSize(size_t pulseCount) {
    return 12 + 2 * pulseCount;  // magic(4)+ver(1)+flags(1)+freq(4)+count(2)
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
