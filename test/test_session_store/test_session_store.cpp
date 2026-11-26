/**
 * @file test_session_store.cpp
 * @brief Unit tests for the dashboard session store (ADR-0170, slice-0171).
 *
 * All logic is pure over an injected table + nowMs, so these tests exercise
 * the same code the device runs.
 */

#include <unity.h>
#include <string.h>

#include "modules/server/session_store.h"

using namespace adversary;

static constexpr size_t kN = 4;
static Session table_[kN];

void setUp() {
    memset(table_, 0, sizeof(table_));
}
void tearDown() {}

// --- extractSessionToken -----------------------------------------------------

void test_extract_lone_cookie() {
    char out[33] = {};
    TEST_ASSERT_TRUE(extractSessionToken("adv_session=deadbeef", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("deadbeef", out);
}

void test_extract_multi_cookie() {
    char out[33] = {};
    TEST_ASSERT_TRUE(extractSessionToken(
        "foo=bar; adv_session=abc123; other=zzz", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc123", out);
}

void test_extract_last_in_header() {
    char out[33] = {};
    TEST_ASSERT_TRUE(extractSessionToken("x=1; y=2; adv_session=t0k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("t0k", out);
}

void test_extract_absent_returns_false() {
    char out[33] = "sentinel";
    TEST_ASSERT_FALSE(extractSessionToken("foo=bar; baz=qux", out, sizeof(out)));
}

void test_extract_empty_value_returns_false() {
    char out[33] = {};
    TEST_ASSERT_FALSE(extractSessionToken("adv_session=; other=1", out, sizeof(out)));
}

void test_extract_malformed_returns_false() {
    char out[33] = {};
    // Name with no '=' nor value.
    TEST_ASSERT_FALSE(extractSessionToken("adv_session; foo=bar", out, sizeof(out)));
}

void test_extract_null_inputs_safe() {
    char out[33] = {};
    TEST_ASSERT_FALSE(extractSessionToken(nullptr, out, sizeof(out)));
    TEST_ASSERT_FALSE(extractSessionToken("adv_session=x", nullptr, 32));
    TEST_ASSERT_FALSE(extractSessionToken("adv_session=x", out, 0));
}

void test_extract_name_is_not_a_prefix_match() {
    // "adv_sessionx" must not match "adv_session".
    char out[33] = {};
    TEST_ASSERT_FALSE(extractSessionToken("adv_sessionx=bad", out, sizeof(out)));
}

// --- createSession -----------------------------------------------------------

void test_create_fills_first_free_slot() {
    TEST_ASSERT_TRUE(createSession(table_, kN, "tok0", 1000));
    TEST_ASSERT_TRUE(table_[0].active);
    TEST_ASSERT_EQUAL_STRING("tok0", table_[0].token);
    TEST_ASSERT_EQUAL_UINT32(1000, table_[0].createdMs);
    TEST_ASSERT_EQUAL_UINT32(1000, table_[0].lastSeenMs);
    TEST_ASSERT_FALSE(table_[1].active);
}

void test_create_lru_evicts_when_full() {
    // Fill all slots with increasing lastSeenMs.
    createSession(table_, kN, "t1", 100);
    createSession(table_, kN, "t2", 200);
    createSession(table_, kN, "t3", 300);
    createSession(table_, kN, "t4", 400);
    // Touch slot 1 so slot 0 (t1, lastSeen=100) is the LRU.
    table_[1].lastSeenMs = 500;
    // New session evicts slot 0.
    TEST_ASSERT_TRUE(createSession(table_, kN, "t5", 600));
    TEST_ASSERT_EQUAL_STRING("t5", table_[0].token);
    TEST_ASSERT_EQUAL_UINT32(600, table_[0].createdMs);
    TEST_ASSERT_TRUE(table_[0].active);
}

void test_create_rejects_oversize_token() {
    // 33 chars + NUL does not fit the 33-byte buffer.
    const char* oversized = "0123456789012345678901234567890123";  // 34 chars
    TEST_ASSERT_FALSE(createSession(table_, kN, oversized, 1));
}

void test_create_null_safe() {
    TEST_ASSERT_FALSE(createSession(nullptr, kN, "t", 1));
    TEST_ASSERT_FALSE(createSession(table_, 0,  "t", 1));
    TEST_ASSERT_FALSE(createSession(table_, kN, nullptr, 1));
    TEST_ASSERT_FALSE(createSession(table_, kN, "", 1));
}

// --- validateSession ---------------------------------------------------------

void test_validate_fresh_token_touches_last_seen() {
    createSession(table_, kN, "tok", 1000);
    TEST_ASSERT_TRUE(validateSession(table_, kN, "tok", 2000,
                                     /*idle*/ 60000, /*abs*/ 3600000));
    TEST_ASSERT_EQUAL_UINT32(2000, table_[0].lastSeenMs);
    TEST_ASSERT_EQUAL_UINT32(1000, table_[0].createdMs);  // created preserved
}

void test_validate_rejects_after_idle_ttl() {
    createSession(table_, kN, "tok", 1000);
    // idleTtl = 5000; now = 1000 + 5001 -> idle age > idleTtl.
    TEST_ASSERT_FALSE(validateSession(table_, kN, "tok", 1000 + 5001,
                                      /*idle*/ 5000, /*abs*/ 3600000));
    TEST_ASSERT_FALSE(table_[0].active);  // deactivated
}

void test_validate_rejects_after_absolute_ttl() {
    createSession(table_, kN, "tok", 1000);
    // Keep touching so idle stays fresh, then overflow absolute.
    for (uint32_t t = 2000; t <= 10000; t += 1000) {
        validateSession(table_, kN, "tok", t, /*idle*/ 60000, /*abs*/ 9000);
    }
    // Now at t=10001 the absolute age (10001 - 1000 = 9001) exceeds abs=9000.
    TEST_ASSERT_FALSE(validateSession(table_, kN, "tok", 10001,
                                      /*idle*/ 60000, /*abs*/ 9000));
    TEST_ASSERT_FALSE(table_[0].active);
}

void test_validate_rejects_unknown_token() {
    createSession(table_, kN, "known", 1000);
    TEST_ASSERT_FALSE(validateSession(table_, kN, "stranger", 2000,
                                      /*idle*/ 60000, /*abs*/ 3600000));
    TEST_ASSERT_TRUE(table_[0].active);  // real session untouched
}

void test_validate_rejects_empty_token() {
    createSession(table_, kN, "tok", 1000);
    TEST_ASSERT_FALSE(validateSession(table_, kN, "", 2000,
                                      /*idle*/ 60000, /*abs*/ 3600000));
}

void test_validate_null_safe() {
    createSession(table_, kN, "tok", 1000);
    TEST_ASSERT_FALSE(validateSession(nullptr, kN, "tok", 2000, 60000, 3600000));
    TEST_ASSERT_FALSE(validateSession(table_, kN, nullptr, 2000, 60000, 3600000));
    TEST_ASSERT_FALSE(validateSession(table_, 0,  "tok", 2000, 60000, 3600000));
}

// --- clearSession ------------------------------------------------------------

void test_clear_invalidates_so_validate_fails() {
    createSession(table_, kN, "tok", 1000);
    clearSession(table_, kN, "tok");
    TEST_ASSERT_FALSE(table_[0].active);
    TEST_ASSERT_FALSE(validateSession(table_, kN, "tok", 2000,
                                      /*idle*/ 60000, /*abs*/ 3600000));
}

void test_clear_unknown_is_noop() {
    createSession(table_, kN, "tok", 1000);
    clearSession(table_, kN, "stranger");
    TEST_ASSERT_TRUE(table_[0].active);
}

void test_clear_null_safe() {
    createSession(table_, kN, "tok", 1000);
    clearSession(nullptr, kN, "tok");
    clearSession(table_, kN, nullptr);
    clearSession(table_, 0,  "tok");
    TEST_ASSERT_TRUE(table_[0].active);  // unchanged
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_extract_lone_cookie);
    RUN_TEST(test_extract_multi_cookie);
    RUN_TEST(test_extract_last_in_header);
    RUN_TEST(test_extract_absent_returns_false);
    RUN_TEST(test_extract_empty_value_returns_false);
    RUN_TEST(test_extract_malformed_returns_false);
    RUN_TEST(test_extract_null_inputs_safe);
    RUN_TEST(test_extract_name_is_not_a_prefix_match);

    RUN_TEST(test_create_fills_first_free_slot);
    RUN_TEST(test_create_lru_evicts_when_full);
    RUN_TEST(test_create_rejects_oversize_token);
    RUN_TEST(test_create_null_safe);

    RUN_TEST(test_validate_fresh_token_touches_last_seen);
    RUN_TEST(test_validate_rejects_after_idle_ttl);
    RUN_TEST(test_validate_rejects_after_absolute_ttl);
    RUN_TEST(test_validate_rejects_unknown_token);
    RUN_TEST(test_validate_rejects_empty_token);
    RUN_TEST(test_validate_null_safe);

    RUN_TEST(test_clear_invalidates_so_validate_fails);
    RUN_TEST(test_clear_unknown_is_noop);
    RUN_TEST(test_clear_null_safe);
    return UNITY_END();
}
