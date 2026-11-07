/**
 * @file pwncrack_response.cpp
 * @brief Implementation of pwncrack upload-response classification (ADR-0148). See the header.
 */

#include "modules/network/pwncrack_response.h"

#include <ArduinoJson.h>

namespace adversary {

bool pwncrackUploadAccepted(int httpStatus, const char* body, size_t len) {
    // Only an HTTP 200 can be a success. The bad-key case answers HTTP 400 (synchronous auth,
    // measured 2026-11-05), so gating on the status makes a misprovisioned key a loud reject that
    // keeps and retries, never a silent uploaded-mark.
    constexpr int kHttpOk = 200;
    if (httpStatus != kHttpOk) return false;

    // Bound the body before touching the parser: an over-long or absent body is rejected, so nothing
    // is ever allocated proportional to the reply (the no-PSRAM TLS discipline, ADR-0148).
    if (body == nullptr || len == 0 || len > kPwncrackResponseMax) return false;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body, len);
    if (err) return false;  // not JSON (e.g. a Cloudflare HTML challenge served 200): reject.

    // The contract body is a JSON object (`{"success": true}` / `{"error": ...}`). Require an object
    // root explicitly: a bare `true`, a number, or an array is not the pwncrack shape.
    if (!doc.is<JsonObject>()) return false;

    // `success` must be a real JSON boolean true. Requiring the boolean type rejects a string "true"
    // or a numeric 1, and a missing key (the HTTP 400 `{"error": ...}` shape, or any unrecognised
    // object) reads as not-a-bool and is rejected.
    JsonVariantConst success = doc["success"];
    return success.is<bool>() && success.as<bool>();
}

}  // namespace adversary
