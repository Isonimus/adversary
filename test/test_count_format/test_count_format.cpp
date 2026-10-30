/**
 * @file test_count_format.cpp
 * @brief Unit tests for bounded index/total formatting.
 */

#include <unity.h>

#include <climits>

#include "utils/count_format.h"

using namespace adversary::utils;

void setUp() {}
void tearDown() {}

void test_count_normal(void) {
    char out[COUNT_FORMAT_BUFFER_SIZE];
    formatCount(2, 5, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2/5", out);
}

void test_count_large_values_fit(void) {
    char out[COUNT_FORMAT_BUFFER_SIZE];
    formatCount(1000, 1000, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1000/1000", out);
}

void test_count_signed_int_extremes_fit(void) {
    char out[COUNT_FORMAT_BUFFER_SIZE];
    formatCount(INT_MIN, INT_MAX, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-2147483648/2147483647", out);
}

void test_count_small_buffer_is_bounded(void) {
    char out[6];
    formatCount(1234, 5678, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1234/", out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_count_normal);
    RUN_TEST(test_count_large_values_fit);
    RUN_TEST(test_count_signed_int_extremes_fit);
    RUN_TEST(test_count_small_buffer_is_bounded);
    return UNITY_END();
}
