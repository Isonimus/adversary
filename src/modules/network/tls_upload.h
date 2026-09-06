#pragma once

// Shared TLS upload plumbing for the WiGLE / WPA-SEC paths.
//
// streamFile() performs a heap-paced upload of an already-open file over an
// already-connected TLS client: it monitors free heap during the write,
// drains the send queue when memory dips, and cleanly aborts (closing the
// file and client) if it cannot recover — so the caller leaves the file's
// status untouched and it is retried on the next sync.

#ifdef ESP32

#include <stddef.h>
#include <WiFiClientSecure.h>
#include <SD.h>

namespace adversary {
namespace tls_upload {

/**
 * @brief Stream an open file to a connected TLS client with heap pacing.
 *
 * On success the file is closed and the client is left connected so the caller
 * can send a trailer and read the response. On failure the file and client are
 * both closed and a short diagnostic is written to outError.
 *
 * @param client       connected WiFiClientSecure
 * @param file         open, readable File positioned at the start of the body
 * @param fileSize     number of bytes to send
 * @param tag          short label for serial logs (e.g. "WPA-SEC")
 * @param outError     buffer for a short failure message (may be nullptr)
 * @param outErrorLen  size of outError
 * @return true only if the entire file was sent
 */
bool streamFile(WiFiClientSecure& client, File& file, size_t fileSize,
                const char* tag, char* outError, size_t outErrorLen);

/**
 * @brief True when the largest contiguous free block is too small to safely
 *        start a TLS upload (see heap_policy::MIN_CONTIG_FOR_TLS).
 */
bool heapTooLowForTls();

/**
 * @brief Configure a TLS client to validate the server against the embedded
 *        Mozilla root-CA bundle (replaces the old setInsecure()).
 *
 * Uses the full root bundle baked into the precompiled mbedTLS lib
 * (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL), so any well-issued server
 * cert validates and there is no per-host pinning to rotate. Call this once on a
 * fresh client before connectWithRetry(). Connecting by hostname (as
 * connectWithRetry does) keeps SNI + CN/SAN hostname verification intact.
 */
void applyCaBundle(WiFiClientSecure& client);

/**
 * @brief True when the system clock is good enough for cert-date validation.
 *
 * Cert validation checks notBefore/notAfter, and the Cardputer has no RTC, so a
 * stale clock fails a *legitimate* server. Returns true only when the time
 * source is NTP or GPS; a stale SD_CACHE (or NONE) is rejected. If the source is
 * not yet NTP/GPS this makes one more blocking NTP attempt before giving up
 * (we are already connected at the call site).
 */
bool timeSyncedForTls();

/**
 * @brief Connect a TLS client to host:port, retrying transient DNS/connect
 *        failures with exponential backoff.
 *
 * Resolves the host first (DNS often fails transiently right after WiFi
 * associates), then connects by hostname so SNI is preserved. The tag is used
 * for serial logs.
 *
 * @return true once connected, false after all attempts are exhausted.
 */
bool connectWithRetry(WiFiClientSecure& client, const char* host, uint16_t port,
                      const char* tag);

} // namespace tls_upload
} // namespace adversary

#endif // ESP32
