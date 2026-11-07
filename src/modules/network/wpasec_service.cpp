/**
 * @file wpasec_service.cpp
 * @brief WPA-SEC cloud cracking service implementation
 */

#include "wpasec_service.h"
#include <memory>
#include "../storage/settings_manager.h"
#include "../storage/capture_registry.h"
#include "tls_upload.h"

#ifndef ESP32
#include "../../test/common/arduino_mocks.h"
#endif

#include "../../ui/components/toast_manager.h"
#include "../../config/config.h"
#include "../../utils/filename_utils.h"
#include "utils/log.h"

#if defined(ESP32) || defined(ARDUINO)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include "../wifi/wifi_connection.h"
#include "../system/system_manager.h"
#endif

namespace adversary {

// Helper function for API key check (declared in header)
bool _wpaSecHasApiKey() {
    return SettingsManager::getInstance().hasWpaSecKey();
}

UploadResult WpaSecService::uploadHandshake(const char* filepath, const char* ssidArg, bool deferCleanup) {
#ifdef ESP32
    if (!hasApiKey()) {
        showErrorToast("No WPA-SEC key");
        return UploadResult::failed("no WPA-SEC key");
    }

    // Get filename from path (for logging + the multipart form field only)
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    LOG_I("WPASEC", "Upload starting: %s", filename);

    // Check local status FIRST (before opening file) to avoid SD reads for already-uploaded files.
    // Keyed by the capture SSID (ADR-0141), not the sanitized filename stem. Pure RAM lookup.
    WpaSecStatus currentStatus = WpaSecStatus::NOT_UPLOADED;
    {
        const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
        for (const auto& s : summaries) {
            if (ssidArg && strcmp(s.ssid, ssidArg) == 0) { currentStatus = s.wpaSecStatus; break; }
        }
    }
    if (currentStatus == WpaSecStatus::UPLOADED ||
        currentStatus == WpaSecStatus::CRACKED ||
        currentStatus == WpaSecStatus::INVALID) {
        if (!deferCleanup) showToast("Already processed (skipped)");  // quiet during bulk
        return UploadResult::already();
    }

    // Open file briefly ONLY to get size, then close before SSL allocation
    // This prevents file buffer + SSL buffers from fragmenting heap
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        // Fail loud: the canvas is purged during a bulk sync so this toast is
        // invisible; name the missing path on the serial log (ADR-0142).
        LOG_W("WPASEC", "upload: cannot open %s", filepath);
        showErrorToast("Cannot open file");
        return UploadResult::failed("cannot open file");
    }

    size_t fileSize = file.size();

    if (fileSize == 0 || fileSize > 1024*1024) {  // Max 1MB
        file.close();
        LOG_W("WPASEC", "upload: invalid file size %u: %s", (unsigned)fileSize, filepath);
        showErrorToast("Invalid file size");
        return UploadResult::failed("invalid file size");
    }
    
    file.close();  // *** CRITICAL: Close before SSL allocation ***
    
    // Get API key
    const char* apiKey = SettingsManager::getInstance().getWpaSecKey();
    
    // Simplified: Assume WiFi is already established by CapturesScreen
    static_cast<void>(CaptureRegistry::getInstance()); // Suppress unused
    
    if (!WiFi.isConnected()) {
        showErrorToast("WiFi connection required");
        return UploadResult::failed("no wifi");
    }

    // Cert validation checks notBefore/notAfter and the Cardputer has no RTC, so
    // refuse to start TLS on a stale clock (would fail a legit server cert).
    if (!tls_upload::timeSyncedForTls()) {
        showErrorToast("Time not synced");
        LOG_W("WPASEC", "Clock not NTP/GPS-synced, skipping (retry next sync)");
        return UploadResult::failed("time not synced");
    }

    // Gate on contiguous heap before allocating TLS buffers, so we fail fast
    // (and leave the file for retry) instead of dying part-way through connect().
    if (tls_upload::heapTooLowForTls()) {
        showErrorToast("Low heap, try again");
        LOG_W("WPASEC", "Heap too low for TLS, skipping (retry next sync)");
        return UploadResult::failed("low heap");
    }

