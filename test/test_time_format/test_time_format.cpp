/**
 * @file test_time_format.cpp
 * @brief Unit tests for duration/time formatting helpers (slice-0057)
 */

#include <unity.h>
#include <cstring>
#include "utils/time_format.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

/** Short durations keep the two-digit zero-padded HH:MM:SS shape. */
void test_duration_padded(void) {
    char out[16];
    formatDuration(3661, out, sizeof(out));  // 1h 1m 1s
    TEST_ASSERT_EQUAL_STRING("01:01:01", out);
}

void test_duration_zero(void) {
    char out[16];
    formatDuration(0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("00:00:00", out);
}

/**
 * Hours grow past two digits without truncation. 444441600 s == 123456 h exactly, a
 * 12-char result ("123456:00:00") that the screen's old char[12] buffer could not hold —
 * this is the regression for the -Wformat-truncation overflow (slice-0057 DoD scenario 1).
 */
void test_duration_hours_overflow_old_buffer(void) {
    char out[16];
    formatDuration(444441600UL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("123456:00:00", out);
}

/** The uint32_t worst case still fits a 16-byte buffer. */
void test_duration_uint32_max(void) {
    char out[16];
    formatDuration(4294967295UL, out, sizeof(out));  // 1193046 h 28 m 15 s
    TEST_ASSERT_EQUAL_STRING("1193046:28:15", out);
}

// --- Display-only local-time offset (ADR-0118, slice-0119) ---

// Anchor: 2021-01-01 00:00:00 UTC. Getting the sign or the minutes-to-seconds scale wrong is
// the classic timezone bug, so each case asserts the rendered wall-clock fields, not arithmetic.
static constexpr time_t NEW_YEAR_2021_UTC = 1609459200;  // 2021-01-01 00:00:00 UTC

/** Offset 0 is a no-op: local fields equal UTC (the default, stock experience). */
void test_localfields_zero_offset_is_utc(void) {
    struct tm t;
    TEST_ASSERT_TRUE(localTimeFields(NEW_YEAR_2021_UTC, 0, &t));
    TEST_ASSERT_EQUAL_INT(2021, t.tm_year + 1900);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mon + 1);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(0, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(0, t.tm_min);
}

/** +5:30 (India, a half-hour zone) shifts the clock forward within the same day. */
void test_localfields_half_hour_positive(void) {
    struct tm t;
    TEST_ASSERT_TRUE(localTimeFields(NEW_YEAR_2021_UTC, 330, &t));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(5, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(30, t.tm_min);
}

/** +5:45 (Nepal, a quarter-hour zone) proves 15-minute granularity renders exactly. */
void test_localfields_quarter_hour_positive(void) {
    struct tm t;
    TEST_ASSERT_TRUE(localTimeFields(NEW_YEAR_2021_UTC, 345, &t));
    TEST_ASSERT_EQUAL_INT(5, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(45, t.tm_min);
}

/** A negative offset at 01:00 UTC rolls the date back across day, month and year. */
void test_localfields_negative_rolls_back_across_year(void) {
    struct tm t;
    const time_t oneAmUtc = NEW_YEAR_2021_UTC + 3600;  // 2021-01-01 01:00:00 UTC
    TEST_ASSERT_TRUE(localTimeFields(oneAmUtc, -180, &t));  // -3:00
    TEST_ASSERT_EQUAL_INT(2020, t.tm_year + 1900);
    TEST_ASSERT_EQUAL_INT(12, t.tm_mon + 1);
    TEST_ASSERT_EQUAL_INT(31, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(22, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(0, t.tm_min);
}

/** Zero renders as the bare "UTC" sentinel, not "UTC+0:00". */
void test_formatoffset_zero(void) {
    char out[10];
    formatUtcOffset(0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("UTC", out);
}

void test_formatoffset_positive_half_hour(void) {
    char out[10];
    formatUtcOffset(330, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("UTC+5:30", out);
}

/** The sign sits on the hour; the minute part stays positive (the classic negative-minute bug). */
void test_formatoffset_negative_half_hour(void) {
    char out[10];
    formatUtcOffset(-330, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("UTC-5:30", out);
}

/** The two configured extremes render within the documented 10-byte buffer. */
void test_formatoffset_extremes(void) {
    char out[10];
    formatUtcOffset(840, out, sizeof(out));   // +14:00
    TEST_ASSERT_EQUAL_STRING("UTC+14:00", out);
    formatUtcOffset(-720, out, sizeof(out));  // -12:00
    TEST_ASSERT_EQUAL_STRING("UTC-12:00", out);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_duration_padded);
    RUN_TEST(test_duration_zero);
    RUN_TEST(test_duration_hours_overflow_old_buffer);
    RUN_TEST(test_duration_uint32_max);
    RUN_TEST(test_localfields_zero_offset_is_utc);
    RUN_TEST(test_localfields_half_hour_positive);
    RUN_TEST(test_localfields_quarter_hour_positive);
    RUN_TEST(test_localfields_negative_rolls_back_across_year);
    RUN_TEST(test_formatoffset_zero);
    RUN_TEST(test_formatoffset_positive_half_hour);
    RUN_TEST(test_formatoffset_negative_half_hour);
    RUN_TEST(test_formatoffset_extremes);
    return UNITY_END();
}
