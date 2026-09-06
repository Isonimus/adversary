/**
 * @file test_ir_signal.cpp
 * @brief Round-trip + fail-loud regression for the IR .ir codec (slice-0004).
 */

#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "modules/ir/ir_signal.h"

using adversary::ir::IrSignal;
using adversary::ir::IrEncoding;
using adversary::ir::IR_MAX_TIMINGS;
using adversary::ir::irSignalValid;
using adversary::ir::serializeIrSignal;
using adversary::ir::deserializeIrSignal;

void setUp(void) {}
void tearDown(void) {}

static IrSignal parsedSample() {
    IrSignal s;
    s.encoding = IrEncoding::Parsed;
    s.protocol = 3;            // e.g. NEC in IRremoteESP8266's enum
    s.value = 0xE0E040BFull;
    s.bits = 32;
    return s;
}

static IrSignal rawSample() {
    IrSignal s;
    s.encoding = IrEncoding::Raw;
    s.carrierHz = 38000;
    s.timingsUs = {9000, 4500, 560, 560, 560, 1690, 560};
    return s;
}

// --- Round-trip preserves every field ----------------------------------------

void test_roundtrip_parsed(void) {
    IrSignal in = parsedSample();
    std::string text;
    TEST_ASSERT_TRUE(serializeIrSignal(in, text));

    IrSignal out;
    TEST_ASSERT_TRUE(deserializeIrSignal(text.data(), text.size(), out));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(IrEncoding::Parsed),
                          static_cast<int>(out.encoding));
    TEST_ASSERT_EQUAL_UINT32(in.protocol, out.protocol);
    TEST_ASSERT_EQUAL_UINT64(in.value, out.value);
    TEST_ASSERT_EQUAL_UINT16(in.bits, out.bits);
}

void test_roundtrip_raw(void) {
    IrSignal in = rawSample();
    std::string text;
    TEST_ASSERT_TRUE(serializeIrSignal(in, text));

    IrSignal out;
    TEST_ASSERT_TRUE(deserializeIrSignal(text.data(), text.size(), out));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(IrEncoding::Raw),
                          static_cast<int>(out.encoding));
    TEST_ASSERT_EQUAL_UINT32(in.carrierHz, out.carrierHz);
    TEST_ASSERT_EQUAL_UINT32(in.timingsUs.size(), out.timingsUs.size());
    for (size_t i = 0; i < in.timingsUs.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT16(in.timingsUs[i], out.timingsUs[i]);
    }
}

// A high 64-bit value must survive the hex round-trip (not truncate to 32 bits).
void test_roundtrip_parsed_wide_value(void) {
    IrSignal in = parsedSample();
    in.value = 0x1122334455667788ull;
    in.bits = 56;
    std::string text;
    TEST_ASSERT_TRUE(serializeIrSignal(in, text));
    IrSignal out;
    TEST_ASSERT_TRUE(deserializeIrSignal(text.data(), text.size(), out));
    TEST_ASSERT_EQUAL_UINT64(in.value, out.value);
}

// --- serialize fails loud on an invalid signal --------------------------------

void test_serialize_rejects_parsed_unknown_protocol(void) {
    IrSignal s = parsedSample();
    s.protocol = 0;  // UNKNOWN never stores as Parsed
    std::string text;
    TEST_ASSERT_FALSE(serializeIrSignal(s, text));
}

void test_serialize_rejects_parsed_zero_bits(void) {
    IrSignal s = parsedSample();
    s.bits = 0;
    std::string text;
    TEST_ASSERT_FALSE(serializeIrSignal(s, text));
}

void test_serialize_rejects_raw_empty(void) {
    IrSignal s = rawSample();
    s.timingsUs.clear();
    std::string text;
    TEST_ASSERT_FALSE(serializeIrSignal(s, text));
}

void test_serialize_rejects_raw_zero_carrier(void) {
    IrSignal s = rawSample();
    s.carrierHz = 0;
    std::string text;
    TEST_ASSERT_FALSE(serializeIrSignal(s, text));
}

void test_serialize_rejects_raw_oversized(void) {
    IrSignal s = rawSample();
    s.timingsUs.assign(IR_MAX_TIMINGS + 1, 500);
    std::string text;
    TEST_ASSERT_FALSE(serializeIrSignal(s, text));
}

// --- deserialize fails loud on a malformed buffer -----------------------------

void test_deserialize_rejects_empty(void) {
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal("", 0, out));
}

void test_deserialize_rejects_bad_magic(void) {
    const char* text = "XX9\ntype: raw\ncarrier: 38000\ntimings: 560 560\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_missing_type(void) {
    const char* text = "IR1\nprotocol: 3\nvalue: E0E040BF\nbits: 32\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_bad_type(void) {
    const char* text = "IR1\ntype: nonsense\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_parsed_missing_bits(void) {
    const char* text = "IR1\ntype: parsed\nprotocol: 3\nvalue: E0E040BF\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_raw_missing_timings(void) {
    const char* text = "IR1\ntype: raw\ncarrier: 38000\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_malformed_number(void) {
    const char* text = "IR1\ntype: parsed\nprotocol: 3x\nvalue: E0E040BF\nbits: 32\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_junk_line(void) {
    const char* text = "IR1\ntype: raw\nthis line has no colon\ncarrier: 38000\ntimings: 560 560\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text, strlen(text), out));
}

void test_deserialize_rejects_oversized_timings(void) {
    std::string text = "IR1\ntype: raw\ncarrier: 38000\ntimings:";
    for (size_t i = 0; i < IR_MAX_TIMINGS + 1; ++i) text += " 500";
    text += "\n";
    IrSignal out;
    TEST_ASSERT_FALSE(deserializeIrSignal(text.data(), text.size(), out));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_parsed);
    RUN_TEST(test_roundtrip_raw);
    RUN_TEST(test_roundtrip_parsed_wide_value);
    RUN_TEST(test_serialize_rejects_parsed_unknown_protocol);
    RUN_TEST(test_serialize_rejects_parsed_zero_bits);
    RUN_TEST(test_serialize_rejects_raw_empty);
    RUN_TEST(test_serialize_rejects_raw_zero_carrier);
    RUN_TEST(test_serialize_rejects_raw_oversized);
    RUN_TEST(test_deserialize_rejects_empty);
    RUN_TEST(test_deserialize_rejects_bad_magic);
    RUN_TEST(test_deserialize_rejects_missing_type);
    RUN_TEST(test_deserialize_rejects_bad_type);
    RUN_TEST(test_deserialize_rejects_parsed_missing_bits);
    RUN_TEST(test_deserialize_rejects_raw_missing_timings);
    RUN_TEST(test_deserialize_rejects_malformed_number);
    RUN_TEST(test_deserialize_rejects_junk_line);
    RUN_TEST(test_deserialize_rejects_oversized_timings);
    return UNITY_END();
}
