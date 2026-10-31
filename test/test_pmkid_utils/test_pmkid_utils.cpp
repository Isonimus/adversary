/**
 * @file test_pmkid_utils.cpp
 * @brief Unit tests for PMKID extraction from an EAPOL-Key (M1) payload.
 *
 * Regression guard for the two-byte offset bug that kept the PMKID counter at
 * zero for the entire life of the feature: Key Data was read from the Key MIC
 * field instead, so the KDE walk never matched. Each test here fails against the
 * old offsets (91/93) and passes against the correct ones (93/95).
 */

#include <unity.h>

#include "utils/pmkid_utils.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// Ground-truth 802.11 EAPOL-Key offsets, hardcoded here ON PURPOSE and NOT via
// the header's constants: this fixture encodes the real wire layout so the test
// fails if the function-under-test reads the wrong offset. (Using the header's
// constants to both build and parse would be tautological: the original offset
// bug would pass.) From the Key Descriptor byte (EAPOL header already skipped):
// KeyInfo(2)+KeyLen(2)+Replay(8)+Nonce(32)+IV(16)+RSC(8)+KeyID(8)+MIC(16) => Key
// Data Length at keyFrame[93..94], Key Data at keyFrame[95].
static constexpr uint16_t WIRE_EAPOL_HEADER = 4;
static constexpr uint16_t WIRE_KEY_DATA_LEN_OFF = 93;  // within the key frame
static constexpr uint16_t WIRE_KEY_DATA_OFF = 95;      // within the key frame

// Builds a byte-accurate EAPOL-Key M1 payload (from the EAPOL header) with the
// given Key Data appended. Fixed portion is 4 (EAPOL header) + 95 = 99 bytes.
static uint16_t buildM1(uint8_t* buf, const uint8_t* keyData, uint16_t keyDataLen) {
    const uint16_t fixed = WIRE_EAPOL_HEADER + WIRE_KEY_DATA_OFF;  // 99
    memset(buf, 0, fixed);
    buf[0] = 0x02;  // EAPOL version
    buf[1] = 0x03;  // EAPOL type: Key
    const uint16_t remaining = WIRE_KEY_DATA_OFF + keyDataLen;    // after header
    buf[2] = (remaining >> 8) & 0xFF;
    buf[3] = remaining & 0xFF;
    buf[4] = 0x02;  // Key Descriptor Type: RSN
    uint8_t* keyFrame = buf + WIRE_EAPOL_HEADER;
    keyFrame[WIRE_KEY_DATA_LEN_OFF] = (keyDataLen >> 8) & 0xFF;
    keyFrame[WIRE_KEY_DATA_LEN_OFF + 1] = keyDataLen & 0xFF;
    memcpy(keyFrame + WIRE_KEY_DATA_OFF, keyData, keyDataLen);
    return fixed + keyDataLen;
}

// A single PMKID KDE: DD 14 00-0F-AC 04 <16 bytes>.
static uint16_t makePmkidKde(uint8_t* kde, const uint8_t pmkid[16]) {
    kde[0] = PMKID_KDE_TAG;
    kde[1] = PMKID_KDE_MIN_LEN;  // 20 = OUI(3)+Type(1)+PMKID(16)
    kde[2] = PMKID_KDE_OUI_0;
    kde[3] = PMKID_KDE_OUI_1;
    kde[4] = PMKID_KDE_OUI_2;
    kde[5] = PMKID_KDE_TYPE;
    memcpy(kde + 6, pmkid, 16);
    return 22;
}

