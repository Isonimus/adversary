/**
 * @file upload_result.h
 * @brief Structured outcome of one cloud-upload attempt (ADR-0142).
 *
 * A cracking-service upload (WPA-SEC <- .pcap, pwncrack <- .22000) returns this
 * instead of a bare bool, so the bulk-sync loop can tally per-file outcomes and
 * show a truthful post-sync recap (slice-0143). During a bulk run the canvas is
 * purged for TLS heap, so the per-file toasts a service fires are invisible; the
 * returned result is the only reliable channel.
 *
 * `reason` is ALWAYS a string literal (static lifetime) so the result can be
 * returned and copied without ownership hazards on this PSRAM-less chip. Anything
 * variable (HTTP code, stream error text, the path that failed) is LOG_W/LOG_E'd
 * at the failure site, not packed in here.
 */

#pragma once

#include <cstdint>

namespace adversary {

enum class UploadOutcome : uint8_t {
    Sent,             ///< server newly accepted the upload
    AlreadyUploaded,  ///< nothing new sent (local skip-check, or server duplicate)
    Failed,           ///< did not land, see reason
};

struct UploadResult {
    UploadOutcome outcome;
    const char* reason;  ///< static literal; nullptr unless outcome == Failed

    // Defaults to a failure so a forgotten early-return can never masquerade as success.
    constexpr UploadResult(UploadOutcome o = UploadOutcome::Failed, const char* r = nullptr)
        : outcome(o), reason(r) {}

    constexpr bool ok() const { return outcome != UploadOutcome::Failed; }

    static constexpr UploadResult sent() { return {UploadOutcome::Sent, nullptr}; }
    static constexpr UploadResult already() { return {UploadOutcome::AlreadyUploaded, nullptr}; }
    static constexpr UploadResult failed(const char* reason) { return {UploadOutcome::Failed, reason}; }
};

}  // namespace adversary
