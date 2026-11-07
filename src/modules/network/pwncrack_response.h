/**
 * @file pwncrack_response.h
 * @brief Pure, host-tested classification of pwncrack's upload response (ADR-0148).
 *
 * Replaces the old substring heuristic in PwncrackService::uploadHandshake, which accepted any 2xx
 * lacking an error keyword and so false-accepted a reworded 2xx, a Cloudflare HTML 200, or a
 * truncated body, stranding the capture at pwncrack. The contract is now measured, not guessed: a
 * live four-leg probe (esp32-wifi-sapper ADR-0109, 2026-11-05; device-verified 2026-11-06) showed
 * the upload reply is a tiny JSON object, success is HTTP 200 `{"success": true}`, a bad key is
 * HTTP 400 `{"error": ...}`, and there is no duplicate signal.
 *
 * Fail-closed: a capture is marked uploaded only on that one positive shape; everything else is a
 * reject, kept and retried, never assumed uploaded. The parse is bounded (kPwncrackResponseMax), so
 * no allocation is ever sized to the reply, matching the no-PSRAM TLS discipline of the upload path.
 */

#pragma once

#include <cstddef>

namespace adversary {

/// Largest upload-response body this classifier parses. The measured bodies are 16-32 bytes; a body
/// over this bound is rejected before any parse, so no heap is ever sized to the reply (ADR-0148).
constexpr size_t kPwncrackResponseMax = 256;

/**
 * @brief Classify one pwncrack upload-response body.
 *
 * @param httpStatus the HTTP status-line code (e.g. 200, 400).
 * @param body       the response body (headers already stripped); need not be NUL-terminated,
 *                   it is read over @p len only.
 * @param len        length of @p body in bytes.
 * @return true only when @p httpStatus is 200 and @p body is a JSON object whose `success` is
 *         boolean true; false on everything else (non-200, nullptr/empty/over-bound body, non-JSON,
 *         a non-object JSON root, a `success` that is missing, false, or not a boolean).
 */
bool pwncrackUploadAccepted(int httpStatus, const char* body, size_t len);

}  // namespace adversary
