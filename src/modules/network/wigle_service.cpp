/**
 * @file wigle_service.cpp
 * @brief WiGLE.net upload service implementation
 * 
 * Memory-conscious upload flow:
 * 1. Open SD file briefly to get size, then close
 * 2. Allocate SSL, connect to WiGLE API
 * 3. Re-open file and stream in 512-byte chunks
 * 4. Parse response and create sidecar marker on success
 */

#include "wigle_service.h"
#include "../storage/settings_manager.h"
#include "../../ui/components/toast_manager.h"
#include "../../config/config.h"
#include "tls_upload.h"

#if defined(ESP32) || defined(ARDUINO)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include "../wifi/wifi_connection.h"
#elif defined(UNIT_TEST)
#include "../../test/common/arduino_mocks.h"
#include "../../test/common/SD.h"
#endif

#include "../wardriving/wardriving_config.h"

namespace adversary {

bool WigleService::hasApiKey() const {
    return SettingsManager::getInstance().hasWigleKey();
}

void WigleService::getSidecarPath(const char* filename, char* outPath, size_t outSize) {
    // Build path: /adversary/captures/wardriving/filename.wigle
    // Strip .csv extension and append .wigle
    char baseName[64];
    strncpy(baseName, filename, sizeof(baseName) - 1);
    baseName[sizeof(baseName) - 1] = '\0';
    
    char* ext = strstr(baseName, ".csv");
    if (ext) *ext = '\0';
    
    snprintf(outPath, outSize, "%s/%s.wigle", wardriving::WARDRIVING_DIR, baseName);
}

bool WigleService::createSidecar(const char* filename) {
#if defined(ESP32) || defined(UNIT_TEST)
    char sidecarPath[128];
    getSidecarPath(filename, sidecarPath, sizeof(sidecarPath));
    
    File marker = SD.open(sidecarPath, FILE_WRITE);
    if (!marker) {
        Serial.printf("[WiGLE] Failed to create sidecar: %s\n", sidecarPath);
        return false;
    }
    marker.println("uploaded");
    marker.close();
    Serial.printf("[WiGLE] Created sidecar: %s\n", sidecarPath);
    return true;
#else
    (void)filename;
    return false;
#endif
}

WigleStatus WigleService::getStatus(const char* filename) const {
#if defined(ESP32) || defined(UNIT_TEST)
    if (!filename) return WigleStatus::NOT_UPLOADED;
    
    char sidecarPath[128];
    getSidecarPath(filename, sidecarPath, sizeof(sidecarPath));
    
    return SD.exists(sidecarPath) ? WigleStatus::UPLOADED : WigleStatus::NOT_UPLOADED;
#else
    (void)filename;
    return WigleStatus::NOT_UPLOADED;
#endif
}

bool WigleService::uploadCSV(const char* filepath) {
#ifdef ESP32
    if (!hasApiKey()) {
        showErrorToast("No WiGLE key");
        return false;
    }
    
    // Get filename from path
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    Serial.printf("[WiGLE] Upload starting: %s\n", filename);
    
    // Check if already uploaded (sidecar exists)
    if (getStatus(filename) == WigleStatus::UPLOADED) {
        showToast("Already uploaded");
        return true;
    }
    
    // Open file briefly to get size, then close before SSL allocation
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        showErrorToast("Cannot open file");
        return false;
    }
    
    size_t fileSize = file.size();
    
    if (fileSize == 0 || fileSize > 10 * 1024 * 1024) {  // Max 10MB
        file.close();
        showErrorToast("Invalid file size");
        return false;
    }
    
    file.close();  // *** CRITICAL: Close before SSL allocation ***
    
    // Get API key
    const char* apiKey = SettingsManager::getInstance().getWigleKey();
    
    // Check WiFi
    if (!WiFi.isConnected()) {
        showErrorToast("WiFi required");
        return false;
    }

    // No RTC: refuse TLS on a stale clock so a legit cert's dates still validate.
    if (!tls_upload::timeSyncedForTls()) {
        showErrorToast("Time not synced");
        Serial.println("[WiGLE] Clock not NTP/GPS-synced — skipping (retry next sync)");
        return false;
    }

    // Gate on contiguous heap before allocating TLS buffers (fail fast / retry).
    if (tls_upload::heapTooLowForTls()) {
        showErrorToast("Low heap, try again");
        Serial.println("[WiGLE] Heap too low for TLS — skipping (retry next sync)");
        return false;
    }