    // Create HTTPS client — validate the server against the embedded root-CA
    // bundle so the API key (Cookie: key=...) can't be MITM'd.
    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(15000);  // 15 second timeout

    if (!tls_upload::connectWithRetry(client, WPASEC_HOST, WPASEC_PORT, "WPA-SEC")) {
        showErrorToast("Connection failed");
        LOG_E("WPASEC", "Upload connect failed (heap: %lu)", ESP.getFreeHeap());
        return UploadResult::failed("connection failed");
    }
    
    // Build multipart form data
    String boundary = "----EpicAdversaryBoundary";
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"\r\n";
    header += "Content-Type: application/octet-stream\r\n\r\n";
    String footer = "\r\n--" + boundary + "--\r\n";
    
    size_t contentLength = header.length() + fileSize + footer.length();
    
    // Send HTTP request
    client.print("POST / HTTP/1.1\r\n");
    client.print("Host: " + String(WPASEC_HOST) + "\r\n");
    client.print("Cookie: key=" + String(apiKey) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(contentLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // Send multipart header
    client.print(header);
    
    // Reopen file for streaming (SSL is already allocated, file was closed earlier)
    file = SD.open(filepath, FILE_READ);
    if (!file) {
        client.stop();
        LOG_W("WPASEC", "upload: cannot reopen %s", filepath);
        showErrorToast("Cannot reopen file");
        return UploadResult::failed("cannot reopen file");
    }

    // Heap-paced upload. On failure the helper closes the file + client and we
    // return without touching the registry status, so the file is retried on
    // the next sync (rather than being marked INVALID/UPLOADED).
    char streamErr[48] = {0};
    if (!tls_upload::streamFile(client, file, fileSize, "WPA-SEC", streamErr, sizeof(streamErr))) {
        LOG_W("WPASEC", "upload: stream failed: %s", streamErr[0] ? streamErr : "unknown");
        showErrorToast(streamErr[0] ? streamErr : "Upload failed");
        return UploadResult::failed("upload failed");
    }

    // Send footer
    client.print(footer);
    
    // Wait for response
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 10000) {
            client.stop();
            LOG_W("WPASEC", "upload: response timeout for %s", filepath);
            showErrorToast("Upload timeout");
            return UploadResult::failed("upload timeout");
        }
        delay(10);
    }
    
