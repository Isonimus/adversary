/**
 * @file pwncrack_service.cpp
 * @brief pwncrack.org cloud cracking service implementation
 */

#include "pwncrack_service.h"
#include <memory>
#include <vector>
#include "pwncrack_response.h"
#include "pwncrack_potfile.h"
#include "../storage/settings_manager.h"
#include "../storage/capture_registry.h"
#include "tls_upload.h"

#ifndef ESP32
#include "../../test/common/arduino_mocks.h"
#endif

#include "../../ui/components/toast_manager.h"
#include "../../config/config.h"
#include "utils/log.h"

#if defined(ESP32) || defined(ARDUINO)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include "../wifi/wifi_connection.h"
#endif

namespace adversary {

// Stop draining the potfile stream after this long with no new bytes: a stalled or half-closed TLS
// socket must not hang the fetch. TCP flow control throttles the sender while we parse, so a slow
// drain never loses data; only a genuine stall trips this (ADR-0149).
constexpr unsigned long POTFILE_IDLE_TIMEOUT_MS = 5000;

// API key check helper (referenced from the header's hasApiKey()).
bool _pwncrackHasApiKey() {
    return SettingsManager::getInstance().hasPwncrackKey();
}

WpaSecStatus PwncrackService::getStatus(const char* ssid) const {
#ifdef ESP32
    if (!ssid) return WpaSecStatus::NOT_UPLOADED;
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
    for (const auto& s : summaries) {
        if (strcmp(s.ssid, ssid) == 0) return s.pwncrackStatus;
    }
    return WpaSecStatus::NOT_UPLOADED;
#else
    (void)ssid;
    return WpaSecStatus::NOT_UPLOADED;
#endif
}

UploadResult PwncrackService::uploadHandshake(const char* filepath, const char* ssidArg, bool quiet) {
#ifdef ESP32
    if (!hasApiKey()) {
        showErrorToast("No pwncrack key");
        return UploadResult::failed("no pwncrack key");
    }

    // SSID keys the manifest status; passed in (not derived from the sanitized filename)
    // so special-char SSIDs record status against the right record (ADR-0141).
    String ssid(ssidArg ? ssidArg : "");

    // Skip if already uploaded/cracked (pure RAM lookup, no SD).
    WpaSecStatus current = getStatus(ssid.c_str());
    if (current == WpaSecStatus::UPLOADED || current == WpaSecStatus::CRACKED) {
        if (!quiet) showToast("pwncrack: already sent");
        return UploadResult::already();
    }

    // Open briefly only for size, then close before TLS allocation.
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        // Fail loud: under a bulk sync the canvas is purged and this toast is
        // invisible, so name the missing path on the serial log (ADR-0142). This
        // is the bail that hid every mis-named PMKID .22000 before ADR-0141.
        LOG_W("PWNCRACK", "upload: cannot open .22000: %s", filepath);
        showErrorToast("No .22000 file");
        return UploadResult::failed("no .22000 file");
    }
    size_t fileSize = file.size();
    file.close();

    // pwncrack rejects >100KB; an empty file is nothing to send.
    if (fileSize == 0 || fileSize > 100 * 1024) {
        LOG_W("PWNCRACK", "upload: bad .22000 size %u: %s", (unsigned)fileSize, filepath);
        showErrorToast("Bad .22000 size");
        return UploadResult::failed("bad .22000 size");
    }

    const char* apiKey = SettingsManager::getInstance().getPwncrackKey();

    if (!WiFi.isConnected()) {
        showErrorToast("WiFi connection required");
        return UploadResult::failed("no wifi");
    }
    if (!tls_upload::timeSyncedForTls()) {
        showErrorToast("Time not synced");
        LOG_W("PWNCRACK", "Clock not NTP/GPS-synced, skipping");
        return UploadResult::failed("time not synced");
    }
    if (tls_upload::heapTooLowForTls()) {
        showErrorToast("Low heap, try again");
        LOG_W("PWNCRACK", "Heap too low for TLS, skipping");
        return UploadResult::failed("low heap");
    }

    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(15000);

    if (!tls_upload::connectWithRetry(client, PWNCRACK_HOST, PWNCRACK_PORT, "pwncrack")) {
        showErrorToast("Connection failed");
        LOG_E("PWNCRACK", "connect failed (heap: %lu)", ESP.getFreeHeap());
        return UploadResult::failed("connection failed");
    }