    // Create HTTPS client — validate against the embedded root-CA bundle so the
    // WiGLE Basic auth header can't be MITM'd.
    WiFiClientSecure client;
    tls_upload::applyCaBundle(client);
    client.setTimeout(15000);
    
    Serial.printf("[WiGLE] Connecting to %s:%d (heap: %lu)\n", 
                  WIGLE_HOST, WIGLE_PORT, ESP.getFreeHeap());
    
    if (!tls_upload::connectWithRetry(client, WIGLE_HOST, WIGLE_PORT, "WiGLE")) {
        showErrorToast("Connection failed");
        Serial.printf("[WiGLE] Connect failed (heap: %lu)\n", ESP.getFreeHeap());
        return false;
    }
    
    // Build multipart form data
    String boundary = "----AdversaryWigleBoundary";
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"\r\n";
    header += "Content-Type: text/csv\r\n\r\n";
    String footer = "\r\n--" + boundary + "--\r\n";
    
    size_t contentLength = header.length() + fileSize + footer.length();
    
    // Send HTTP request
    client.print("POST " + String(WIGLE_UPLOAD_PATH) + " HTTP/1.1\r\n");
    client.print("Host: " + String(WIGLE_HOST) + "\r\n");
    client.print("Authorization: Basic " + String(apiKey) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(contentLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // Send multipart header
    client.print(header);
    
    // Re-open file for streaming
    file = SD.open(filepath, FILE_READ);
    if (!file) {
        client.stop();
        showErrorToast("Cannot reopen file");
        return false;
    }
    
    // Heap-paced upload. On failure the helper closes the file + client and we
    // return without creating the sidecar marker, so the file is retried later.
    char streamErr[48] = {0};
    if (!tls_upload::streamFile(client, file, fileSize, "WiGLE", streamErr, sizeof(streamErr))) {
        showErrorToast(streamErr[0] ? streamErr : "Upload failed");
        return false;
    }

    // Send footer
    client.print(footer);
    
    // Wait for response
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 15000) {
            client.stop();
            showErrorToast("Upload timeout");
            return false;
        }
        delay(10);
    }
    
    // Read response
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
        // Limit response buffer to prevent OOM
        if (response.length() > 2048) break;
    }
    client.stop();
    
    Serial.println("[WiGLE] Response received:");
    Serial.println(response.substring(0, 500));
    
    // Check for success
    // WiGLE API v2 returns JSON: {"success": true, ...}
    bool success = false;
    
    if (response.indexOf("\"success\":true") != -1 || 
        response.indexOf("\"success\": true") != -1) {
        success = true;
    }
    
    // Also check HTTP status code
    if (response.startsWith("HTTP/1.1 200") || response.startsWith("HTTP/1.0 200")) {
        // 200 OK with success in body
        if (success) {
            createSidecar(filename);
            WiFiConnection::getInstance().disconnect();
            showSuccessToast("Uploaded to WiGLE!");
            return true;
        }
    }
    
    // Handle errors
    // Extract body for error message
    int bodyStart = response.indexOf("\r\n\r\n");
    if (bodyStart != -1) {
        String body = response.substring(bodyStart + 4);
        body.trim();
        
        Serial.println("[WiGLE] Response body:");
        Serial.println(body.substring(0, 500));
        
        // Try to extract error message
        int msgStart = body.indexOf("\"message\":");
        if (msgStart != -1) {
            int quoteStart = body.indexOf('"', msgStart + 10);
            int quoteEnd = body.indexOf('"', quoteStart + 1);
            if (quoteStart >= 0 && quoteEnd > quoteStart) {
                String errMsg = body.substring(quoteStart + 1, quoteEnd);
                if (errMsg.length() > 35) errMsg = errMsg.substring(0, 32) + "...";
                showErrorToast(errMsg.c_str());
                return false;
            }
        }
    }
    
    // Check for auth errors
    if (response.startsWith("HTTP/1.1 401") || response.startsWith("HTTP/1.0 401")) {
        showErrorToast("Invalid WiGLE key");
        return false;
    }
    
    if (response.startsWith("HTTP/1.1 429") || response.startsWith("HTTP/1.0 429")) {
        showErrorToast("Rate limited");
        return false;
    }
    
    showErrorToast("Upload failed");
    return false;
#else
    (void)filepath;
    return false;
#endif
}

} // namespace adversary