    // Read response (Robust Loop)
    String response = "";
    timeout = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            response += (char)client.read();
            timeout = millis(); // Reset timeout on data
        } else {
            if (millis() - timeout > 5000) break; // 5s timeout for next chunk
            delay(10);
        }
    }
    client.stop();
    
    // Check for success indicators from WPA-SEC/hcxpcapngtool
    // - "Uploaded" - direct upload success
    // - "already in database" - file was already processed
    // - "EAPOL pairs written" - hcxpcapngtool extracted handshake successfully
    // Status is persisted to the BSSID-keyed manifest via the registry
    
    // Status is keyed by the capture SSID passed in by the caller (ADR-0141), so a
    // sanitized filename cannot desync it from the display-SSID registry record.
    String ssid(ssidArg ? ssidArg : "");
    
    // 1. Duplicate
    if (response.indexOf("already in database") != -1) {
        CaptureRegistry::getInstance().setWpaSecStatus(ssid.c_str(), WpaSecStatus::UPLOADED);
        if (!deferCleanup) {
            CaptureRegistry::getInstance().rescanSummaries();
            WiFiConnection::getInstance().disconnect();  // Keep STA mode for scanner
            showToast("Already uploaded");
        }
        return UploadResult::already();
    }

    // 2. No valid handshakes found by server
    if (response.indexOf("No valid handshakes") != -1 ||
        response.indexOf("0 EAPOL pairs written") != -1) {
        CaptureRegistry::getInstance().setWpaSecStatus(ssid.c_str(), WpaSecStatus::INVALID);
        if (!deferCleanup) {
            CaptureRegistry::getInstance().rescanSummaries();
            WiFiConnection::getInstance().disconnect();  // Keep STA mode for scanner
        }
        LOG_W("WPASEC", "upload: server found no valid handshakes in %s", filepath);
        showErrorToast("No valid handshakes");
        return UploadResult::failed("no valid handshakes");
    }

    // 3. Success (New upload). The server pipes back hcxpcapngtool's summary; a
    // PMKID-only / partial capture is accepted (PMKID extracted) but never says
    // "Uploaded" or "EAPOL pairs written" — recognise the written PMKID too, or
    // it stays NOT_UPLOADED (red) and gets re-uploaded forever.
    if (response.indexOf("Uploaded") != -1 ||
        response.indexOf("EAPOL pairs written") != -1 ||
        response.indexOf("PMKID written to 22000 hash file") != -1) {
        CaptureRegistry::getInstance().setWpaSecStatus(ssid.c_str(), WpaSecStatus::UPLOADED);
        if (!deferCleanup) {
            CaptureRegistry::getInstance().rescanSummaries();
            WiFiConnection::getInstance().disconnect();  // Keep STA mode for scanner
            showSuccessToast("Uploaded to WPA-SEC!");
        }
        return UploadResult::sent();
    }

    // Extract error message from response body
    int bodyStart = response.indexOf("\r\n\r\n");
    if (bodyStart != -1) {
        String body = response.substring(bodyStart + 4);
        body.trim();
        
        // Skip chunked encoding length if present (e.g. "37\n")
        int newlinePos = body.indexOf('\n');
        if (newlinePos > 0 && newlinePos < 5) {
            body = body.substring(newlinePos + 1);
            body.trim();
        }
        if (body.length() > 0) {
            // Full unrecognised response to serial for debugging the failure.
            LOG_W("WPASEC", "unhandled API response: %s", body.c_str());

            // Truncate for toast display only
            String toastMsg = body;
            if (toastMsg.length() > 40) {
                toastMsg = toastMsg.substring(0, 37) + "...";
            }
            showErrorToast(toastMsg.c_str());
        }
    }
    
    return UploadResult::failed("server error");
#else
    (void)filepath;
    (void)ssidArg;
    (void)deferCleanup;
    return UploadResult::failed("not supported");
#endif
}

