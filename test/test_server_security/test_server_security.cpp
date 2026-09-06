/**
 * @file test_server_security.cpp
 * @brief Unit tests for dashboard file-server path-traversal guard
 */

#include <unity.h>
#include "utils/path_security.h"

using namespace adversary::path_security;

void setUp() {}
void tearDown() {}

// --- Legitimate dashboard paths are allowed ---------------------------------
void test_allows_normal_paths() {
    TEST_ASSERT_TRUE(isSafeWebPath("/index.html"));
    TEST_ASSERT_TRUE(isSafeWebPath("/css/app.css"));
    TEST_ASSERT_TRUE(isSafeWebPath("/js/main.min.js"));
    TEST_ASSERT_TRUE(isSafeWebPath("/assets/logo.png"));
    TEST_ASSERT_TRUE(isSafeWebPath("/"));
}

// Names that merely contain dots (not a "/.." segment) are still fine.
void test_allows_dotted_names() {
    TEST_ASSERT_TRUE(isSafeWebPath("/a..b.html"));
    TEST_ASSERT_TRUE(isSafeWebPath("/v1.2.3/app.js"));
}

// --- Parent-directory traversal is rejected ---------------------------------
void test_rejects_traversal() {
    TEST_ASSERT_FALSE(isSafeWebPath("/../config/settings.json"));
    TEST_ASSERT_FALSE(isSafeWebPath("/../../etc/passwd"));
    TEST_ASSERT_FALSE(isSafeWebPath("/css/../../config/settings.json"));
    TEST_ASSERT_FALSE(isSafeWebPath("/.."));
    TEST_ASSERT_FALSE(isSafeWebPath("/captures/../credentials/wifi.json"));
}

// --- Backslash separators are rejected (no Windows-style traversal) ----------
void test_rejects_backslash() {
    TEST_ASSERT_FALSE(isSafeWebPath("/..\\config"));
    TEST_ASSERT_FALSE(isSafeWebPath("/foo\\bar"));
}

// --- Non-absolute / empty / null paths are rejected -------------------------
void test_rejects_non_absolute() {
    TEST_ASSERT_FALSE(isSafeWebPath("index.html"));
    TEST_ASSERT_FALSE(isSafeWebPath(""));
    TEST_ASSERT_FALSE(isSafeWebPath(nullptr));
    TEST_ASSERT_FALSE(isSafeWebPath("../secret"));
}

// --- Control characters are rejected ----------------------------------------
void test_rejects_control_chars() {
    TEST_ASSERT_FALSE(isSafeWebPath("/foo\nbar"));
    TEST_ASSERT_FALSE(isSafeWebPath("/foo\tbar"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_allows_normal_paths);
    RUN_TEST(test_allows_dotted_names);
    RUN_TEST(test_rejects_traversal);
    RUN_TEST(test_rejects_backslash);
    RUN_TEST(test_rejects_non_absolute);
    RUN_TEST(test_rejects_control_chars);
    return UNITY_END();
}
