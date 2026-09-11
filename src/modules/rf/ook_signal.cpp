/**
 * @file ook_signal.cpp
 * @brief OOK signal (de)serialisation (slice-0003). Pure, native-testable.
 */

#include "ook_signal.h"

#include <cstring>

namespace adversary {
namespace rf {

namespace {

constexpr uint8_t MAGIC_SUB1[4] = {'S', 'U', 'B', '1'};  // OOK/ASK
constexpr uint8_t MAGIC_SUB2[4] = {'S', 'U', 'B', '2'};  // FSK (+ descriptor)
constexpr uint8_t VERSION_SUB1 = 1;
constexpr uint8_t VERSION_SUB2 = 2;
constexpr uint8_t FLAG_FIRST_LEVEL_HIGH = 0x01;

/// Highest valid Modulation enum value, to reject an unknown SUB2 modFormat byte.
constexpr uint8_t MODULATION_MAX = static_cast<uint8_t>(Modulation::Msk);

void putU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

size_t serializeOokSignal(const OokSignal& sig, uint8_t* out, size_t cap) {
    const size_t count = sig.durationsUs.size();
    if (count == 0 || count > OOK_MAX_PULSES) return 0;
    const bool isFsk = sig.modulation != Modulation::Ook;
    const size_t need = subGhzSignalSerializedSize(sig.modulation, count);
    if (cap < need) return 0;

    // Shared SUB1/SUB2 preamble: magic + version + flags + frequency.
    std::memcpy(out, isFsk ? MAGIC_SUB2 : MAGIC_SUB1, 4);
    out[4] = isFsk ? VERSION_SUB2 : VERSION_SUB1;
    out[5] = sig.firstLevelHigh ? FLAG_FIRST_LEVEL_HIGH : 0;
    putU32(out + 6, sig.frequencyHz);

    // SUB2 inserts the modulation descriptor between the frequency and the count;
    // SUB1 places the count immediately after the frequency (byte-identical to v1).
    size_t headerSize = SUB1_HEADER_SIZE;
    if (isFsk) {
        out[10] = static_cast<uint8_t>(sig.modulation);
        putU32(out + 11, sig.deviationHz);
        putU32(out + 15, sig.dataRateBaud);
        putU32(out + 19, sig.rxBwHz);
        headerSize = SUB2_HEADER_SIZE;
    }
    putU16(out + headerSize - 2, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        putU16(out + headerSize + 2 * i, sig.durationsUs[i]);
    }
    return need;
}

bool deserializeOokSignal(const uint8_t* in, size_t len, OokSignal& out) {
    if (len < SUB1_HEADER_SIZE) return false;

    const bool isSub2 = std::memcmp(in, MAGIC_SUB2, 4) == 0;
    if (!isSub2 && std::memcmp(in, MAGIC_SUB1, 4) != 0) return false;
    if (in[4] != (isSub2 ? VERSION_SUB2 : VERSION_SUB1)) return false;

    Modulation modulation = Modulation::Ook;
    uint32_t deviationHz = 0, dataRateBaud = 0, rxBwHz = 0;
    size_t headerSize = SUB1_HEADER_SIZE;
    if (isSub2) {
        if (len < SUB2_HEADER_SIZE) return false;
        const uint8_t modByte = in[10];
        // A SUB2 file must name an FSK scheme; Ook (0) or an unknown value is a
        // malformed descriptor, not a signal to guess at (fail loud).
        if (modByte == static_cast<uint8_t>(Modulation::Ook) ||
            modByte > MODULATION_MAX) {
            return false;
        }
        modulation = static_cast<Modulation>(modByte);
        deviationHz = getU32(in + 11);
        dataRateBaud = getU32(in + 15);
        rxBwHz = getU32(in + 19);
        headerSize = SUB2_HEADER_SIZE;
    }

    const uint16_t count = getU16(in + headerSize - 2);
    if (count == 0 || count > OOK_MAX_PULSES) return false;
    if (len != subGhzSignalSerializedSize(modulation, count)) return false;

    out.frequencyHz = getU32(in + 6);
    out.firstLevelHigh = (in[5] & FLAG_FIRST_LEVEL_HIGH) != 0;
    out.modulation = modulation;
    out.deviationHz = deviationHz;
    out.dataRateBaud = dataRateBaud;
    out.rxBwHz = rxBwHz;
    out.durationsUs.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        out.durationsUs[i] = getU16(in + headerSize + 2 * i);
    }
    return true;
}

} // namespace rf
} // namespace adversary
