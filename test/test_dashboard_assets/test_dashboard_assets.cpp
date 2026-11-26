/**
 * @file test_dashboard_assets.cpp
 * @brief Unit tests for the embedded-dashboard request-path resolver (slice-0169).
 */

#include <unity.h>
#include "modules/server/dashboard_assets.h"

using namespace adversary;

// Stub bytes: the resolver only reads pointers/strings, never the content, so
// any non-null spans serve. The length test uses these sizes.
static const uint8_t kIndexBytes[] = "<html>index</html>";
static const uint8_t kJsBytes[]    = "console.log(1)";
static const uint8_t kCssBytes[]   = "body{}";
static const uint8_t kLoginBytes[] = "<html>login</html>";

static const DashboardAsset kTable[] = {
    {"/index.html",   kIndexBytes, kIndexBytes + sizeof(kIndexBytes) - 1, "text/html"},
    {"/dashboard.js", kJsBytes,    kJsBytes + sizeof(kJsBytes) - 1,       "application/javascript"},
    {"/index.css",    kCssBytes,   kCssBytes + sizeof(kCssBytes) - 1,     "text/css"},
    {"/login.html",   kLoginBytes, kLoginBytes + sizeof(kLoginBytes) - 1, "text/html"},
};
static const size_t kCount = sizeof(kTable) / sizeof(kTable[0]);

void setUp() {}
void tearDown() {}

// --- Known paths resolve to the right asset + content type ------------------

void test_root_resolves_to_index() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("/index.html", a->requestPath);
    TEST_ASSERT_EQUAL_STRING("text/html", a->contentType);
}

void test_index_explicit() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/index.html");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("text/html", a->contentType);
}

void test_js_resolves() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/dashboard.js");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("application/javascript", a->contentType);
}

void test_css_resolves() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/index.css");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("text/css", a->contentType);
}

void test_login_resolves() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/login.html");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("text/html", a->contentType);
}

// --- Unknown paths 404 ------------------------------------------------------

void test_unknown_returns_null() {
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "/nope.txt"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "/dashboard.js.map"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, ""));
}

// --- Traversal probes fail to match (no path to escape) ---------------------

void test_traversal_returns_null() {
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "/../config/settings.json"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "/dashboard/../login.html"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "..%2fsettings.json"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, "/.."));
}

// --- Null-input safety ------------------------------------------------------

void test_null_inputs() {
    TEST_ASSERT_NULL(resolveDashboardAsset(nullptr, 0, "/index.html"));
    TEST_ASSERT_NULL(resolveDashboardAsset(kTable, kCount, nullptr));
}

// --- Length helper ----------------------------------------------------------

void test_asset_len() {
    const DashboardAsset* a = resolveDashboardAsset(kTable, kCount, "/index.html");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(sizeof(kIndexBytes) - 1, dashboardAssetLen(*a));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_root_resolves_to_index);
    RUN_TEST(test_index_explicit);
    RUN_TEST(test_js_resolves);
    RUN_TEST(test_css_resolves);
    RUN_TEST(test_login_resolves);
    RUN_TEST(test_unknown_returns_null);
    RUN_TEST(test_traversal_returns_null);
    RUN_TEST(test_null_inputs);
    RUN_TEST(test_asset_len);
    return UNITY_END();
}