static const uint8_t kPmkid[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

void test_extracts_pmkid_from_m1(void) {
    uint8_t kde[22];
    uint16_t kdeLen = makePmkidKde(kde, kPmkid);
    uint8_t frame[256];
    uint16_t frameLen = buildM1(frame, kde, kdeLen);

    uint8_t out[16];
    TEST_ASSERT_TRUE(extractPmkidFromEapol(frame, frameLen, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kPmkid, out, 16);
}

void test_finds_pmkid_after_another_element(void) {
    // A benign RSN IE (tag 0x30, len 4) precedes the PMKID KDE: the walk must
    // skip it and still land on the PMKID.
    uint8_t keyData[40];
    keyData[0] = 0x30;  // RSN IE
    keyData[1] = 0x04;
    memset(keyData + 2, 0xAB, 4);
    uint16_t kdeLen = makePmkidKde(keyData + 6, kPmkid);
    uint16_t keyDataLen = 6 + kdeLen;

    uint8_t frame[256];
    uint16_t frameLen = buildM1(frame, keyData, keyDataLen);

    uint8_t out[16];
    TEST_ASSERT_TRUE(extractPmkidFromEapol(frame, frameLen, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kPmkid, out, 16);
}

void test_no_pmkid_when_kde_absent(void) {
    // Key Data holds only a non-PMKID vendor KDE (type 0x01, e.g. GTK): no PMKID.
    uint8_t keyData[24];
    keyData[0] = PMKID_KDE_TAG;
    keyData[1] = PMKID_KDE_MIN_LEN;
    keyData[2] = PMKID_KDE_OUI_0;
    keyData[3] = PMKID_KDE_OUI_1;
    keyData[4] = PMKID_KDE_OUI_2;
    keyData[5] = 0x01;  // not PMKID (type 4)
    memset(keyData + 6, 0x77, 16);

    uint8_t frame[256];
    uint16_t frameLen = buildM1(frame, keyData, 22);

    uint8_t out[16];
    TEST_ASSERT_FALSE(extractPmkidFromEapol(frame, frameLen, out));
}

void test_no_pmkid_when_key_data_empty(void) {
    // M1 with no Key Data at all (Key Data Length = 0): the common 4-way M1.
    uint8_t frame[256];
    uint16_t frameLen = buildM1(frame, nullptr, 0);

    uint8_t out[16];
    TEST_ASSERT_FALSE(extractPmkidFromEapol(frame, frameLen, out));
}

void test_rejects_truncated_frame(void) {
    // Shorter than the fixed EAPOL-Key header: must bail, not read out of bounds.
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    uint8_t out[16];
    TEST_ASSERT_FALSE(extractPmkidFromEapol(frame, 50, out));
}

void test_rejects_overlong_key_data_len(void) {
    // Key Data Length claims more bytes than the frame carries: must not walk
    // past the buffer.
    uint8_t kde[22];
    makePmkidKde(kde, kPmkid);
    uint8_t frame[256];
    uint16_t frameLen = buildM1(frame, kde, 22);
    // Corrupt the length field to claim 200 bytes of Key Data.
    uint8_t* keyFrame = frame + WIRE_EAPOL_HEADER;
    keyFrame[WIRE_KEY_DATA_LEN_OFF] = 0x00;
    keyFrame[WIRE_KEY_DATA_LEN_OFF + 1] = 0xC8;  // 200

    uint8_t out[16];
    TEST_ASSERT_FALSE(extractPmkidFromEapol(frame, frameLen, out));
}

void test_null_arguments_return_false(void) {
    uint8_t out[16];
    uint8_t frame[128] = {0};
    TEST_ASSERT_FALSE(extractPmkidFromEapol(nullptr, 128, out));
    TEST_ASSERT_FALSE(extractPmkidFromEapol(frame, 128, nullptr));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_extracts_pmkid_from_m1);
    RUN_TEST(test_finds_pmkid_after_another_element);
    RUN_TEST(test_no_pmkid_when_kde_absent);
    RUN_TEST(test_no_pmkid_when_key_data_empty);
    RUN_TEST(test_rejects_truncated_frame);
    RUN_TEST(test_rejects_overlong_key_data_len);
    RUN_TEST(test_null_arguments_return_false);
    return UNITY_END();
}