int WpaSecService::fetchCrackedResults() {
#ifdef ESP32
    if (!hasApiKey()) {
        showErrorToast("No WPA-SEC key");
        return 0;
    }
    
    const char* apiKey = SettingsManager::getInstance().getWpaSecKey();

    // Refuse to fetch (which sends the API key) over an unvalidatable TLS clock.
    if (!tls_upload::timeSyncedForTls()) {
        showErrorToast("Time not synced");
        LOG_W("WPASEC", "Clock not NTP/GPS-synced — skipping results fetch");
        return 0;
    }

    // Validated HTTPS: HTTPClient over a WiFiClientSecure that checks the server
    // cert against the embedded root-CA bundle (client must outlive http).
    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(30000);

    HTTPClient http;

    // Build URL with API endpoint
    String url = "https://";
    url += WPASEC_HOST;
    url += "/?api&dl=1";

    // Configure HTTPClient
    http.setTimeout(30000);  // 30 second timeout
    http.setReuse(false);

    if (!http.begin(client, url)) {
        showErrorToast("HTTP begin failed");
        LOG_E("WPASEC", "HTTPClient begin() failed");
        return 0;
    }
    
    // Set cookie header for authentication
    String cookie = "key=";
    cookie += apiKey;
    http.addHeader("Cookie", cookie);
    
    int httpCode = http.GET();
    
    // Check for HTTP errors
    if (httpCode != HTTP_CODE_OK) {
        LOG_E("WPASEC", "HTTP error: %s", http.errorToString(httpCode).c_str());
        http.end();
        
        if (httpCode < 0) {
            showErrorToast("Connection failed");
        } else {
            char msg[32];
            snprintf(msg, sizeof(msg), "HTTP error %d", httpCode);
            showErrorToast(msg);
        }
        return 0;
    }
    
    // Get response stream for efficient reading
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        showErrorToast("No response stream");
        return 0;
    }
    
    // Heap-allocated temp buffer for cracked entries; applied after SSL closes.
    // Was static local (3KB BSS always); now ~3KB heap only during this call.
    struct CrackedEntry { char ssid[33]; char password[64]; uint8_t bssid[6]; };
    std::unique_ptr<CrackedEntry[]> crackedBuffer(new (std::nothrow) CrackedEntry[32]);
    if (!crackedBuffer) { http.end(); return 0; }
    int crackedBufferCount = 0;

    // Parse "aabbccddeeff" / "aa:bb:cc:dd:ee:ff" -> 6 bytes. Returns false on
    // malformed input (caller then falls back to SSID matching).
    auto parseApBssid = [](const char* s, uint8_t out[6]) -> bool {
        if (!s) return false;
        char hex[13];
        int n = 0;
        for (const char* p = s; *p && n < 12; ++p) {
            if (*p != ':' && *p != '-') hex[n++] = *p;
        }
        if (n != 12) return false;
        hex[12] = '\0';
        for (int i = 0; i < 6; ++i) {
            unsigned v;
            if (sscanf(hex + i * 2, "%2x", &v) != 1) return false;
            out[i] = (uint8_t)v;
        }
        return true;
    };
    
    // Parse body line-by-line with FIXED stack buffer (no heap allocation!)
    // Format: ap_bssid:client_bssid:ssid:password
    char lineBuffer[256];  // Max line size - stack allocated
    int lineLen = 0;
    int downloadedCount = 0;
    
    auto parseLine = [&](char* line) {
        // Trim trailing whitespace
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0) return;
        
        // Parse: ap_bssid:client_bssid:ssid:password using strtok_r (no heap)
        char* saveptr;
        char* apBssid = strtok_r(line, ":", &saveptr);
        char* clBssid = strtok_r(NULL, ":", &saveptr);
        char* ssid = strtok_r(NULL, ":", &saveptr);
        char* password = strtok_r(NULL, ":", &saveptr);

        (void)clBssid;

        if (ssid && password && strlen(ssid) > 0 && strlen(password) > 0) {
            downloadedCount++;
            // Store in temp buffer for later merge (don't touch cache during SSL)
            if (crackedBufferCount < 32) {
                strncpy(crackedBuffer[crackedBufferCount].ssid, ssid, 32);
                crackedBuffer[crackedBufferCount].ssid[32] = '\0';
                strncpy(crackedBuffer[crackedBufferCount].password, password, 63);
                crackedBuffer[crackedBufferCount].password[63] = '\0';
                if (!parseApBssid(apBssid, crackedBuffer[crackedBufferCount].bssid)) {
                    memset(crackedBuffer[crackedBufferCount].bssid, 0, 6);
                }
                crackedBufferCount++;
            }
            LOG_D("WPASEC", "Cracked: %s - %s", ssid, password);
        }
    };
    
    // Stream-read body character by character
    unsigned long timeout = millis();
    while (stream->connected() || stream->available()) {
        if (stream->available()) {
            char c = stream->read();
            
            if (c == '\n') {
                lineBuffer[lineLen] = '\0';
                parseLine(lineBuffer);
                lineLen = 0;
            } else if (c != '\r' && lineLen < (int)sizeof(lineBuffer) - 1) {
                lineBuffer[lineLen++] = c;
            }
            timeout = millis();  // Reset timeout on data
        } else {
            if (millis() - timeout > 5000) break;  // 5s idle timeout
            delay(10);
        }
    }
    
    // Parse any remaining line
    if (lineLen > 0) {
        lineBuffer[lineLen] = '\0';
        parseLine(lineBuffer);
    }
    
    http.end();  // Close HTTPClient - heap now available!
    
    // Persist cracked passwords to the manifest (single source of truth),
    // matched by BSSID first then SSID. Many downloaded results legitimately
    // aren't on THIS device (deleted, captured on another unit) — those simply
    // don't match. No JSON is written here: getMetadata/getCrackedPassword read
    // the manifest now, and skipping ~N SD writes keeps the post-SSL heap (with
    // the canvas purged) from fragmenting before the UI is restored.
    int appliedCount = 0;
    if (crackedBufferCount > 0) {
        auto& registry = CaptureRegistry::getInstance();
        for (int i = 0; i < crackedBufferCount; i++) {
            const char* ssid = crackedBuffer[i].ssid;
            if (registry.setCrackedPassword(crackedBuffer[i].bssid, ssid,
                                            crackedBuffer[i].password)) {
                appliedCount++;
                LOG_D("WPASEC", "Cracked applied: %s", ssid);
            } else {
                LOG_D("WPASEC", "Cracked result not on device: %s", ssid);
            }
        }
    }

    // Report what actually landed on THIS device, not the raw download count.
    if (appliedCount > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "%d cracked!", appliedCount);
        showSuccessToast(msg);
    } else if (downloadedCount > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "%d results, none here", downloadedCount);
        showToast(msg);
    } else {
        showToast("No results yet");
    }

    return appliedCount;
