/**
 * @file test_hc22000_format.cpp
 * @brief Regression for the hashcat 22000 line builders (hc22000_format.h).
 *
 * The one thing that must stay true is the TYPE discriminator, which cracking
 * tools key on: 01 = PMKID, 02 = EAPOL (4-way). These were shipped swapped, so
 * every .22000 was mislabelled (a 4-way MIC sat in the PMKID slot and never
 * cracked). These tests assert the correct tag and that the discriminator field
 * (field 3) carries the right value for each type. They fail against the old
 * swapped writer and pass after the fix.
 */

#include <cstring>
#include <string>

#include <unity.h>

#include "utils/hc22000_format.h"

using adversary::CapturedHandshake;
using adversary::CapturedPMKID;
namespace hc = adversary::hc22000;

void setUp(void) {}
void tearDown(void) {}

namespace {

std::string toHex(const uint8_t* d, size_t n) {
    static const char H[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += H[d[i] >> 4];
        s += H[d[i] & 0x0F];
    }
    return s;
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.compare(0, p.size(), p) == 0;
}
bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// A CapturedHandshake with a synthetic, well-formed M2 EAPOL frame.
CapturedHandshake makeFourWay() {
    CapturedHandshake hs;
    memset(&hs, 0, sizeof(hs));
    hs.hasMsg1 = true;
    hs.hasMsg2 = true;
    for (int i = 0; i < 6; ++i) hs.apBssid[i] = 0x10 + i;    // 101112131415
    for (int i = 0; i < 6; ++i) hs.clientMac[i] = 0x20 + i;  // 202122232425
    for (int i = 0; i < 16; ++i) hs.mic[i] = 0xA0 + i;       // known MIC pattern
    for (int i = 0; i < 32; ++i) hs.anonce[i] = 0x30 + i;    // known ANonce pattern
    strcpy(hs.ssid, "Test");                                 // 54657374

    // LLC/SNAP + EAPOL marker, then a 99-byte EAPOL frame (declared len 95).
    static const uint8_t marker[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
    memcpy(hs.msg2Data, marker, 8);
    hs.msg2Data[8] = 0x02;   // EAPOL version
    hs.msg2Data[9] = 0x03;   // EAPOL type = Key
    hs.msg2Data[10] = 0x00;  // length hi
    hs.msg2Data[11] = 0x5F;  // length lo = 95 -> declared = 4 + 95 = 99
    for (int i = 12; i < 8 + 99; ++i) hs.msg2Data[i] = 0x11;  // filler key frame
    hs.msg2Len = 8 + 99;     // 107
    return hs;
}

CapturedPMKID makePmkid() {
    CapturedPMKID p;
    memset(&p, 0, sizeof(p));
    for (int i = 0; i < 16; ++i) p.pmkid[i] = 0x50 + i;
    for (int i = 0; i < 6; ++i) p.bssid[i] = 0x10 + i;
    for (int i = 0; i < 6; ++i) p.staMac[i] = 0x20 + i;
    strcpy(p.ssid, "Test");
    return p;
}

}  // namespace

// --- EAPOL (4-way) ---------------------------------------------------------

void test_eapol_line_uses_type_02(void) {
    CapturedHandshake hs = makeFourWay();
    char out[1024];
    TEST_ASSERT_TRUE(hc::buildEapolLine(hs, out, sizeof(out)));
    std::string line(out);
    TEST_ASSERT_TRUE(startsWith(line, "WPA*02*"));
    TEST_ASSERT_FALSE(startsWith(line, "WPA*01*"));
}

void test_eapol_field3_is_the_mic(void) {
    // Field 3 (after WPA*02*) must be the M2 MIC, then AP, STA, ESSID, ANonce.
    CapturedHandshake hs = makeFourWay();
    char out[1024];
    TEST_ASSERT_TRUE(hc::buildEapolLine(hs, out, sizeof(out)));
    std::string line(out);
    std::string expectedPrefix =
        "WPA*02*" + toHex(hs.mic, 16) + "*" + toHex(hs.apBssid, 6) + "*" +
        toHex(hs.clientMac, 6) + "*" + toHex(reinterpret_cast<const uint8_t*>(hs.ssid), 4) +
        "*" + toHex(hs.anonce, 32) + "*";
    TEST_ASSERT_TRUE(startsWith(line, expectedPrefix));
    TEST_ASSERT_TRUE(endsWith(line, "*00"));
}

void test_eapol_requires_m2_plus_anonce_source(void) {
    CapturedHandshake hs = makeFourWay();
    hs.hasMsg2 = false;  // only M1, no M2 -> nothing crackable
    char out[1024];
    TEST_ASSERT_FALSE(hc::buildEapolLine(hs, out, sizeof(out)));
}

void test_eapol_line_exact_golden_vector(void) {
    // Pins the whole WPA*02 line against a vector computed independently of the
    // builder (see the Python in the commit / ledger). The part no other test
    // covered is the EAPOL field: it must be M2's frame verbatim with the 16-byte
    // Key MIC (EAPOL offset 81) zeroed, because hashcat recomputes the MIC over
    // exactly those bytes and checks it against field 3. A wrong offset or a
    // dropped zeroing yields a correct-looking line that never cracks, which is
    // invisible to a prefix/suffix check, so assert the exact bytes end to end.
    CapturedHandshake hs = makeFourWay();
    char out[1024];
    TEST_ASSERT_TRUE(hc::buildEapolLine(hs, out, sizeof(out)));
    static const char* kGolden =
        "WPA*02*a0a1a2a3a4a5a6a7a8a9aaabacadaeaf*101112131415*202122232425*54657374*"
        "303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f*"
        "0203005f"  // EAPOL header: ver 02, type 03, len 005f (95)
        "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
        "00000000000000000000000000000000"  // Key MIC at EAPOL offset 81, zeroed
        "1111"  // trailing key-data-length bytes
        "*00";  // messagepair
    TEST_ASSERT_EQUAL_STRING(kGolden, out);
}

void test_eapol_truncation_fails_rather_than_partial(void) {
    CapturedHandshake hs = makeFourWay();
    char out[32];  // far too small for a WPA*02 line
    TEST_ASSERT_FALSE(hc::buildEapolLine(hs, out, sizeof(out)));
}

void test_eapol_declared_length_larger_than_available_trims_to_avail(void) {
    // Pins the trim-to-avail boundary: when the EAPOL length field advertises
    // more than the msg2Data cap can hold, the builder trims to what is actually
    // available instead of silently capping at a hard-coded 256 (the former bug).
    // Fill msg2Data to its full 256-byte cap, place the marker at offset 0 so
    // avail is maximal (248), and declare a bogus 65535-byte EAPOL frame.
    CapturedHandshake hs = makeFourWay();
    static const uint8_t marker[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
    memcpy(hs.msg2Data, marker, 8);
    hs.msg2Data[8]  = 0x02;  // EAPOL version
    hs.msg2Data[9]  = 0x03;  // EAPOL type = Key
    hs.msg2Data[10] = 0xFF;  // length hi
    hs.msg2Data[11] = 0xFF;  // length lo -> declared = 4 + 65535, far above avail
    for (int i = 12; i < 256; ++i) hs.msg2Data[i] = 0x22;
    hs.msg2Len = 256;

    char out[2048];
    TEST_ASSERT_TRUE(hc::buildEapolLine(hs, out, sizeof(out)));

    // Avail here is 256 - 8 = 248 bytes of EAPOL hex -> 496 hex chars.
    std::string line(out);
    size_t star = line.rfind('*');  // last '*' is before the "00" messagepair
    size_t prev = line.rfind('*', star - 1);  // '*' before the EAPOL field
    TEST_ASSERT_EQUAL_UINT(248 * 2, star - prev - 1);
    TEST_ASSERT_TRUE(endsWith(line, "*00"));
}

// --- PMKID -----------------------------------------------------------------

void test_pmkid_line_uses_type_01(void) {
    CapturedPMKID p = makePmkid();
    char out[256];
    TEST_ASSERT_TRUE(hc::buildPmkidLine(p, out, sizeof(out)));
    std::string line(out);
    TEST_ASSERT_TRUE(startsWith(line, "WPA*01*"));
    TEST_ASSERT_FALSE(startsWith(line, "WPA*02*"));
}

void test_pmkid_line_exact_layout(void) {
    CapturedPMKID p = makePmkid();
    char out[256];
    TEST_ASSERT_TRUE(hc::buildPmkidLine(p, out, sizeof(out)));
    std::string expected =
        "WPA*01*" + toHex(p.pmkid, 16) + "*" + toHex(p.bssid, 6) + "*" +
        toHex(p.staMac, 6) + "*" + toHex(reinterpret_cast<const uint8_t*>(p.ssid), 4) + "***";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), out);
}

void test_pmkid_truncation_fails_rather_than_partial(void) {
    CapturedPMKID p = makePmkid();
    char out[16];  // far too small
    TEST_ASSERT_FALSE(hc::buildPmkidLine(p, out, sizeof(out)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_eapol_line_uses_type_02);
    RUN_TEST(test_eapol_field3_is_the_mic);
    RUN_TEST(test_eapol_requires_m2_plus_anonce_source);
    RUN_TEST(test_eapol_line_exact_golden_vector);
    RUN_TEST(test_eapol_truncation_fails_rather_than_partial);
    RUN_TEST(test_eapol_declared_length_larger_than_available_trims_to_avail);
    RUN_TEST(test_pmkid_line_uses_type_01);
    RUN_TEST(test_pmkid_line_exact_layout);
    RUN_TEST(test_pmkid_truncation_fails_rather_than_partial);
    return UNITY_END();
}
