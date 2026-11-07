/**
 * @file test_pwncrack_potfile.cpp
 * @brief Native unit tests for the pwncrack potfile parser and uncapped collection (ADR-0149).
 *
 * Field layout is frozen on the verified pwncrack potfile format (esp32-wifi-sapper ADR-0109 #5,
 * checked against the live site and the pwncrack-addons plugin): colon-delimited, BSSID at field
 * index 1, ESSID at field index 3, password everything after the fourth colon, field index 0 an
 * ignored auxiliary name.
 *
 * Teeth: the ESSID case uses a line whose field 0 and field 3 differ, so the REPLACED parse (which
 * matched the SSID fallback against field 0) would read the wrong field; and the collection case
 * drives more than the old 32-row cap through collectPotfileResults, so the REPLACED fixed buffer
 * would deliver only 32. Both pass now and would have failed against the old code.
 */

#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "modules/network/pwncrack_potfile.h"

using adversary::parsePotfileLine;
using adversary::collectPotfileResults;
using adversary::PotfileResult;

void setUp(void) {}
void tearDown(void) {}

static bool parse(const char* line, PotfileResult& out) {
    return parsePotfileLine(line, std::strlen(line), out);
}

// --- parsePotfileLine: the verified field layout. ---

// A well-formed row: BSSID field 1, ESSID field 3, password after the fourth colon.
void test_valid_row_fields(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:aabbccddeeff:grp:MyNet:hunter2", r));
    TEST_ASSERT_TRUE(r.bssidValid);
    const uint8_t expect[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, r.bssid, 6);
    TEST_ASSERT_EQUAL_STRING("MyNet", r.ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2", r.password);
}

// The ESSID is field index 3, NOT field index 0. The old parse used field 0, so this line (whose
// field 0 and field 3 differ) pins the corrected field. This is the teeth for the ESSID-field fix.
void test_essid_comes_from_field_three_not_zero(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("WRONG:aabbccddeeff:grp:RIGHT:pw", r));
    TEST_ASSERT_EQUAL_STRING("RIGHT", r.ssid);
}

// A password may contain ':': everything after the fourth colon is kept verbatim.
void test_password_with_colons_kept_verbatim(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:aabbccddeeff:grp:MyNet:pa:ss:w0rd", r));
    TEST_ASSERT_EQUAL_STRING("pa:ss:w0rd", r.password);
}

// A hyphen-grouped MAC parses to the same octets as a bare one.
void test_hyphen_grouped_bssid(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:aa-bb-cc-dd-ee-ff:grp:Net:pw", r));
    TEST_ASSERT_TRUE(r.bssidValid);
    const uint8_t expect[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, r.bssid, 6);
}

// An unparseable BSSID does not reject the row (this firmware matches by BSSID OR SSID): the row
// stands with bssidValid=false so it can still match a capture by ESSID.
void test_bad_bssid_keeps_row_ssid_only(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:nothex:grp:Net:pw", r));
    TEST_ASSERT_FALSE(r.bssidValid);
    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, r.bssid, 6);
    TEST_ASSERT_EQUAL_STRING("Net", r.ssid);
    TEST_ASSERT_EQUAL_STRING("pw", r.password);
}

// Wrong hex-nibble count (10, not 12) leaves the BSSID invalid, but the row still stands.
void test_short_bssid_invalid(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:aabbccddee:grp:Net:pw", r));
    TEST_ASSERT_FALSE(r.bssidValid);
}

// Fewer than four colons is not a result row; an empty password recovers nothing: both rejected.
void test_too_few_colons_or_empty_password_rejected(void) {
    PotfileResult r;
    TEST_ASSERT_FALSE(parse("aux:aabbccddeeff:grp:Net", r));   // three colons.
    TEST_ASSERT_FALSE(parse("aux:aabbccddeeff:grp:Net:", r));  // four colons, empty password.
    TEST_ASSERT_FALSE(parse("", r));                           // empty line.
}

// A trailing CR/LF and trailing spaces are tolerated and stripped off the password.
void test_trailing_terminator_and_spaces_stripped(void) {
    PotfileResult r;
    TEST_ASSERT_TRUE(parse("aux:aabbccddeeff:grp:Net:pw  \r\n", r));
    TEST_ASSERT_EQUAL_STRING("pw", r.password);
}

