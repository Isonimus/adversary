/**
 * @file test_wifi_scanner_pmf.cpp
 * @brief Unit tests for the PMF-bit merge step inside WiFiScanner.
 *
 * Covers only the merge helper, not the beacon-sniff promiscuous pass (which
 * needs a radio and lives in processResults). Takes three fixture NetworkInfo
 * rows, drives parseRsnPmfBits against three fixture RSN IE buffers to get the
 * same bits the sniff callback would, and asserts that each pair lands on the
 * right row by BSSID.
 *
 * Regression guard: changing the memcmp to a pointer compare drops every match.
 * Weakening the "unknown BSSID is a no-op" rule would corrupt a neighbour row.
 */

#include <unity.h>

#include <stdint.h>
#include <string.h>
#include <vector>

#include "modules/wifi/wifi_scanner.h"
#include "utils/rsn_ie_parser.h"

using adversary::NetworkInfo;
using adversary::WiFiScanner;
using adversary::WiFiSecurity;
using adversary::utils::parseRsnPmfBits;

void setUp() {}
void tearDown() {}

static const uint8_t kBssidLegacy[6] = {0xAA, 0xAA, 0xAA, 0x00, 0x00, 0x01};
static const uint8_t kBssidMfpc[6]   = {0xBB, 0xBB, 0xBB, 0x00, 0x00, 0x02};
static const uint8_t kBssidMfpr[6]   = {0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x03};
static const uint8_t kBssidStranger[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};

// RSN IE fixtures matching the three BSSIDs above.
static const uint8_t kRsnLegacy[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x00, 0x00               // no PMF
};

static const uint8_t kRsnMfpcOnly[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x80, 0x00               // MFPC=1, MFPR=0
};

static const uint8_t kRsnMfpcAndMfpr[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0xC0, 0x00               // MFPC=1, MFPR=1
};

static NetworkInfo makeFixture(const uint8_t bssid[6]) {
    NetworkInfo net;
    net.ssid = "TestAP";
    memcpy(net.bssid, bssid, 6);
    net.rssi = -50;
    net.channel = 6;
    net.security = WiFiSecurity::WPA2_PSK;
    net.isHidden = false;
    net.lastSeen = 0;
    return net;  // pmfCapable/pmfRequired default to false
}

static std::vector<NetworkInfo> makeThreeRowFixture() {
    return {makeFixture(kBssidLegacy),
            makeFixture(kBssidMfpc),
            makeFixture(kBssidMfpr)};
}

// Drive the whole pipeline the sniff callback will: parseRsnPmfBits -> merge.
static void applyFromIe(std::vector<NetworkInfo>& list,
                        const uint8_t bssid[6],
                        const uint8_t* ieBuf, size_t ieLen) {
    bool mfpc = false, mfpr = false;
    TEST_ASSERT_TRUE(parseRsnPmfBits(ieBuf, ieLen, &mfpc, &mfpr));
    WiFiScanner::applyPmfBitsByBssid(list, bssid, mfpc, mfpr);
}

void test_defaults_are_false_before_merge(void) {
    auto list = makeThreeRowFixture();
    for (const auto& net : list) {
        TEST_ASSERT_FALSE(net.pmfCapable);
        TEST_ASSERT_FALSE(net.pmfRequired);
    }
}

void test_merge_lands_bits_on_right_bssid(void) {
    auto list = makeThreeRowFixture();

    applyFromIe(list, kBssidLegacy, kRsnLegacy, sizeof(kRsnLegacy));
    applyFromIe(list, kBssidMfpc,   kRsnMfpcOnly, sizeof(kRsnMfpcOnly));
    applyFromIe(list, kBssidMfpr,   kRsnMfpcAndMfpr, sizeof(kRsnMfpcAndMfpr));

    // Legacy row: no PMF.
    TEST_ASSERT_FALSE(list[0].pmfCapable);
    TEST_ASSERT_FALSE(list[0].pmfRequired);
    // MFPC-only row: capable but not required.
    TEST_ASSERT_TRUE(list[1].pmfCapable);
    TEST_ASSERT_FALSE(list[1].pmfRequired);
    // MFPC+MFPR row: both bits set.
    TEST_ASSERT_TRUE(list[2].pmfCapable);
    TEST_ASSERT_TRUE(list[2].pmfRequired);
}

void test_merge_returns_false_for_unknown_bssid(void) {
    auto list = makeThreeRowFixture();
    const bool matched = WiFiScanner::applyPmfBitsByBssid(list, kBssidStranger, true, true);
    TEST_ASSERT_FALSE(matched);
    // No row mutated.
    for (const auto& net : list) {
        TEST_ASSERT_FALSE(net.pmfCapable);
        TEST_ASSERT_FALSE(net.pmfRequired);
    }
}

void test_merge_only_touches_matching_row(void) {
    auto list = makeThreeRowFixture();
    const bool matched = WiFiScanner::applyPmfBitsByBssid(list, kBssidMfpr, true, true);
    TEST_ASSERT_TRUE(matched);
    // Only row 2 mutated.
    TEST_ASSERT_FALSE(list[0].pmfCapable);
    TEST_ASSERT_FALSE(list[0].pmfRequired);
    TEST_ASSERT_FALSE(list[1].pmfCapable);
    TEST_ASSERT_FALSE(list[1].pmfRequired);
    TEST_ASSERT_TRUE(list[2].pmfCapable);
    TEST_ASSERT_TRUE(list[2].pmfRequired);
}

void test_merge_on_empty_list_is_noop(void) {
    std::vector<NetworkInfo> empty;
    const bool matched = WiFiScanner::applyPmfBitsByBssid(empty, kBssidMfpr, true, true);
    TEST_ASSERT_FALSE(matched);
    TEST_ASSERT_EQUAL(0, empty.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_false_before_merge);
    RUN_TEST(test_merge_lands_bits_on_right_bssid);
    RUN_TEST(test_merge_returns_false_for_unknown_bssid);
    RUN_TEST(test_merge_only_touches_matching_row);
    RUN_TEST(test_merge_on_empty_list_is_noop);
    return UNITY_END();
}
