/**
 * @file test_pmkid_solicit.cpp
 * @brief Byte-exact regression for the clientless-PMKID solicitation frame builders
 *        (ADR-0137 acceptance, ADR-0138 as-built).
 *
 * The one thing that must stay true is the wire layout: these are the exact frames that drew
 * an EAPOL M1 with a PMKID KDE from a real AP on-device on 2026-11-04. A field reordered, a
 * cipher/AKM suite swapped, or the SSID element mis-sized would silently stop soliciting M1
 * on hardware, where there is no cheap way to notice. The expected bytes below are written
 * out independently of the builder (from the 802.11 layout), so a builder regression fails
 * here instead of in the field. Test vectors are the proven-on-device AP/STA/SSID.
 */

#include <cstring>

#include <unity.h>

#include "utils/pmkid_solicit.h"

using adversary::utils::buildAuthRequest;
using adversary::utils::buildAssocRequest;
using adversary::utils::defaultSolicitStaMac;
using adversary::utils::SOLICIT_AUTH_FRAME_LEN;
using adversary::utils::SOLICIT_FRAME_BUF_LEN;

void setUp(void) {}
void tearDown(void) {}

namespace {

// The frames proven on-device 2026-11-04 (ADR-0138).
const uint8_t kBssid[6] = {0x14, 0xab, 0x02, 0x01, 0xa7, 0xd2};
const uint8_t kSta[6]   = {0x02, 0xad, 0x5a, 0x11, 0xed, 0x01};
const char kSsid[]      = "LKS_Invitados";  // 13 octets

} // namespace

void test_auth_request_exact_bytes(void) {
    const uint8_t expected[SOLICIT_AUTH_FRAME_LEN] = {
        0xB0, 0x00,                               // frame control: mgmt / authentication
        0x00, 0x00,                               // duration
        0x14, 0xab, 0x02, 0x01, 0xa7, 0xd2,       // addr1 = AP
        0x02, 0xad, 0x5a, 0x11, 0xed, 0x01,       // addr2 = spoofed STA
        0x14, 0xab, 0x02, 0x01, 0xa7, 0xd2,       // addr3 = BSSID
        0x00, 0x00,                               // sequence control
        0x00, 0x00,                               // auth algorithm = open system
        0x01, 0x00,                               // auth transaction sequence = 1
        0x00, 0x00                                // status code = 0
    };

    uint8_t buf[SOLICIT_FRAME_BUF_LEN];
    const uint16_t len = buildAuthRequest(buf, sizeof(buf), kBssid, kSta);

    TEST_ASSERT_EQUAL_UINT16(SOLICIT_AUTH_FRAME_LEN, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, SOLICIT_AUTH_FRAME_LEN);
}

void test_assoc_request_exact_bytes(void) {
    const uint8_t expected[] = {
        0x00, 0x00,                               // frame control: mgmt / assoc request
        0x00, 0x00,                               // duration
        0x14, 0xab, 0x02, 0x01, 0xa7, 0xd2,       // addr1 = AP
        0x02, 0xad, 0x5a, 0x11, 0xed, 0x01,       // addr2 = spoofed STA
        0x14, 0xab, 0x02, 0x01, 0xa7, 0xd2,       // addr3 = BSSID
        0x00, 0x00,                               // sequence control
        0x11, 0x00,                               // capability info: ESS + Privacy
        0x0a, 0x00,                               // listen interval
        0x00, 0x0d,                               // SSID element, len 13
        'L','K','S','_','I','n','v','i','t','a','d','o','s',
        0x01, 0x08,                               // supported rates
        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
        0x30, 0x14,                               // RSN IE, len 20
        0x01, 0x00,                               // version 1
        0x00, 0x0f, 0xac, 0x04,                   // group cipher CCMP
        0x01, 0x00,                               // pairwise count 1
        0x00, 0x0f, 0xac, 0x04,                   // pairwise CCMP
        0x01, 0x00,                               // AKM count 1
        0x00, 0x0f, 0xac, 0x02,                   // AKM PSK
        0x00, 0x00                                // RSN capabilities
    };

    uint8_t buf[SOLICIT_FRAME_BUF_LEN];
    const uint16_t len = buildAssocRequest(buf, sizeof(buf), kBssid, kSta, kSsid);

    TEST_ASSERT_EQUAL_UINT16(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

// SSID longer than the 802.11 cap is clamped to 32 octets, and the element length byte
// matches the clamp, so the frame stays well-formed.
void test_assoc_request_clamps_overlong_ssid(void) {
    char longSsid[64];
    memset(longSsid, 'A', sizeof(longSsid) - 1);
    longSsid[sizeof(longSsid) - 1] = '\0';

    uint8_t buf[SOLICIT_FRAME_BUF_LEN];
    const uint16_t len = buildAssocRequest(buf, sizeof(buf), kBssid, kSta, longSsid);

    // SSID element starts after header(24) + cap(2) + listen(2) = offset 28: tag then length.
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[28]);  // SSID IE tag
    TEST_ASSERT_EQUAL_UINT8(32, buf[29]);    // clamped length
    const uint16_t expectedLen = 24 + 2 + 2 + (2 + 32) + (2 + 8) + (2 + 20);
    TEST_ASSERT_EQUAL_UINT16(expectedLen, len);
}

void test_builders_reject_too_small_buffer(void) {
    uint8_t small[SOLICIT_AUTH_FRAME_LEN - 1];
    TEST_ASSERT_EQUAL_UINT16(0, buildAuthRequest(small, sizeof(small), kBssid, kSta));

    uint8_t tiny[40];  // enough for auth, not for the assoc request with this SSID
    TEST_ASSERT_EQUAL_UINT16(0, buildAssocRequest(tiny, sizeof(tiny), kBssid, kSta, kSsid));
}

void test_builders_reject_null_pointers(void) {
    uint8_t buf[SOLICIT_FRAME_BUF_LEN];
    TEST_ASSERT_EQUAL_UINT16(0, buildAuthRequest(nullptr, sizeof(buf), kBssid, kSta));
    TEST_ASSERT_EQUAL_UINT16(0, buildAuthRequest(buf, sizeof(buf), nullptr, kSta));
    TEST_ASSERT_EQUAL_UINT16(0, buildAuthRequest(buf, sizeof(buf), kBssid, nullptr));
    TEST_ASSERT_EQUAL_UINT16(0, buildAssocRequest(buf, sizeof(buf), kBssid, kSta, nullptr));
}

void test_default_sta_mac_is_locally_administered_unicast(void) {
    const uint8_t* mac = defaultSolicitStaMac();
    TEST_ASSERT_EQUAL_UINT8(0x02, mac[0] & 0x03);  // LA bit set, multicast bit clear
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_auth_request_exact_bytes);
    RUN_TEST(test_assoc_request_exact_bytes);
    RUN_TEST(test_assoc_request_clamps_overlong_ssid);
    RUN_TEST(test_builders_reject_too_small_buffer);
    RUN_TEST(test_builders_reject_null_pointers);
    RUN_TEST(test_default_sta_mac_is_locally_administered_unicast);
    return UNITY_END();
}