    // Multipart body: text field "key" then file field "handshake" (.hc22000).
    String boundary = "----EpicAdversaryBoundary";
    String preamble = "--" + boundary + "\r\n";
    preamble += "Content-Disposition: form-data; name=\"key\"\r\n\r\n";
    preamble += String(apiKey) + "\r\n";
    preamble += "--" + boundary + "\r\n";
    preamble += "Content-Disposition: form-data; name=\"handshake\"; filename=\"" + ssid + ".hc22000\"\r\n";
    preamble += "Content-Type: application/octet-stream\r\n\r\n";
    String footer = "\r\n--" + boundary + "--\r\n";

    size_t contentLength = preamble.length() + fileSize + footer.length();

    client.print("POST /upload_handshake HTTP/1.1\r\n");
    client.print("Host: " + String(PWNCRACK_HOST) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(contentLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(preamble);

    // Reopen for streaming (TLS buffers already allocated, file was closed).
    file = SD.open(filepath, FILE_READ);
    if (!file) {
        client.stop();
        LOG_W("PWNCRACK", "upload: cannot reopen %s", filepath);
        showErrorToast("Cannot reopen file");
        return UploadResult::failed("cannot reopen file");
    }

    char streamErr[48] = {0};
    if (!tls_upload::streamFile(client, file, fileSize, "pwncrack", streamErr, sizeof(streamErr))) {
        LOG_W("PWNCRACK", "upload: stream failed: %s", streamErr[0] ? streamErr : "unknown");
        showErrorToast(streamErr[0] ? streamErr : "Upload failed");
        return UploadResult::failed("upload failed");
    }

    client.print(footer);

    // Read response.
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 10000) {
            client.stop();
            LOG_W("PWNCRACK", "upload: response timeout for %s", filepath);
            showErrorToast("Upload timeout");
            return UploadResult::failed("upload timeout");
        }
        delay(10);
    }

    String response = "";
    timeout = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            response += (char)client.read();
            timeout = millis();
        } else {
            if (millis() - timeout > 5000) break;
            delay(10);
        }
    }
    client.stop();

    // Parse HTTP status line.
    int httpCode = 0;
    int sp = response.indexOf(' ');
    if (sp != -1) httpCode = response.substring(sp + 1, sp + 4).toInt();

    LOG_D("PWNCRACK", "upload response: %s", response.c_str());

    // Classify against the measured JSON contract, fail-closed (ADR-0148): accepted only on HTTP 200
    // with a JSON {"success": true}. The body is everything past the header terminator; a missing
    // terminator leaves it empty, which the classifier rejects. The full response is logged above, so
    // an unrecognised reply keeps the capture at NOT_UPLOADED to retry instead of a silent false "sent".
    int bodyStart = response.indexOf("\r\n\r\n");
    String body = (bodyStart != -1) ? response.substring(bodyStart + 4) : String();

    if (pwncrackUploadAccepted(httpCode, body.c_str(), body.length())) {
        CaptureRegistry::getInstance().setPwncrackStatus(ssid.c_str(), WpaSecStatus::UPLOADED);
        if (!quiet) showSuccessToast("Sent to pwncrack!");
        return UploadResult::sent();
    }

    char msg[40];
    snprintf(msg, sizeof(msg), "pwncrack err %d", httpCode);
    LOG_W("PWNCRACK", "upload rejected (HTTP %d) for %s", httpCode, filepath);
    showErrorToast(msg);
    return UploadResult::failed("server rejected");
#else
    (void)filepath;
    (void)ssidArg;
    (void)quiet;
    return UploadResult::failed("not supported");
#endif
}

