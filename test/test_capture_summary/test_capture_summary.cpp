/**
 * @file test_capture_summary.cpp
 * @brief HandshakeSummary PMKID-only classification + filename derivation.
 *
 * Regression guard for ADR-0144: the PMKID-only UI treatment (render as {ssid}.22000,
 * WPA-SEC N/A, dot follows pwncrack) must require an ACTUAL .22000 (has22000), not just
 * the type label. An older record mislabeled type=PMKID with no .22000 is really a 4-way
 * and must render as its .pcap. Before the fix, getFilename() branched on isPmkid alone,
 * so the mislabeled case wrongly produced "{ssid}.22000"; this pins the corrected behavior.
 */

#include <unity.h>

#include "modules/network/wpasec_service.h"      // full WpaSecStatus enum
#include "modules/storage/capture_registry.h"    // HandshakeSummary (self-mocks under !ESP32)

using adversary::HandshakeSummary;
using adversary::WpaSecStatus;

void setUp() {}
void tearDown() {}

static HandshakeSummary make(const char* ssid, bool isPmkid, bool has22000) {
    HandshakeSummary s(ssid, 100, WpaSecStatus::NOT_UPLOADED, WpaSecStatus::NOT_UPLOADED);
    s.isPmkid = isPmkid;
    s.has22000 = has22000;
    return s;
}

static bool endsWith(const char* str, const char* suffix) {
    size_t ls = strlen(str), lx = strlen(suffix);
    return ls >= lx && strcmp(str + ls - lx, suffix) == 0;
}

// A real PMKID-only capture: type PMKID AND a .22000 exists -> treated as PMKID-only.
void test_true_pmkid_only_is_pmkid_only_and_names_22000() {
    HandshakeSummary s = make("HomeNet", /*isPmkid=*/true, /*has22000=*/true);
    TEST_ASSERT_TRUE(s.isPmkidOnly());
    char name[48];
    s.getFilename(name, sizeof(name));
    TEST_ASSERT_TRUE(endsWith(name, ".22000"));
    TEST_ASSERT_FALSE(endsWith(name, ".pcap"));
}

// The ADR-0144 bug: type=PMKID but NO .22000 (misclassified 4-way). Must NOT be PMKID-only,
// and must render as a .pcap, not an unsyncable .22000 row.
void test_mislabeled_pmkid_without_hc22000_renders_as_pcap() {
    HandshakeSummary s = make("MOVISTAR_1A4F", /*isPmkid=*/true, /*has22000=*/false);
    TEST_ASSERT_FALSE(s.isPmkidOnly());
    char name[48];
    s.getFilename(name, sizeof(name));
    TEST_ASSERT_TRUE(endsWith(name, ".pcap"));
    TEST_ASSERT_FALSE(endsWith(name, ".22000"));
}

// A 4-way that also produced a .22000: not PMKID-only (it has a .pcap), shows .pcap.
void test_fourway_with_hc22000_is_not_pmkid_only() {
    HandshakeSummary s = make("CafeAP", /*isPmkid=*/false, /*has22000=*/true);
    TEST_ASSERT_FALSE(s.isPmkidOnly());
    char name[48];
    s.getFilename(name, sizeof(name));
    TEST_ASSERT_TRUE(endsWith(name, ".pcap"));
}

// A plain 4-way with no .22000: not PMKID-only, shows .pcap.
void test_plain_fourway_is_not_pmkid_only() {
    HandshakeSummary s = make("CafeAP", /*isPmkid=*/false, /*has22000=*/false);
    TEST_ASSERT_FALSE(s.isPmkidOnly());
    char name[48];
    s.getFilename(name, sizeof(name));
    TEST_ASSERT_TRUE(endsWith(name, ".pcap"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_true_pmkid_only_is_pmkid_only_and_names_22000);
    RUN_TEST(test_mislabeled_pmkid_without_hc22000_renders_as_pcap);
    RUN_TEST(test_fourway_with_hc22000_is_not_pmkid_only);
    RUN_TEST(test_plain_fourway_is_not_pmkid_only);
    return UNITY_END();
}
