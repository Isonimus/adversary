/**
 * @file test_qos_null_frame.cpp
 * @brief Byte-level unit tests for the pure QoS Null-Data builder and the
 *        autohunt QoS-Null burst-fire predicate (slice-0160).
 *
 * The builder is the only thing standing between "we know what we're
 * transmitting" and "we hope we're transmitting the right thing." These
 * tests pin every header byte (FC, Duration, A1, A2, A3, Sequence Control,
 * QoS Control) for the sunny path, and exhaustively cover the 16 predicate
 * cells so the DoD "fires in exactly one cell" invariant is proven here.
 */

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "utils/qos_null_frame.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// ---------- Fixtures ----------

namespace {

constexpr uint8_t kClientMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr uint8_t kBssid[6]     = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

}  // namespace

// ---------- Builder: length and parameter rejection ----------

void test_builder_returns_26_on_sunny_path(void) {
    uint8_t frame[64] = {0};
    const size_t written = build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_size_t(kQosNullFrameSize, written);
    TEST_ASSERT_EQUAL_size_t(26U, written);
}

void test_builder_rejects_null_out(void) {
    TEST_ASSERT_EQUAL_size_t(0U, build_qos_null_data(nullptr, 64, kClientMac, kBssid, false, 0));
}

void test_builder_rejects_undersized_buffer(void) {
    uint8_t frame[25] = {0};
    TEST_ASSERT_EQUAL_size_t(0U, build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0));
}

void test_builder_rejects_null_ta(void) {
    uint8_t frame[64] = {0};
    TEST_ASSERT_EQUAL_size_t(0U, build_qos_null_data(frame, sizeof(frame), nullptr, kBssid, false, 0));
}

void test_builder_rejects_null_bssid(void) {
    uint8_t frame[64] = {0};
    TEST_ASSERT_EQUAL_size_t(0U, build_qos_null_data(frame, sizeof(frame), kClientMac, nullptr, false, 0));
}

// ---------- Builder: Frame Control ----------

void test_fc_byte0_is_0xc8(void) {
    // version=0, type=Data(2)=0b10, subtype=QoS-Null(12)=0b1100
    //   -> [subtype:4][type:2][version:2] low-to-high = 1100 10 00 -> 0xC8.
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0xC8, frame[0]);
}

void test_fc_byte1_todsis_set_pwrmgt_off(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[1]);  // ToDS=1, every other flag 0.
}

void test_fc_byte1_pwrmgt_toggles_with_arg(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, true, 0);
    // ToDS(0x01) | PwrMgt(0x10) = 0x11.
    TEST_ASSERT_EQUAL_UINT8(0x11, frame[1]);
    TEST_ASSERT_TRUE((frame[1] & 0x10) != 0);
    TEST_ASSERT_TRUE((frame[1] & 0x01) != 0);
    TEST_ASSERT_FALSE((frame[1] & 0x02) != 0);  // FromDS stays clear.
}

// ---------- Builder: Duration ----------

void test_duration_is_zero(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[3]);
}

// ---------- Builder: Addressing triplet ----------

void test_a1_equals_bssid(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kBssid, &frame[4], 6);
}

void test_a2_equals_ta(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kClientMac, &frame[10], 6);
}

void test_a3_equals_bssid(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kBssid, &frame[16], 6);
}

// ---------- Builder: Sequence Control ----------

void test_sequence_control_encodes_seq_no_le_fragment_zero(void) {
    // seq_no = 0x0ABC, fragment = 0 -> seq_ctl = (0x0ABC << 4) = 0xABC0.
    // Little-endian: byte 22 = 0xC0, byte 23 = 0xAB.
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0x0ABC);
    TEST_ASSERT_EQUAL_UINT8(0xC0, frame[22]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, frame[23]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[22] & 0x0F);  // Fragment nibble is zero.
}

void test_sequence_control_zero(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[22]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[23]);
}

void test_sequence_control_masks_to_12_bits(void) {
    // 0xFFFF truncated to the sequence's 12 bits = 0xFFF; seq_ctl = 0xFFF << 4 = 0xFFF0.
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0xFFFF);
    TEST_ASSERT_EQUAL_UINT8(0xF0, frame[22]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[23]);
}

