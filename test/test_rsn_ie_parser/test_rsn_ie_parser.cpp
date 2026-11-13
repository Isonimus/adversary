/**
 * @file test_rsn_ie_parser.cpp
 * @brief Unit tests for the RSN IE PMF-bit walker.
 *
 * Covers the four well-formed shapes we care about (WPA2 no PMF, WPA2 MFPC-only,
 * WPA2 MFPC+MFPR, WPA3-SAE MFPR), a wrong IE id, and truncation at every field
 * boundary. The failure cases assert the output booleans are left untouched: a
 * caller that pre-initialises them to `false` (the scanner's default) must
 * observe exactly that after a false return, with no partial write.
 *
 * Regression guards: zeroing the RSN Capabilities read in the parser fails
 * cases b/c/d; dropping any of the bounds checks drops the matching truncation
 * case.
 */

#include <unity.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "utils/rsn_ie_parser.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

// RSN IE skeleton: Tag 0x30, Len, then Value. Fixed value fields are the
// 2-byte version (0x01 0x00), 4-byte group cipher (CCMP), pairwise count +
// suites, AKM count + suites, and 2-byte capabilities. These fixtures are
// hardcoded rather than assembled through the parser's own constants so a
// bug in the parser cannot also corrupt the fixture.
static const uint8_t kWpa2NoPmf[] = {
    0x30, 0x14,              // Tag, Len=20
    0x01, 0x00,              // Version
    0x00, 0x0F, 0xAC, 0x04,  // Group cipher: CCMP
    0x01, 0x00,              // Pairwise suite count = 1
    0x00, 0x0F, 0xAC, 0x04,  // Pairwise: CCMP
    0x01, 0x00,              // AKM suite count = 1
    0x00, 0x0F, 0xAC, 0x02,  // AKM: PSK
    0x00, 0x00               // RSN Capabilities (no PMF)
};

static const uint8_t kWpa2MfpcOnly[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x80, 0x00               // MFPC=1, MFPR=0
};

static const uint8_t kWpa2MfpcAndMfpr[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0xC0, 0x00               // MFPC=1, MFPR=1
};

static const uint8_t kWpa3SaeMfpr[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,  // Pairwise: CCMP
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x08,  // AKM: SAE
    0xC0, 0x00               // MFPC=1, MFPR=1 (WPA3 mandates both)
};

void test_wpa2_no_pmf(void) {
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_TRUE(parseRsnPmfBits(kWpa2NoPmf, sizeof(kWpa2NoPmf), &mfpc, &mfpr));
    TEST_ASSERT_FALSE(mfpc);
    TEST_ASSERT_FALSE(mfpr);
}

void test_wpa2_mfpc_only(void) {
    bool mfpc = false, mfpr = true;
    TEST_ASSERT_TRUE(parseRsnPmfBits(kWpa2MfpcOnly, sizeof(kWpa2MfpcOnly), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_FALSE(mfpr);
}

void test_wpa2_mfpc_and_mfpr(void) {
    bool mfpc = false, mfpr = false;
    TEST_ASSERT_TRUE(parseRsnPmfBits(kWpa2MfpcAndMfpr, sizeof(kWpa2MfpcAndMfpr), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_wpa3_sae_mfpr(void) {
    bool mfpc = false, mfpr = false;
    TEST_ASSERT_TRUE(parseRsnPmfBits(kWpa3SaeMfpr, sizeof(kWpa3SaeMfpr), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_wrong_ie_id_returns_false_and_leaves_outputs(void) {
    uint8_t bad[sizeof(kWpa2MfpcAndMfpr)];
    memcpy(bad, kWpa2MfpcAndMfpr, sizeof(bad));
    bad[0] = 0x31;  // not the RSN IE tag
    bool mfpc = true, mfpr = true;  // sentinel: must survive untouched
    TEST_ASSERT_FALSE(parseRsnPmfBits(bad, sizeof(bad), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_before_header(void) {
    const uint8_t buf[] = {0x30};  // just the tag
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_ielen_shorter_than_declared(void) {
    // declaredLen claims 20 but only 10 bytes of buffer supplied.
    const uint8_t buf[] = {
        0x30, 0x14,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_version(void) {
    // declaredLen=1: version itself does not fit.
    const uint8_t buf[] = {0x30, 0x01, 0x01};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_group_cipher(void) {
    // declaredLen=2: version fits, group cipher does not.
    const uint8_t buf[] = {0x30, 0x02, 0x01, 0x00};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_pairwise_count(void) {
    // declaredLen=6: version + group fit, pairwise count does not.
    const uint8_t buf[] = {
        0x30, 0x06,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_pairwise_suites(void) {
    // declaredLen=8: count=1 claims 4 bytes of suite, none present.
    const uint8_t buf[] = {
        0x30, 0x08,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_akm_count(void) {
    // declaredLen=12: through pairwise suite fits, AKM count does not.
    const uint8_t buf[] = {
        0x30, 0x0C,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_akm_suites(void) {
    // declaredLen=14: AKM count=1 but no suite bytes.
    const uint8_t buf[] = {
        0x30, 0x0E,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_truncation_at_capabilities(void) {
    // declaredLen=18: everything through AKM suites fits, capabilities does not.
    const uint8_t buf[] = {
        0x30, 0x12,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x02};
    bool mfpc = true, mfpr = true;
    TEST_ASSERT_FALSE(parseRsnPmfBits(buf, sizeof(buf), &mfpc, &mfpr));
    TEST_ASSERT_TRUE(mfpc);
    TEST_ASSERT_TRUE(mfpr);
}

void test_null_arguments_return_false(void) {
    bool mfpc = false, mfpr = false;
    TEST_ASSERT_FALSE(parseRsnPmfBits(nullptr, 22, &mfpc, &mfpr));
    TEST_ASSERT_FALSE(parseRsnPmfBits(kWpa2NoPmf, sizeof(kWpa2NoPmf), nullptr, &mfpr));
    TEST_ASSERT_FALSE(parseRsnPmfBits(kWpa2NoPmf, sizeof(kWpa2NoPmf), &mfpc, nullptr));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wpa2_no_pmf);
    RUN_TEST(test_wpa2_mfpc_only);
    RUN_TEST(test_wpa2_mfpc_and_mfpr);
    RUN_TEST(test_wpa3_sae_mfpr);
    RUN_TEST(test_wrong_ie_id_returns_false_and_leaves_outputs);
    RUN_TEST(test_truncation_before_header);
    RUN_TEST(test_truncation_ielen_shorter_than_declared);
    RUN_TEST(test_truncation_at_version);
    RUN_TEST(test_truncation_at_group_cipher);
    RUN_TEST(test_truncation_at_pairwise_count);
    RUN_TEST(test_truncation_at_pairwise_suites);
    RUN_TEST(test_truncation_at_akm_count);
    RUN_TEST(test_truncation_at_akm_suites);
    RUN_TEST(test_truncation_at_capabilities);
    RUN_TEST(test_null_arguments_return_false);
    return UNITY_END();
}