// The parser reads exactly `len` bytes, not a NUL terminator: content past len must not be read.
void test_respects_explicit_length(void) {
    const char buf[] = "aux:aabbccddeeff:grp:Net:pw\nGARBAGE:PAST:THE:LINE:here";
    const size_t lineLen = std::strlen("aux:aabbccddeeff:grp:Net:pw");
    PotfileResult r;
    TEST_ASSERT_TRUE(parsePotfileLine(buf, lineLen, r));
    TEST_ASSERT_EQUAL_STRING("pw", r.password);
}

// A password longer than the field is truncated at the bound and stays NUL-terminated.
void test_overlong_password_truncated(void) {
    std::string line = "aux:aabbccddeeff:grp:Net:";
    line.append(200, 'p');
    PotfileResult r;
    TEST_ASSERT_TRUE(parsePotfileLine(line.data(), line.size(), r));
    TEST_ASSERT_EQUAL_size_t(sizeof(r.password) - 1, std::strlen(r.password));
}

// --- collectPotfileResults: no cap. ---

// A line source over a fixed array of C-strings.
struct LineSource {
    const std::vector<std::string>& lines;
    size_t i = 0;
    int operator()(char* buf, size_t cap) {
        if (i >= lines.size()) return -1;
        const std::string& s = lines[i++];
        const size_t n = (s.size() < cap - 1) ? s.size() : cap - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
        return static_cast<int>(n);
    }
};

// More than the old 32-row cap: every valid row must reach the sink. The old fixed buffer delivered
// only 32, so this is the regression that the cap removal closes. Teeth for the no-cap fix.
void test_collect_delivers_every_row_past_old_cap(void) {
    const int kRows = 40;  // deliberately past the old crackedBuffer[32].
    std::vector<std::string> lines;
    for (int i = 0; i < kRows; ++i) {
        char line[64];
        std::snprintf(line, sizeof(line), "aux:aabbccddee%02x:grp:Net%02d:pw%02d", i, i, i);
        lines.emplace_back(line);
    }
    LineSource source{lines};
    std::vector<PotfileResult> got;
    const int valid = collectPotfileResults(source, [&](const PotfileResult& r) { got.push_back(r); });

    TEST_ASSERT_EQUAL_INT(kRows, valid);
    TEST_ASSERT_EQUAL_size_t(static_cast<size_t>(kRows), got.size());
    TEST_ASSERT_EQUAL_STRING("Net00", got.front().ssid);
    TEST_ASSERT_EQUAL_STRING("Net39", got.back().ssid);
}

// Blank and malformed lines are skipped; only valid rows are counted and delivered.
void test_collect_skips_invalid_lines(void) {
    std::vector<std::string> lines = {
        "aux:aabbccddeeff:grp:A:pw",  // valid
        "",                            // blank
        "garbage with no colons",      // malformed
        "a:b:c:d",                     // three colons
        "aux:aabbccddeeff:grp:B:pw",  // valid
    };
    LineSource source{lines};
    int count = 0;
    const int valid = collectPotfileResults(source, [&](const PotfileResult&) { ++count; });
    TEST_ASSERT_EQUAL_INT(2, valid);
    TEST_ASSERT_EQUAL_INT(2, count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_row_fields);
    RUN_TEST(test_essid_comes_from_field_three_not_zero);
    RUN_TEST(test_password_with_colons_kept_verbatim);
    RUN_TEST(test_hyphen_grouped_bssid);
    RUN_TEST(test_bad_bssid_keeps_row_ssid_only);
    RUN_TEST(test_short_bssid_invalid);
    RUN_TEST(test_too_few_colons_or_empty_password_rejected);
    RUN_TEST(test_trailing_terminator_and_spaces_stripped);
    RUN_TEST(test_respects_explicit_length);
    RUN_TEST(test_overlong_password_truncated);
    RUN_TEST(test_collect_delivers_every_row_past_old_cap);
    RUN_TEST(test_collect_skips_invalid_lines);
    return UNITY_END();
}