// ---------- Builder: QoS Control ----------

void test_qos_control_is_zero(void) {
    uint8_t frame[64] = {0};
    build_qos_null_data(frame, sizeof(frame), kClientMac, kBssid, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[24]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[25]);
}

// ---------- Predicate: all 16 cells ----------

static bool fire(bool en, bool already, bool elapsed, bool known) {
    return shouldFireQosNullBurst(en, already, elapsed, known);
}

void test_predicate_the_one_true_cell(void) {
    // enabled & not-yet-fired & deauth-elapsed & client-known.
    TEST_ASSERT_TRUE(fire(true, false, true, true));
}

void test_predicate_the_other_15_cells_are_false(void) {
    // Every other combination must be false. 16 cells total; the positive is above.
    TEST_ASSERT_FALSE(fire(false, false, false, false));
    TEST_ASSERT_FALSE(fire(false, false, false, true));
    TEST_ASSERT_FALSE(fire(false, false, true,  false));
    TEST_ASSERT_FALSE(fire(false, false, true,  true));
    TEST_ASSERT_FALSE(fire(false, true,  false, false));
    TEST_ASSERT_FALSE(fire(false, true,  false, true));
    TEST_ASSERT_FALSE(fire(false, true,  true,  false));
    TEST_ASSERT_FALSE(fire(false, true,  true,  true));
    TEST_ASSERT_FALSE(fire(true,  false, false, false));
    TEST_ASSERT_FALSE(fire(true,  false, false, true));
    TEST_ASSERT_FALSE(fire(true,  false, true,  false));
    TEST_ASSERT_FALSE(fire(true,  true,  false, false));
    TEST_ASSERT_FALSE(fire(true,  true,  false, true));
    TEST_ASSERT_FALSE(fire(true,  true,  true,  false));
    TEST_ASSERT_FALSE(fire(true,  true,  true,  true));
}

void test_predicate_default_off_wins_over_everything(void) {
    TEST_ASSERT_FALSE(fire(false, false, true, true));
}

void test_predicate_already_fired_latch_holds(void) {
    TEST_ASSERT_FALSE(fire(true, true, true, true));
}

void test_predicate_waits_for_deauth_sub_window(void) {
    TEST_ASSERT_FALSE(fire(true, false, false, true));
}

void test_predicate_skips_when_no_client_mac(void) {
    TEST_ASSERT_FALSE(fire(true, false, true, false));
}

// ---------- Main ----------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_builder_returns_26_on_sunny_path);
    RUN_TEST(test_builder_rejects_null_out);
    RUN_TEST(test_builder_rejects_undersized_buffer);
    RUN_TEST(test_builder_rejects_null_ta);
    RUN_TEST(test_builder_rejects_null_bssid);
    RUN_TEST(test_fc_byte0_is_0xc8);
    RUN_TEST(test_fc_byte1_todsis_set_pwrmgt_off);
    RUN_TEST(test_fc_byte1_pwrmgt_toggles_with_arg);
    RUN_TEST(test_duration_is_zero);
    RUN_TEST(test_a1_equals_bssid);
    RUN_TEST(test_a2_equals_ta);
    RUN_TEST(test_a3_equals_bssid);
    RUN_TEST(test_sequence_control_encodes_seq_no_le_fragment_zero);
    RUN_TEST(test_sequence_control_zero);
    RUN_TEST(test_sequence_control_masks_to_12_bits);
    RUN_TEST(test_qos_control_is_zero);
    RUN_TEST(test_predicate_the_one_true_cell);
    RUN_TEST(test_predicate_the_other_15_cells_are_false);
    RUN_TEST(test_predicate_default_off_wins_over_everything);
    RUN_TEST(test_predicate_already_fired_latch_holds);
    RUN_TEST(test_predicate_waits_for_deauth_sub_window);
    RUN_TEST(test_predicate_skips_when_no_client_mac);
    return UNITY_END();
}
