/**
 * @file test_ook_signal.cpp
 * @brief Round-trip + fail-loud regression for the OOK .sub codec (slice-0003).
 */

#include <unity.h>
#include <cstring>
#include <vector>
#include "modules/rf/ook_signal.h"

using adversary::rf::Modulation;
using adversary::rf::OokSignal;
using adversary::rf::OOK_MAX_PULSES;
using adversary::rf::ookSignalSerializedSize;
using adversary::rf::subGhzSignalSerializedSize;
using adversary::rf::serializeOokSignal;
using adversary::rf::deserializeOokSignal;

void setUp(void) {}
void tearDown(void) {}

static OokSignal sampleSignal() {
    OokSignal s;
    s.frequencyHz = 433920000;
    s.firstLevelHigh = true;
    s.durationsUs = {350, 700, 350, 1050, 300, 300, 900};
    return s;
}

static OokSignal sampleFskSignal() {
    OokSignal s = sampleSignal();
    s.modulation = Modulation::Fsk2;
    s.deviationHz = 47607;
    s.dataRateBaud = 4800;
    s.rxBwHz = 203125;
    return s;
}

// --- Round-trip preserves every field ----------------------------------------

void test_roundtrip_preserves_signal(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(ookSignalSerializedSize(in.durationsUs.size()), n);

    OokSignal out;
    TEST_ASSERT_TRUE(deserializeOokSignal(buf, n, out));
    TEST_ASSERT_EQUAL_UINT32(in.frequencyHz, out.frequencyHz);
    TEST_ASSERT_TRUE(out.firstLevelHigh);
    TEST_ASSERT_EQUAL_UINT32(in.durationsUs.size(), out.durationsUs.size());
    for (size_t i = 0; i < in.durationsUs.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT16(in.durationsUs[i], out.durationsUs[i]);
    }
}

void test_roundtrip_first_level_low(void) {
    OokSignal in = sampleSignal();
    in.firstLevelHigh = false;
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    OokSignal out;
    TEST_ASSERT_TRUE(deserializeOokSignal(buf, n, out));
    TEST_ASSERT_FALSE(out.firstLevelHigh);
}

// --- serialize fails loud (returns 0), never truncates ------------------------

void test_serialize_rejects_empty(void) {
    OokSignal empty;  // no durations
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT32(0, serializeOokSignal(empty, buf, sizeof(buf)));
}

void test_serialize_rejects_oversized(void) {
    OokSignal big;
    big.durationsUs.assign(OOK_MAX_PULSES + 1, 500);
    std::vector<uint8_t> buf(ookSignalSerializedSize(big.durationsUs.size()));
    TEST_ASSERT_EQUAL_UINT32(0, serializeOokSignal(big, buf.data(), buf.size()));
}

void test_serialize_rejects_small_buffer(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[8];  // smaller than the 12-byte header
    TEST_ASSERT_EQUAL_UINT32(0, serializeOokSignal(in, buf, sizeof(buf)));
}

// --- deserialize fails loud on a malformed buffer -----------------------------

void test_deserialize_rejects_bad_magic(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    buf[0] = 'X';
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, n, out));
}

void test_deserialize_rejects_bad_version(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    buf[4] = 0x02;  // version byte
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, n, out));
}

void test_deserialize_rejects_length_mismatch(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    OokSignal out;
    // One byte short of what the edge count declares: fail loud, not partial.
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, n - 1, out));
}

void test_deserialize_rejects_zero_count(void) {
    // Hand-built header claiming zero edges.
    uint8_t buf[12] = {'S', 'U', 'B', '1', 0x01, 0x00, 0, 0, 0, 0, 0x00, 0x00};
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, sizeof(buf), out));
}

void test_deserialize_rejects_too_short(void) {
    uint8_t buf[4] = {'S', 'U', 'B', '1'};
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, sizeof(buf), out));
}

// --- SUB2 (FSK): descriptor round-trips, OOK stays SUB1 -----------------------

// An OOK signal serialises as SUB1 (magic byte 3 == '1'); the descriptor is absent.
void test_ook_writes_sub1_magic(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(subGhzSignalSerializedSize(Modulation::Ook,
                                                        in.durationsUs.size()), n);
    TEST_ASSERT_EQUAL_UINT8('1', buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[4]);  // version 1
}