#else
    return 0;
#endif
}

bool WpaSecService::validatePcap(const char* filepath) {
#ifdef ESP32
    File file = SD.open(filepath, FILE_READ);
    if (!file) return false;
    
    // Check for pcap magic number (0xa1b2c3d4 or 0xd4c3b2a1)
    // or pcapng magic (0x0a0d0d0a)
    uint32_t magic = 0;
    if (file.read((uint8_t*)&magic, 4) != 4) {
        file.close();
        return false;
    }
    file.close();
    
    if (magic == 0xa1b2c3d4 || magic == 0xd4c3b2a1 ||  // pcap
        magic == 0x0a0d0d0a) {                          // pcapng
        return true;
    }
    
    return false;
#else
    (void)filepath;
    return false;
#endif
}

// Status Methods - Use WpaSecCache for O(1) Memory Lookups
// =============================================================================

/**
 * @brief Extract SSID from filename for cache lookup
 * Filename format: "SSID.pcap" or legacy "SSID_timestamp.pcap"
 */
static String extractSSID(const char* filename) {
    if (!filename) return "";
    
    String name(filename);
    
    // Remove .pcap extension
    int dotPos = name.lastIndexOf('.');
    if (dotPos > 0) {
        name = name.substring(0, dotPos);
    }
    
    // Handle legacy timestamp format: SSID_1234567890
    int underscorePos = name.lastIndexOf('_');
    if (underscorePos > 0) {
        // Check if everything after underscore is digits (timestamp)
        String suffix = name.substring(underscorePos + 1);
        bool isTimestamp = true;
        for (size_t i = 0; i < suffix.length(); i++) {
            if (!isdigit(suffix[i])) {
                isTimestamp = false;
                break;
            }
        }
        if (isTimestamp && suffix.length() >= 8) {
            name = name.substring(0, underscorePos);
        }
    }
    
    return name;
}

WpaSecStatus WpaSecService::getStatus(const char* filename) const {
#ifdef ESP32
    if (!filename) return WpaSecStatus::NOT_UPLOADED;
    
    // Extract SSID and lookup in registry summaries (O(n) but fast for small N)
    String ssid = extractSSID(filename);
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
    for (const auto& s : summaries) {
        if (strcmp(s.ssid, ssid.c_str()) == 0) {
            return s.wpaSecStatus;
        }
    }
    return WpaSecStatus::NOT_UPLOADED;
#else
    (void)filename;
    return WpaSecStatus::NOT_UPLOADED;
#endif
}

void WpaSecService::setStatus(const char* filename, WpaSecStatus status) {
#ifdef ESP32
    if (!filename) return;
    
    // Extract SSID and update registry (persisted to manifest.bin)
    String ssid = extractSSID(filename);
    CaptureRegistry::getInstance().setWpaSecStatus(ssid.c_str(), status);
#else
    (void)filename;
    (void)status;
#endif
}

const char* WpaSecService::getCrackedPassword(const char* filename) const {
#ifdef ESP32
    if (!filename) return nullptr;
    
    // Load password from metadata JSON (on-demand, not cached)
    const HandshakeMetadata* meta = CaptureRegistry::getInstance().getMetadata(filename);
    if (meta && meta->wpaSecStatus == WpaSecStatus::CRACKED && meta->wpaSecPassword[0] != '\0') {
        return meta->wpaSecPassword;
    }
    return nullptr;
#else
    (void)filename;
    return nullptr;
#endif
}

} // namespace adversary
