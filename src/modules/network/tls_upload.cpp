/**
 * @file tls_upload.cpp
 * @brief Heap-paced TLS upload helper shared by the WiGLE / WPA-SEC paths.
 */

#include "tls_upload.h"

#ifdef ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../../core/heap_policy.h"
#include "../system/time_manager.h"
#include "utils/log.h"

// Default Mozilla root-CA bundle embedded in the precompiled mbedTLS lib
// (x509_crt_bundle.S.obj in libmbedtls.a). Referencing it here pulls the bundle
// into the firmware (~68 KB flash) so setCACertBundle() can validate any
// well-issued server cert without us shipping our own roots.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

namespace adversary {
namespace tls_upload {

static size_t largestFreeBlock() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

bool heapTooLowForTls() {
    return largestFreeBlock() < heap_policy::MIN_CONTIG_FOR_TLS;
}

void applyCaBundle(WiFiClientSecure& client) {
    // x509_crt_bundle_{start,end} are linker-emitted symbols bounding one
    // contiguous embedded blob; their difference is the bundle's byte length.
    // cppcheck sees two distinct array objects and flags the subtraction, but
    // the idiom is well-defined by construction for _binary_*_start/_end symbols.
    // (cppcheck renamed this check id from comparePointers to subtractPointers.)
    // cppcheck-suppress subtractPointers
    const size_t bundleLen = (size_t)(x509_crt_bundle_end - x509_crt_bundle_start);
    client.setCACertBundle(x509_crt_bundle_start, bundleLen);
}

bool timeSyncedForTls() {
    auto& tm = TimeManager::getInstance();
    TimeSource src = tm.getSource();
    if (src != TimeSource::NTP && src != TimeSource::GPS) {
        // On-connect sync may have failed; we are connected now, so try once more.
        tm.syncFromNTP();
        src = tm.getSource();
    }
    return src == TimeSource::NTP || src == TimeSource::GPS;
}

bool connectWithRetry(WiFiClientSecure& client, const char* host, uint16_t port,
                      const char* tag) {
    constexpr int MAX_ATTEMPTS = 3;
    uint32_t backoffMs = 300;  // doubles each retry: 300 → 600 → 1200
    IPAddress ip;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        // Resolve first: DNS frequently fails for the first lookup right after
        // WiFi associates. A successful resolve also warms the cache for the
        // by-hostname connect below (which keeps SNI intact).
        if (WiFi.hostByName(host, ip) == 1 && ip != IPAddress((uint32_t)0)) {
            if (client.connect(host, port)) {
                LOG_I("TLS", "[%s] Connected to %s:%u (attempt %d, %s)",
                      tag, host, port, attempt, ip.toString().c_str());
                return true;
            }
            LOG_W("TLS", "[%s] connect() to %s:%u failed (attempt %d/%d)",
                  tag, host, port, attempt, MAX_ATTEMPTS);
        } else {
            LOG_W("TLS", "[%s] DNS failed for %s (attempt %d/%d)",
                  tag, host, attempt, MAX_ATTEMPTS);
        }

        if (attempt < MAX_ATTEMPTS) {
            delay(backoffMs);
            backoffMs *= 2;
        }
    }
    return false;
}

bool streamFile(WiFiClientSecure& client, File& file, size_t fileSize,
                const char* tag, char* outError, size_t outErrorLen) {
    // Larger chunks than the old 512B loop → fewer TLS records / less overhead.
    constexpr size_t CHUNK_SIZE = 2048;
    uint8_t chunk[CHUNK_SIZE];
    size_t bytesRemaining = fileSize;
    size_t bytesSent = 0;
    size_t nextLogAt = 0;  // heap trace every 32KB

    LOG_D("TLS", "[%s] write start: size=%u free=%u largest=%u",
          tag, (unsigned)fileSize, (unsigned)ESP.getFreeHeap(),
          (unsigned)largestFreeBlock());

    while (bytesRemaining > 0) {
        // Periodic heap trace so a stalled/failed upload shows the curve.
        if (bytesSent >= nextLogAt) {
            LOG_D("TLS", "[%s] write progress: sent=%u/%u free=%u largest=%u",
                  tag, (unsigned)bytesSent, (unsigned)fileSize,
                  (unsigned)ESP.getFreeHeap(), (unsigned)largestFreeBlock());
            nextLogAt = bytesSent + 32768;
        }

        // Heap pacing: free heap dipped → drain the send queue before feeding
        // more; clean-abort if it can't recover.
        if (ESP.getFreeHeap() < heap_policy::TLS_WRITE_SOFT_FLOOR) {
            client.flush();
            uint32_t drainStart = millis();
            while (ESP.getFreeHeap() < heap_policy::TLS_WRITE_SOFT_FLOOR &&
                   millis() - drainStart < heap_policy::TLS_WRITE_DRAIN_MS) {
                if (!client.connected()) break;
                delay(20);
                yield();
            }
            if (ESP.getFreeHeap() < heap_policy::TLS_WRITE_HARD_FLOOR) {
                LOG_E("TLS", "[%s] Heap floor hit mid-upload: sent=%u/%u free=%u — aborting",
                      tag, (unsigned)bytesSent, (unsigned)fileSize, (unsigned)ESP.getFreeHeap());
                if (outError && outErrorLen) snprintf(outError, outErrorLen, "Low heap @%uB", (unsigned)bytesSent);
                file.close();
                client.stop();
                return false;
            }
        }

        if (!client.connected()) {
            char tlsErr[64] = {0};
            int errCode = client.lastError(tlsErr, sizeof(tlsErr) - 1);
            LOG_E("TLS", "[%s] Connection lost: sent=%u/%u, err=%d (%s)",
                  tag, (unsigned)bytesSent, (unsigned)fileSize, errCode, tlsErr);
            if (outError && outErrorLen) snprintf(outError, outErrorLen, "Conn lost @%uB", (unsigned)bytesSent);
            file.close();
            client.stop();
            return false;
        }

        size_t toRead = (bytesRemaining > CHUNK_SIZE) ? CHUNK_SIZE : bytesRemaining;
        size_t bytesRead = file.read(chunk, toRead);
        if (bytesRead == 0) {
            LOG_E("TLS", "[%s] SD read failed at %u/%u", tag, (unsigned)bytesSent, (unsigned)fileSize);
            if (outError && outErrorLen) snprintf(outError, outErrorLen, "SD read @%uB", (unsigned)bytesSent);
            file.close();
            client.stop();
            return false;
        }

        size_t written = client.write(chunk, bytesRead);
        if (written != bytesRead) {
            char tlsErr[64] = {0};
            int errCode = client.lastError(tlsErr, sizeof(tlsErr) - 1);
            LOG_E("TLS", "[%s] TLS write failed: wrote=%u/%u, sent=%u/%u, err=%d (%s), conn=%d",
                  tag, (unsigned)written, (unsigned)bytesRead,
                  (unsigned)bytesSent, (unsigned)fileSize, errCode, tlsErr, client.connected());
            if (outError && outErrorLen) snprintf(outError, outErrorLen, "TLS write @%uB", (unsigned)bytesSent);
            file.close();
            client.stop();
            return false;
        }

        bytesSent += bytesRead;
        bytesRemaining -= bytesRead;
        yield();  // let the WiFi stack breathe
    }

    file.close();
    return true;
}

} // namespace tls_upload
} // namespace adversary

#endif // ESP32