// An FSK signal serialises as SUB2 and every descriptor field round-trips.
void test_fsk_roundtrip_preserves_descriptor(void) {
    OokSignal in = sampleFskSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(subGhzSignalSerializedSize(Modulation::Fsk2,
                                                        in.durationsUs.size()), n);
    TEST_ASSERT_EQUAL_UINT8('2', buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[4]);  // version 2

    OokSignal out;
    TEST_ASSERT_TRUE(deserializeOokSignal(buf, n, out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Modulation::Fsk2),
                            static_cast<uint8_t>(out.modulation));
    TEST_ASSERT_EQUAL_UINT32(in.deviationHz, out.deviationHz);
    TEST_ASSERT_EQUAL_UINT32(in.dataRateBaud, out.dataRateBaud);
    TEST_ASSERT_EQUAL_UINT32(in.rxBwHz, out.rxBwHz);
    // The edge list and carrier survive alongside the descriptor.
    TEST_ASSERT_EQUAL_UINT32(in.frequencyHz, out.frequencyHz);
    TEST_ASSERT_EQUAL_UINT32(in.durationsUs.size(), out.durationsUs.size());
    for (size_t i = 0; i < in.durationsUs.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT16(in.durationsUs[i], out.durationsUs[i]);
    }
}

// GFSK/MSK carry through the same SUB2 path.
void test_fsk_roundtrip_gfsk_and_msk(void) {
    for (Modulation mod : {Modulation::Gfsk, Modulation::Msk}) {
        OokSignal in = sampleFskSignal();
        in.modulation = mod;
        uint8_t buf[256];
        size_t n = serializeOokSignal(in, buf, sizeof(buf));
        OokSignal out;
        TEST_ASSERT_TRUE(deserializeOokSignal(buf, n, out));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(mod),
                                static_cast<uint8_t>(out.modulation));
    }
}

// A pre-existing SUB1 (OOK) file must still load, as OOK, with a zeroed descriptor
// — the back-compat guarantee for captures on the operator's SD.
void test_sub1_still_reads_as_ook(void) {
    OokSignal in = sampleSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    OokSignal out;
    TEST_ASSERT_TRUE(deserializeOokSignal(buf, n, out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Modulation::Ook),
                            static_cast<uint8_t>(out.modulation));
    TEST_ASSERT_EQUAL_UINT32(0, out.deviationHz);
    TEST_ASSERT_EQUAL_UINT32(0, out.dataRateBaud);
    TEST_ASSERT_EQUAL_UINT32(0, out.rxBwHz);
}

// A SUB2 file whose modFormat byte says OOK (0) is self-contradictory — reject it
// rather than silently loading an FSK-less "FSK" signal.
void test_sub2_rejects_ook_modformat(void) {
    OokSignal in = sampleFskSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    buf[10] = static_cast<uint8_t>(Modulation::Ook);  // modFormat byte
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, n, out));
}

// An unknown modFormat byte (beyond MSK) is a malformed descriptor.
void test_sub2_rejects_unknown_modformat(void) {
    OokSignal in = sampleFskSignal();
    uint8_t buf[256];
    size_t n = serializeOokSignal(in, buf, sizeof(buf));
    buf[10] = 0x7F;
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, n, out));
}

// A SUB2 magic with a body too short for the 25-byte header fails loud.
void test_sub2_rejects_truncated_header(void) {
    uint8_t buf[20] = {'S', 'U', 'B', '2', 0x02};
    OokSignal out;
    TEST_ASSERT_FALSE(deserializeOokSignal(buf, sizeof(buf), out));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_preserves_signal);
    RUN_TEST(test_roundtrip_first_level_low);
    RUN_TEST(test_serialize_rejects_empty);
    RUN_TEST(test_serialize_rejects_oversized);
    RUN_TEST(test_serialize_rejects_small_buffer);
    RUN_TEST(test_deserialize_rejects_bad_magic);
    RUN_TEST(test_deserialize_rejects_bad_version);
    RUN_TEST(test_deserialize_rejects_length_mismatch);
    RUN_TEST(test_deserialize_rejects_zero_count);
    RUN_TEST(test_deserialize_rejects_too_short);
    RUN_TEST(test_ook_writes_sub1_magic);
    RUN_TEST(test_fsk_roundtrip_preserves_descriptor);
    RUN_TEST(test_fsk_roundtrip_gfsk_and_msk);
    RUN_TEST(test_sub1_still_reads_as_ook);
    RUN_TEST(test_sub2_rejects_ook_modformat);
    RUN_TEST(test_sub2_rejects_unknown_modformat);
    RUN_TEST(test_sub2_rejects_truncated_header);
    return UNITY_END();
}
