/**
 * @file test_pwncrack_response.cpp
 * @brief Native unit tests for the pwncrack upload-response classifier (ADR-0148).
 *
 * The success fixtures are frozen on the bodies a live probe measured (esp32-wifi-sapper ADR-0109,
 * 2026-11-05): accept, re-upload, and malformed-content all answer HTTP 200 {"success":true}, and a
 * bad key answers HTTP 400 {"error":"Invalid session key."}. No fixture encodes a guessed schema.
 *
 * Teeth: every fail-closed case here is an input the REPLACED substring heuristic got wrong. The old
 * check accepted any 2xx whose body lacked error/invalid/denied/unauthorized, so it false-accepted a
 * Cloudflare HTML 200, a {"success":false}, a bare `true`, and an empty/truncated body. Those cases
 * pass now and would have failed against the old code, which is the regression this fix closes.
 */

#include <cstring>
#include <string>

#include <unity.h>

#include "modules/network/pwncrack_response.h"

using adversary::pwncrackUploadAccepted;
using adversary::kPwncrackResponseMax;

void setUp(void) {}
void tearDown(void) {}

static bool classify(int status, const char* body) {
    return pwncrackUploadAccepted(status, body, std::strlen(body));
}

// --- The four measured legs (frozen fixtures). ---

// Legs 1-3 (accept, re-upload, malformed content) all answer HTTP 200 {"success":true}: all accepted.
// pwncrack cannot distinguish a fresh submit from a re-submit (no dup signal), and content is
// validated async server-side, so a malformed line still gets a success body here. That is harmless:
// the firmware only uploads well-formed hc22000 lines and WPA-SEC holds the .pcap independently, so a
// residual accept costs no handshake (ADR-0148).
void test_success_body_is_accepted(void) {
    TEST_ASSERT_TRUE(classify(200, "{\"success\":true}"));
}

// Leg 4 bad key answers HTTP 400 {"error":"Invalid session key."}: rejected (synchronous auth).
void test_bad_key_rejected(void) {
    TEST_ASSERT_FALSE(classify(400, "{\"error\":\"Invalid session key.\"}"));
}

// --- Fail-closed cases (each one the old substring heuristic accepted). ---

// A 200 whose body is a Cloudflare HTML interstitial is not JSON: rejected. A substring search for
// "success" / absence of "error" would have accepted it.
void test_non_json_200_rejected(void) {
    TEST_ASSERT_FALSE(
        classify(200, "<!DOCTYPE html><html><head><title>Just a moment...</title></head></html>"));
}

// A JSON root that is not an object (a bare boolean, number, array, string) is not the pwncrack shape.
void test_non_object_json_root_rejected(void) {
    TEST_ASSERT_FALSE(classify(200, "true"));
    TEST_ASSERT_FALSE(classify(200, "1"));
    TEST_ASSERT_FALSE(classify(200, "[true]"));
    TEST_ASSERT_FALSE(classify(200, "\"success\""));
}

// success:false, a string "true", a number 1, and a missing success key are each rejected (strict
// boolean). The old check accepted {"success":false} (no error keyword in it).
void test_success_false_or_mistyped_or_missing_rejected(void) {
    TEST_ASSERT_FALSE(classify(200, "{\"success\":false}"));
    TEST_ASSERT_FALSE(classify(200, "{\"success\":\"true\"}"));
    TEST_ASSERT_FALSE(classify(200, "{\"success\":1}"));
    TEST_ASSERT_FALSE(classify(200, "{\"status\":\"ok\"}"));
}

// The positive JSON body with a non-200 status is rejected: the status gate holds.
void test_success_body_with_non_200_rejected(void) {
    TEST_ASSERT_FALSE(classify(500, "{\"success\":true}"));
    TEST_ASSERT_FALSE(classify(203, "{\"success\":true}"));  // a 2xx that is not 200.
}

// An empty body, a nullptr, and a body over the bound are each rejected before any parse.
void test_empty_null_and_overbound_rejected(void) {
    TEST_ASSERT_FALSE(pwncrackUploadAccepted(200, "", 0));
    TEST_ASSERT_FALSE(pwncrackUploadAccepted(200, nullptr, 16));
    std::string big = "{\"success\":true,\"pad\":\"";
    big.append(kPwncrackResponseMax, 'x');
    big += "\"}";
    TEST_ASSERT_TRUE(big.size() > kPwncrackResponseMax);
    TEST_ASSERT_FALSE(pwncrackUploadAccepted(200, big.data(), big.size()));
}

// The bound is reachable at the exact boundary: a valid success object of exactly kPwncrackResponseMax
// bytes parses and is accepted, while one byte over is rejected before any parse. Pins both sides.
void test_body_bound_boundary(void) {
    const std::string base = "{\"success\":true,\"p\":\"";  // + pad + "\"}" closes the object.
    const size_t overhead = base.size() + 2;                // the trailing `"}`.
    std::string at = base + std::string(kPwncrackResponseMax - overhead, 'x') + "\"}";
    TEST_ASSERT_EQUAL_size_t(kPwncrackResponseMax, at.size());
    TEST_ASSERT_TRUE(pwncrackUploadAccepted(200, at.data(), at.size()));

    std::string over = base + std::string(kPwncrackResponseMax + 1 - overhead, 'x') + "\"}";
    TEST_ASSERT_EQUAL_size_t(kPwncrackResponseMax + 1, over.size());
    TEST_ASSERT_FALSE(pwncrackUploadAccepted(200, over.data(), over.size()));
}

// The classifier reads exactly `len` bytes, not a NUL terminator: a success object followed by
// trailing garbage beyond `len` must not be read (guards a future switch to strlen that would
// overread an unterminated TLS buffer).
void test_respects_explicit_length(void) {
    const char buf[] = "{\"success\":true}GARBAGE_PAST_THE_JSON";
    const size_t jsonLen = std::strlen("{\"success\":true}");
    TEST_ASSERT_TRUE(pwncrackUploadAccepted(200, buf, jsonLen));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_success_body_is_accepted);
    RUN_TEST(test_bad_key_rejected);
    RUN_TEST(test_non_json_200_rejected);
    RUN_TEST(test_non_object_json_root_rejected);
    RUN_TEST(test_success_false_or_mistyped_or_missing_rejected);
    RUN_TEST(test_success_body_with_non_200_rejected);
    RUN_TEST(test_empty_null_and_overbound_rejected);
    RUN_TEST(test_body_bound_boundary);
    RUN_TEST(test_respects_explicit_length);
    return UNITY_END();
}
