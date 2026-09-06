/**
 * @file pwncrack_service.cpp
 * @brief pwncrack.org cloud cracking service implementation
 */

#include "pwncrack_service.h"
#include <memory>
#include "../storage/settings_manager.h"
#include "../storage/capture_registry.h"
#include "tls_upload.h"

#ifndef ESP32
#include "../../test/common/arduino_mocks.h"
#endif

#include "../../ui/components/toast_manager.h"
#include "../../config/config.h"

#if defined(ESP32) || defined(ARDUINO)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include "../wifi/wifi_connection.h"
#endif

namespace adversary {

// API key check helper (referenced from the header's hasApiKey()).
bool _pwncrackHasApiKey() {
    return SettingsManager::getInstance().hasPwncrackKey();
}

// Derive the capture SSID from a .22000 path (strip directory + extension).
static String pwncrackDeriveSSID(const char* filepath) {
    const char* basename = strrchr(filepath, '/');
    basename = basename ? basename + 1 : filepath;
    String ssid(basename);
    int dot = ssid.lastIndexOf('.');
    if (dot > 0) ssid = ssid.substring(0, dot);
    return ssid;
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

bool PwncrackService::uploadHandshake(const char* filepath, bool quiet) {
#ifdef ESP32
    if (!hasApiKey()) {
        showErrorToast("No pwncrack key");
        return false;
    }

    String ssid = pwncrackDeriveSSID(filepath);

    // Skip if already uploaded/cracked (pure RAM lookup, no SD).
    WpaSecStatus current = getStatus(ssid.c_str());
    if (current == WpaSecStatus::UPLOADED || current == WpaSecStatus::CRACKED) {
        if (!quiet) showToast("pwncrack: already sent");
        return true;
    }

    // Open briefly only for size, then close before TLS allocation.
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        showErrorToast("No .22000 file");
        return false;
    }
    size_t fileSize = file.size();
    file.close();

    // pwncrack rejects >100KB; an empty file is nothing to send.
    if (fileSize == 0 || fileSize > 100 * 1024) {
        showErrorToast("Bad .22000 size");
        return false;
    }

    const char* apiKey = SettingsManager::getInstance().getPwncrackKey();

    if (!WiFi.isConnected()) {
        showErrorToast("WiFi connection required");
        return false;
    }
    if (!tls_upload::timeSyncedForTls()) {
        showErrorToast("Time not synced");
        Serial.println("[pwncrack] Clock not NTP/GPS-synced — skipping");
        return false;
    }
    if (tls_upload::heapTooLowForTls()) {
        showErrorToast("Low heap, try again");
        Serial.println("[pwncrack] Heap too low for TLS — skipping");
        return false;
    }

    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(15000);

    if (!tls_upload::connectWithRetry(client, PWNCRACK_HOST, PWNCRACK_PORT, "pwncrack")) {
        showErrorToast("Connection failed");
        Serial.printf("[pwncrack] connect failed (heap: %lu)\n", ESP.getFreeHeap());
        return false;
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
        showErrorToast("Cannot reopen file");
        return false;
    }

    char streamErr[48] = {0};
    if (!tls_upload::streamFile(client, file, fileSize, "pwncrack", streamErr, sizeof(streamErr))) {
        showErrorToast(streamErr[0] ? streamErr : "Upload failed");
        return false;
    }

    client.print(footer);

    // Read response.
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 10000) {
            client.stop();
            showErrorToast("Upload timeout");
            return false;
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

    Serial.println("[pwncrack] FULL upload response:");
    Serial.println(response.c_str());
    Serial.println("[pwncrack] END response");

    String lower = response;
    lower.toLowerCase();
    bool looksError = (lower.indexOf("error") != -1) ||
                      (lower.indexOf("invalid") != -1) ||
                      (lower.indexOf("denied") != -1) ||
                      (lower.indexOf("unauthorized") != -1);

    // Accept on 2xx without an error keyword. The body wording isn't documented,
    // so we log the full response above for refinement during field testing.
    if (httpCode >= 200 && httpCode < 300 && !looksError) {
        CaptureRegistry::getInstance().setPwncrackStatus(ssid.c_str(), WpaSecStatus::UPLOADED);
        if (!quiet) showSuccessToast("Sent to pwncrack!");
        return true;
    }

    char msg[40];
    snprintf(msg, sizeof(msg), "pwncrack err %d", httpCode);
    showErrorToast(msg);
    return false;
#else
    (void)filepath;
    return false;
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
        Serial.println("[pwncrack] Clock not NTP/GPS-synced — skipping results fetch");
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
        Serial.printf("[pwncrack] results fetch failed: HTTP %d (%s)\n",
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

    // Heap-allocated temp buffer; was static local (3KB BSS always).
    struct CrackedEntry { char ssid[33]; char password[64]; uint8_t bssid[6]; };
    std::unique_ptr<CrackedEntry[]> crackedBuffer(new (std::nothrow) CrackedEntry[32]);
    if (!crackedBuffer) { http.end(); return 0; }
    int crackedBufferCount = 0;

    auto parseBssid = [](const char* s, uint8_t out[6]) -> bool {
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

    // Potfile line: colon-delimited, BSSID is the 2nd field, password is
    // everything after the 4th colon (passwords may contain ':').
    char lineBuffer[256];
    int lineLen = 0;
    int downloadedCount = 0;

    auto parseLine = [&](char* line) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0) return;

        // Locate the first four colons.
        int colon[4];
        int found = 0;
        for (int i = 0; i < len && found < 4; ++i) {
            if (line[i] == ':') colon[found++] = i;
        }
        if (found < 4) return;  // not a result row

        // field[1] = BSSID (between colon[0] and colon[1]); password = after colon[3].
        char bssidStr[32] = {0};
        int bl = colon[1] - colon[0] - 1;
        if (bl > 0 && bl < (int)sizeof(bssidStr)) {
            strncpy(bssidStr, line + colon[0] + 1, bl);
            bssidStr[bl] = '\0';
        }
        const char* password = line + colon[3] + 1;
        // field[0] = best-effort SSID/essid for the fallback match.
        char ssid0[33] = {0};
        if (colon[0] > 0 && colon[0] < (int)sizeof(ssid0)) {
            strncpy(ssid0, line, colon[0]);
            ssid0[colon[0]] = '\0';
        }

        if (password[0] == '\0') return;
        downloadedCount++;
        if (crackedBufferCount < 32) {
            strncpy(crackedBuffer[crackedBufferCount].ssid, ssid0, 32);
            crackedBuffer[crackedBufferCount].ssid[32] = '\0';
            strncpy(crackedBuffer[crackedBufferCount].password, password, 63);
            crackedBuffer[crackedBufferCount].password[63] = '\0';
            if (!parseBssid(bssidStr, crackedBuffer[crackedBufferCount].bssid)) {
                memset(crackedBuffer[crackedBufferCount].bssid, 0, 6);
            }
            crackedBufferCount++;
        }
        Serial.printf("[pwncrack] Cracked: %s / %s\n", bssidStr, password);
    };

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
            timeout = millis();
        } else {
            if (millis() - timeout > 5000) break;
            delay(10);
        }
    }
    if (lineLen > 0) {
        lineBuffer[lineLen] = '\0';
        parseLine(lineBuffer);
    }

    http.end();  // heap available again

    int appliedCount = 0;
    if (crackedBufferCount > 0) {
        auto& registry = CaptureRegistry::getInstance();
        for (int i = 0; i < crackedBufferCount; i++) {
            if (registry.setPwncrackCrackedPassword(crackedBuffer[i].bssid,
                                                    crackedBuffer[i].ssid,
                                                    crackedBuffer[i].password)) {
                appliedCount++;
            }
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
