/**
 * @file test_ook_signal.cpp
 * @brief Round-trip + fail-loud regression for the OOK .sub codec (slice-0003).
 */

#include <unity.h>
#include <cstring>
#include <vector>
#include "modules/rf/ook_signal.h"

using adversary::rf::OokSignal;
using adversary::rf::OOK_MAX_PULSES;
using adversary::rf::ookSignalSerializedSize;
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
    return UNITY_END();
}
