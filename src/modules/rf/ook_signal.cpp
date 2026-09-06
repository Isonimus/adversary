/**
 * @file ook_signal.cpp
 * @brief OOK signal (de)serialisation (slice-0003). Pure, native-testable.
 */

#include "ook_signal.h"

#include <cstring>

namespace adversary {
namespace rf {

namespace {

constexpr uint8_t MAGIC[4] = {'S', 'U', 'B', '1'};
constexpr uint8_t FORMAT_VERSION = 1;
constexpr uint8_t FLAG_FIRST_LEVEL_HIGH = 0x01;
constexpr size_t HEADER_SIZE = 12;

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
    const size_t need = ookSignalSerializedSize(count);
    if (cap < need) return 0;

    std::memcpy(out, MAGIC, sizeof(MAGIC));
    out[4] = FORMAT_VERSION;
    out[5] = sig.firstLevelHigh ? FLAG_FIRST_LEVEL_HIGH : 0;
    putU32(out + 6, sig.frequencyHz);
    putU16(out + 10, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        putU16(out + HEADER_SIZE + 2 * i, sig.durationsUs[i]);
    }
    return need;
}

bool deserializeOokSignal(const uint8_t* in, size_t len, OokSignal& out) {
    if (len < HEADER_SIZE) return false;
    if (std::memcmp(in, MAGIC, sizeof(MAGIC)) != 0) return false;
    if (in[4] != FORMAT_VERSION) return false;

    const uint16_t count = getU16(in + 10);
    if (count == 0 || count > OOK_MAX_PULSES) return false;
    if (len != ookSignalSerializedSize(count)) return false;

    out.frequencyHz = getU32(in + 6);
    out.firstLevelHigh = (in[5] & FLAG_FIRST_LEVEL_HIGH) != 0;
    out.durationsUs.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        out.durationsUs[i] = getU16(in + HEADER_SIZE + 2 * i);
    }
    return true;
}

} // namespace rf
} // namespace adversary