int PwncrackService::fetchCrackedResults(bool quiet) {
#ifdef ESP32
    if (!hasApiKey()) {
        if (!quiet) showErrorToast("No pwncrack key");
        return 0;
    }

    const char* apiKey = SettingsManager::getInstance().getPwncrackKey();

    if (!tls_upload::timeSyncedForTls()) {
        if (!quiet) showErrorToast("Time not synced");
        LOG_W("PWNCRACK", "Clock not NTP/GPS-synced — skipping results fetch");
        return 0;
    }

    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(30000);

    HTTPClient http;
    String url = "https://";
    url += PWNCRACK_HOST;
    url += "/download_potfile_script?key=";
    url += apiKey;

    http.setTimeout(30000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        if (!quiet) showErrorToast("HTTP begin failed");
        return 0;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        // errorToString() only maps NEGATIVE HTTPClient codes; for a positive
        // HTTP status (404/403/...) it returns "" — hence the earlier blank log.
        // Always print the numeric code so the real cause is visible.
        LOG_E("PWNCRACK", "results fetch failed: HTTP %d (%s)",
              httpCode,
              httpCode < 0 ? http.errorToString(httpCode).c_str() : "server status");
        http.end();
        // 404 = no potfile for this key yet (nothing cracked). Benign, not an
        // error — don't shout, matching the "no results" wording used below.
        if (httpCode == HTTP_CODE_NOT_FOUND) {
            if (!quiet) showToast("No pwncrack results yet");
            return 0;
        }
        if (!quiet) {
            if (httpCode < 0) {
                showErrorToast("Connection failed");
            } else {
                char msg[32];
                snprintf(msg, sizeof(msg), "pwncrack HTTP %d", httpCode);
                showErrorToast(msg);
            }
        }
        return 0;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        if (!quiet) showErrorToast("No response stream");
        return 0;
    }

    // Match every potfile row against resident captures during the drain (pure RAM, no SD), buffering
    // only the matched rows, then apply them after http.end(). No SD write happens while the TLS socket
    // is open, which is the no-PSRAM fragmentation discipline the deferral protects (ADR-0149). The
    // buffer is bounded by the number of distinct local captures: the device can never apply more cracks
    // than it has captures, and an unmatched row is discarded here as it was before. No fixed cap.
    auto& registry = CaptureRegistry::getInstance();
    struct PendingCrack { int summaryIndex; char password[65]; uint8_t bssid[6]; bool bssidValid; };
    std::vector<PendingCrack> pending;
    pending.reserve(registry.getHandshakeSummaries().size());

    // nextLine pulls one line from the TLS stream, applying the same idle timeout as before. It returns
    // a trailing line that lacks a terminator once, then -1 when the stream is exhausted.
    unsigned long idle = millis();
    auto nextLine = [&](char* buf, size_t cap) -> int {
        int n = 0;
        for (;;) {
            if (stream->available()) {
                const char c = stream->read();
                idle = millis();
                if (c == '\n') { buf[n] = '\0'; return n; }
                if (c != '\r' && n < static_cast<int>(cap) - 1) buf[n++] = c;
            } else if (stream->connected() && millis() - idle <= POTFILE_IDLE_TIMEOUT_MS) {
                delay(10);
            } else {
                if (n > 0) { buf[n] = '\0'; return n; }  // final line without a newline.
                return -1;  // stream exhausted.
            }
        }
    };

    auto onResult = [&](const PotfileResult& r) {
        const int idx = registry.findSummaryIndexByBssidOrSsid(r.bssidValid ? r.bssid : nullptr, r.ssid);
        if (idx < 0) {
            LOG_W("PWNCRACK", "result unmatched: %s", r.ssid);
            return;
        }
        LOG_D("PWNCRACK", "Cracked: %s / %s", r.ssid, r.password);
        // De-dup by capture so the buffer stays bounded by the local capture count; a later row for the
        // same capture (e.g. matched once by BSSID, once by SSID) just refreshes the password.
        for (auto& p : pending) {
            if (p.summaryIndex == idx) {
                strncpy(p.password, r.password, sizeof(p.password) - 1);
                p.password[sizeof(p.password) - 1] = '\0';
                p.bssidValid = r.bssidValid;
                if (r.bssidValid) memcpy(p.bssid, r.bssid, 6);
                return;
            }
        }
        PendingCrack pc{};
        pc.summaryIndex = idx;
        strncpy(pc.password, r.password, sizeof(pc.password) - 1);
        pc.password[sizeof(pc.password) - 1] = '\0';
        pc.bssidValid = r.bssidValid;
        if (r.bssidValid) memcpy(pc.bssid, r.bssid, 6);
        pending.push_back(pc);
    };

    const int downloadedCount = collectPotfileResults(nextLine, onResult);

    http.end();  // heap available again

    int appliedCount = 0;
    for (auto& p : pending) {
        if (registry.applyCrackedAt(p.summaryIndex, p.bssidValid ? p.bssid : nullptr, p.password)) {
            appliedCount++;
        }
    }

    if (appliedCount > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "%d cracked (pwncrack)!", appliedCount);
        showSuccessToast(msg);  // good news always shown
    } else if (!quiet && downloadedCount > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "%d results, none here", downloadedCount);
        showToast(msg);
    } else if (!quiet) {
        showToast("No pwncrack results yet");
    }

    return appliedCount;
#else
    return 0;
#endif
}

} // namespace adversary
