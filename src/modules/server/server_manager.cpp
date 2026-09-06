/**
 * @file server_manager.cpp
 * @brief Server Manager implementation
 */

#include "server_manager.h"
#include "../storage/settings_manager.h"
#include "../storage/capture_registry.h"
#include "../../hal/storage/sd_manager.h"

#ifdef ESP32
#include <WiFi.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#endif

#include "../../config/config.h"
#include "../../utils/path_security.h"

namespace adversary {

ServerManager::ServerManager() 
    : running_(false)
    , startTime_(0)
    , requestCount_(0)
    , lastRequestTime_(0) {
    strncpy(ipAddress_, "0.0.0.0", 15);
    ipAddress_[15] = '\0';
}

bool ServerManager::start() {
    if (running_) return true;

#ifdef ESP32
    Serial.println("[Server] Starting server mode...");

    if (!setupAP()) {
        Serial.println("[Server] ERROR: AP setup failed");
        return false;
    }

    if (!setupMDNS()) {
        Serial.println("[Server] WARNING: mDNS setup failed");
    }

    if (!setupWebServer()) {
        Serial.println("[Server] ERROR: WebServer setup failed");
        stop();
        return false;
    }

    startTime_ = millis();
    requestCount_ = 0;
    lastRequestTime_ = 0;
    running_ = true;
    Serial.printf("[Server] Ready at http://%s.local or http://%s\n", 
                  SettingsManager::getInstance().get().system.deviceName,
                  getIPAddress());
#endif
    
    return true;
}

void ServerManager::stop() {
    if (!running_) return;

#ifdef ESP32
    Serial.println("[Server] Stopping server mode...");

    if (server_) {
        server_->stop();
        server_.reset();
    }
    if (dnsServer_) {
        dnsServer_->stop();
        dnsServer_.reset();
    }
    MDNS.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF); // Power down radio for max heap during transition
#endif

    running_ = false;
    startTime_ = 0;
    requestCount_ = 0;
    lastRequestTime_ = 0;
    strncpy(ipAddress_, "0.0.0.0", 15);
}

void ServerManager::update() {
    if (!running_) return;

#ifdef ESP32
    if (dnsServer_) {
        dnsServer_->processNextRequest();
    }
    if (server_) {
        server_->handleClient();
    }
#endif
}

bool ServerManager::setupAP() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance().get();
    
    // Configure AP
    WiFi.mode(WIFI_AP);
    
    // Explicit config for stability
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    
    // Use device name as SSID, no password for open AP
    if (!WiFi.softAP(settings.system.deviceName, nullptr)) {
        return false;
    }

    // Start DNS server on port 53 to redirect all queries to AP IP
    dnsServer_ = std::make_unique<DNSServer>();
    dnsServer_->start(53, "*", apIP);

    // Capture IP
    IPAddress myIP = WiFi.softAPIP();
    strncpy(ipAddress_, myIP.toString().c_str(), 15);
    ipAddress_[15] = '\0';
    
    return true;
#else
    return false;
#endif
}

bool ServerManager::setupMDNS() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance().get();
    
    // Sanitize hostname (alphanumeric only)
    String hostname = settings.system.deviceName;
    hostname.replace(" ", "-");
    String safeHostname = "";
    for(size_t i = 0; i < hostname.length(); i++) {
        char c = hostname[i];
        if(isalnum(c) || c == '-') safeHostname += c;
    }
    if(safeHostname.length() == 0) safeHostname = "adversary";

    if (!MDNS.begin(safeHostname.c_str())) {
        return false;
    }
    MDNS.addService("http", "tcp", 80);
    return true;
#else
    return false;
#endif
}

bool ServerManager::setupWebServer() {
#ifdef ESP32
    server_ = std::make_unique<WebServer>(80);

    // Set up routes
    server_->on("/", HTTP_GET, [this]() { handleRoot(); });
    
    // Redirect helper
    auto redirectRoot = [this]() {
        server_->sendHeader("Location", "/", true);
        server_->send(302, "text/plain", "");
    };

    // Captive portal redirects for common probes
    server_->on("/generate_204", redirectRoot);       // Android/Chrome
    server_->on("/redirect", redirectRoot);           // iPhone
    server_->on("/hotspot-detect.html", redirectRoot); // iOS/OSX
    server_->on("/canonical.html", redirectRoot);     // Android
    server_->on("/library/test/success.html", redirectRoot); // iOS/MacOS
    server_->on("/success.txt", [this]() { server_->send(200, "text/plain", "success"); }); // Android (Some versions need direct response)
    server_->on("/ncsi.txt", [this]() { server_->send(200, "text/plain", "Microsoft NCSI"); }); // Windows

    // API Routes
    server_->on("/api/info", HTTP_GET, [this]() { handleApiInfo(); });
    server_->on("/api/settings", HTTP_GET, [this]() { handleApiSettings(); });
    server_->on("/api/settings", HTTP_POST, [this]() { handleApiSettings(); });
    server_->on("/api/files/captures", HTTP_GET, [this]() { handleApiFilesCaptures(); });
    server_->on("/api/files/handshakes", HTTP_GET, [this]() { handleApiFilesHandshakes(); });
    server_->on("/api/files/packets", HTTP_GET, [this]() { handleApiFilesPackets(); });
    server_->on("/api/files/logs", HTTP_GET, [this]() { handleApiFilesLogs(); });
    server_->on("/api/files/wardriving", HTTP_GET, [this]() { handleApiFilesWardriving(); });

    // Note: Detail endpoint /api/files/handshakes/{file} is handled in onNotFound
    // dynamically to avoid complex route registration for every file

    server_->onNotFound([this]() { handleNotFound(); });

    server_->begin();
    return true;
#else
    return false;
#endif
}

void ServerManager::handleRoot() {
    requestCount_++;
    lastRequestTime_ = millis();

#ifdef ESP32
    // First-run guard: auth is enabled but no password has been set yet. Firing a
    // bare Basic-auth 401 here is a UX trap — cryptic on desktop, and Android
    // Chrome aborts it with ERR_HTTP_RESPONSE_CODE_FAILURE. Instead serve a page
    // telling the user to set a password on the device. Auth stays enforced on the
    // data/API routes (checkAuth() is unchanged), so this does not fail open.
    {
        auto& settings = SettingsManager::getInstance().get();
        if (settings.system.dashboardAuthEnabled && settings.system.dashboardPassword[0] == '\0') {
            server_->sendHeader("Connection", "close");
            server_->send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Adversary - Setup</title></head>"
                "<body style='background:#0c0c0c;color:#eee;font-family:sans-serif;text-align:center;padding:3rem 1.5rem;'>"
                "<h1 style='color:#ff3e3e;letter-spacing:2px;'>ADVERSARY</h1>"
                "<h2>Set a dashboard password</h2>"
                "<p>Authentication is on, but no password is set yet.</p>"
                "<p>On the device: <b>Settings &rarr; Dashboard &rarr; Password</b>, "
                "then reload and sign in (default username <code>admin</code>).</p>"
                "</body></html>");
            return;
        }
    }
#endif

    if (!checkAuth()) return;

#ifdef ESP32
    // Check if index.html exists on SD
    static const char* index_path = "/adversary/dashboard/index.html";
    if (SDManager::getInstance().fileExists(index_path)) {
        File file = SD.open(index_path, "r");
        if (file) {
            server_->streamFile(file, "text/html");
            file.close();
        } else {
             server_->send(500, "text/plain", "Failed to open index.html");
        }
    } else {
        server_->send(200, "text/html", 
            "<h1>Adversary Dashboard</h1>"
            "<p>Dashboard files missing on SD card.</p>"
            "<p>Please create <code>/adversary/dashboard/index.html</code></p>");
    }
#endif
}

void ServerManager::handleNotFound() {
    requestCount_++;
    lastRequestTime_ = millis();
#ifdef ESP32
    // Try to serve file from SD
    String path = urlDecode(server_->uri());
    if (path == "/") path = "/index.html";

    // Reject path traversal before joining to the dashboard base dir, otherwise
    // "/../config/settings.json" etc. would escape and read arbitrary SD files.
    if (!path_security::isSafeWebPath(path.c_str())) {
        Serial.printf("[Server] Rejected unsafe path: %s\n", path.c_str());
        server_->send(400, "text/plain", "Bad Request");
        return;
    }

    String fullPath = "/adversary/dashboard" + path;

    // Check if it's a dashboard file request
    if (SDManager::getInstance().fileExists(fullPath.c_str())) {
        // Require auth for dashboard files
        if (!checkAuth()) return;
        
        String contentType = "text/plain";
        if (path.endsWith(".html")) contentType = "text/html";
        else if (path.endsWith(".css")) contentType = "text/css";
        else if (path.endsWith(".js")) contentType = "application/javascript";
        else if (path.endsWith(".png")) contentType = "image/png";
        else if (path.endsWith(".jpg")) contentType = "image/jpeg";
        else if (path.endsWith(".ico")) contentType = "image/x-icon";
        
        File file = SD.open(fullPath.c_str(), "r");
        if (file) {
            server_->streamFile(file, contentType);
            file.close();
            return;
        }
    }
    
    String uri = server_->uri();
    
    // If it's an API request, require auth and return JSON 404
    if (uri.startsWith("/api/")) {
        if (!checkAuth()) return;
        
        // Special case: Dynamic handshake detail endpoint
        if (uri.startsWith("/api/files/handshakes/")) {
            handleApiHandshakeDetail();
            return;
        }
        
        // Special case: Dynamic wardriving detail endpoint
        if (uri.startsWith("/api/files/wardriving/")) {
            handleApiWardrivingDetail();
            return;
        }
        
        server_->send(404, "application/json", "{\"error\":\"API Not Found\"}");
        return;
    }

    // Captive portal / connectivity check - show login page if auth enabled, else redirect to root
    auto& settings = SettingsManager::getInstance().get();
    String host = server_->hostHeader();
    Serial.printf("[Server] Captive portal: %s (Host: %s)\n", uri.c_str(), host.c_str());
    
    if (settings.system.dashboardAuthEnabled) {
        // Serve login landing page
        if (SDManager::getInstance().fileExists("/adversary/dashboard/login.html")) {
            File file = SD.open("/adversary/dashboard/login.html", "r");
            if (file) {
                server_->streamFile(file, "text/html");
                file.close();
                return;
            }
        }
        // Fallback if login.html doesn't exist
        server_->send(200, "text/html",
            "<html><body style='background:#0c0c0c;color:#fff;font-family:sans-serif;text-align:center;padding:3rem;'>"
            "<h1 style='color:#ff3e3e'>ADVERSARY</h1>"
            "<p>Authentication required. <a href='/' style='color:#ff3e3e'>Sign In</a></p>"
            "</body></html>");
        return;
    }
    
    // No auth - just redirect to dashboard
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
#endif
}

void ServerManager::handleApiInfo() {
    if (!checkAuth()) return;

#ifdef ESP32
    StaticJsonDocument<512> doc;
    doc["version"] = esp_ota_get_app_description()->version;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["psram"] = ESP.getFreePsram();
    doc["battery"] = -1; // Battery sense not supported on Cardputer
    
    SDCardInfo sd = SDManager::getInstance().getCardInfo();
    JsonObject sdObj = doc.createNestedObject("sd");
    sdObj["total"] = sd.totalBytes;
    sdObj["used"] = sd.usedBytes;
    sdObj["free"] = sd.freeBytes;
    sdObj["type"] = sd.type;

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["stations"] = WiFi.softAPgetStationNum();
    wifi["ip"] = WiFi.softAPIP().toString();

    String response;
    serializeJson(doc, response);
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json", response);
#endif
}

void ServerManager::handleApiSettings() {
    if (!checkAuth()) return;

#ifdef ESP32
    if (server_->method() == HTTP_GET) {
        auto& settings = SettingsManager::getInstance().get();
        StaticJsonDocument<1024> doc;
        
        JsonObject system = doc.createNestedObject("system");
        system["deviceName"] = settings.system.deviceName;
        system["dashboardAuthEnabled"] = settings.system.dashboardAuthEnabled;
        system["dashboardUsername"] = settings.system.dashboardUsername;
        system["theme"] = settings.display.themePreset;
        
        JsonObject wireless = doc.createNestedObject("wireless");
        wireless["karmaChannel"] = settings.wireless.karmaChannel;
        wireless["bleName"] = settings.wireless.bleName;

        JsonObject api = doc.createNestedObject("api");
        api["wpasec"] = settings.apiKeys.wpasec;
        api["wigle"] = settings.apiKeys.wigle;
        api["pwncrack"] = settings.apiKeys.pwncrack;

        String response;
        serializeJson(doc, response);
        server_->sendHeader("Connection", "close");
        server_->send(200, "application/json", response);
    } 
    else if (server_->method() == HTTP_POST) {
        if (server_->hasArg("plain")) {
            String body = server_->arg("plain");
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                server_->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            auto& settings = SettingsManager::getInstance().getMutable();
            
            if (doc.containsKey("system")) {
                JsonObject system = doc["system"];
                if (system.containsKey("deviceName")) {
                    strncpy(settings.system.deviceName, system["deviceName"], 32);
                    settings.system.deviceName[32] = '\0';
                }
                if (system.containsKey("dashboardAuthEnabled")) {
                    settings.system.dashboardAuthEnabled = system["dashboardAuthEnabled"];
                }
                if (system.containsKey("dashboardUsername")) {
                    strncpy(settings.system.dashboardUsername, system["dashboardUsername"], 32);
                    settings.system.dashboardUsername[32] = '\0';
                }
                if (system.containsKey("dashboardPassword")) {
                    strncpy(settings.system.dashboardPassword, system["dashboardPassword"], 32);
                    settings.system.dashboardPassword[32] = '\0';
                }
                if (system.containsKey("theme")) {
                    settings.display.themePreset = system["theme"].as<uint8_t>();
                }
            }

            if (doc.containsKey("wireless")) {
                JsonObject wireless = doc["wireless"];
                if (wireless.containsKey("karmaChannel")) {
                    settings.wireless.karmaChannel = wireless["karmaChannel"];
                }
                if (wireless.containsKey("bleName")) {
                    strncpy(settings.wireless.bleName, wireless["bleName"], 32);
                    settings.wireless.bleName[32] = '\0';
                }
            }

            if (doc.containsKey("api")) {
                JsonObject api = doc["api"];
                if (api.containsKey("wpasec")) {
                    SettingsManager::getInstance().setWpaSecKey(api["wpasec"]);
                }
                if (api.containsKey("wigle")) {
                    SettingsManager::getInstance().setWigleKey(api["wigle"]);
                }
                if (api.containsKey("pwncrack")) {
                    SettingsManager::getInstance().setPwncrackKey(api["pwncrack"]);
                }
            }

            SettingsManager::getInstance().save();
            server_->sendHeader("Connection", "close");
            server_->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            server_->send(400, "application/json", "{\"error\":\"Missing body\"}");
        }
    }
#endif
}

void ServerManager::handleApiFilesCaptures() {
    serveFiles(config::SD_CAPTURES_PATH, "Capture");
}

void ServerManager::handleApiFilesHandshakes() {
    if (!checkAuth()) return;

#ifdef ESP32
    // Use in-memory registry instead of re-reading SD card. The registry is the
    // single source of truth the firmware uses, so serving wpaSecStatus from here
    // (not the per-file JSON) keeps the dashboard list consistent with the device.
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();

    // Size for the entry count (the old StaticJsonDocument<2048> silently
    // truncated once there were more than ~40 handshakes).
    DynamicJsonDocument doc(1024 + summaries.size() * 96);
    JsonArray files = doc.to<JsonArray>();

    for (const auto& summary : summaries) {
        JsonObject fileObj = files.createNestedObject();
        fileObj["name"] = summary.ssid;  // SSID is our display name
        fileObj["size"] = summary.size;
        fileObj["wpaSecStatus"] = static_cast<uint8_t>(summary.wpaSecStatus);
        fileObj["pwncrackStatus"] = static_cast<uint8_t>(summary.pwncrackStatus);
        fileObj["hasGPS"] = summary.hasGPS;
    }
    
    String response;
    serializeJson(doc, response);
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json", response);
#endif
}

void ServerManager::handleApiFilesPackets() {
    serveFiles(config::SD_PACKETS_PATH, "Packet");
}

void ServerManager::handleApiFilesLogs() {
    serveFiles(config::SD_LOGS_PATH, "Log");
}

void ServerManager::handleApiFilesWardriving() {
    serveFiles(config::SD_WARDRIVING_PATH, "Wardriving", false, ".csv");
}

void ServerManager::serveFiles(const char* path, const char* category, bool minimal, const char* extension) {
    if (!checkAuth()) return;

#ifdef ESP32
    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        server_->send(200, "application/json", "[]");
        return;
    }

    DynamicJsonDocument doc(4096);
    JsonArray files = doc.to<JsonArray>();

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fullName = String(file.name());
            
            // Extract basename (handle full paths if returned)
            int lastSlash = fullName.lastIndexOf('/');
            String name = (lastSlash >= 0) ? fullName.substring(lastSlash + 1) : fullName;
            
            bool match = true;
            if (extension) {
                match = name.endsWith(extension);
                if (match) {
                    // Strip extension for the "name" field
                    name = name.substring(0, name.length() - strlen(extension));
                }
            }

            if (match) {
                JsonObject fileObj = files.createNestedObject();
                fileObj["name"] = name;
                fileObj["size"] = file.size();
                
                if (!minimal) {
                    fileObj["path"] = fullName; // Use the full path for internal references
                    fileObj["category"] = category;
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();

    String response;
    serializeJson(doc, response);
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json", response);
#endif
}

void ServerManager::handleApiHandshakeDetail() {
    if (!checkAuth()) return;

#ifdef ESP32
    String uri = server_->uri();
    String filename = urlDecode(uri.substring(String("/api/files/handshakes/").length()));
    
    if (filename.length() == 0) {
        server_->send(400, "application/json", "{\"error\":\"Missing filename\"}");
        return;
    }

    // The UI requests "{name}.json" by convention, but the per-file JSON sidecars
    // were retired in the storage rework. Serve the detail from the BSSID-keyed
    // manifest (the single source of truth) so the dashboard matches the device
    // exactly — the stale sidecars used to show NOT_UPLOADED after a sync.
    if (filename.endsWith(".json")) {
        filename = filename.substring(0, filename.length() - 5);
    }
    int lastSlash = filename.lastIndexOf('/');  // strip any path component
    if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
    }

    // getMetadata() keys off the SSID; pass "{ssid}.pcap" so an SSID ending in a
    // dot survives the extension strip.
    String pcapName = filename + ".pcap";
    const HandshakeMetadata* meta = CaptureRegistry::getInstance().getMetadata(pcapName.c_str());
    if (!meta) {
        Serial.printf("[Server] Detail not in manifest: %s\n", filename.c_str());
        server_->send(404, "application/json", "{\"error\":\"Not found\"}");
        return;
    }

    DynamicJsonDocument doc(1024);
    doc["ssid"] = meta->ssid;

    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             meta->bssid[0], meta->bssid[1], meta->bssid[2],
             meta->bssid[3], meta->bssid[4], meta->bssid[5]);
    doc["bssid"] = bssidStr;

    doc["channel"] = meta->channel;
    doc["type"] = meta->type;
    doc["capturedAt"] = meta->capturedAt;
    doc["quality"] = meta->quality;
    doc["signalStrength"] = meta->signalStrength;

    doc["wpaSecStatus"] = static_cast<uint8_t>(meta->wpaSecStatus);
    if (meta->wpaSecPassword[0]) doc["wpaSecPassword"] = meta->wpaSecPassword;

    doc["pwncrackStatus"] = static_cast<uint8_t>(meta->pwncrackStatus);
    if (meta->pwncrackPassword[0]) doc["pwncrackPassword"] = meta->pwncrackPassword;

    if (meta->hasGPS) {
        JsonObject gps = doc.createNestedObject("gps");
        gps["latitude"] = meta->latitude;
        gps["longitude"] = meta->longitude;
        gps["altitude"] = meta->altitude;
        gps["satellites"] = meta->satellites;
    }

    String response;
    serializeJson(doc, response);
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json; charset=utf-8", response);
#endif
}

void ServerManager::handleApiWardrivingDetail() {
    if (!checkAuth()) return;

#ifdef ESP32
    String uri = server_->uri();
    String filename = urlDecode(uri.substring(String("/api/files/wardriving/").length()));
    
    if (filename.length() == 0) {
        server_->send(400, "application/json", "{\"error\":\"Missing filename\"}");
        return;
    }

    // Ensure .csv extension
    if (!filename.endsWith(".csv")) {
        filename += ".csv";
    }

    // Secure path: strip directory traversal
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
    }

    String fullPath = String(config::SD_WARDRIVING_PATH) + "/" + filename;
    
    if (!SD.exists(fullPath)) {
        Serial.printf("[Server] Wardriving 404: %s\n", fullPath.c_str());
        server_->send(404, "application/json", "{\"error\":\"File not found\"}");
        return;
    }

    File file = SD.open(fullPath, "r");
    if (!file) {
        server_->send(500, "application/json", "{\"error\":\"Failed to open file\"}");
        return;
    }

    server_->streamFile(file, "text/csv; charset=utf-8");
    file.close();
#endif
}

String ServerManager::urlDecode(String str) {
    String decoded = "";
    char temp[] = "00";
    for (unsigned int i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                temp[0] = str[i + 1];
                temp[1] = str[i + 2];
                decoded += (char)strtol(temp, NULL, 16);
                i += 2;
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

bool ServerManager::checkAuth() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance().get();
    if (!settings.system.dashboardAuthEnabled) return true;

    if (!server_->authenticate(settings.system.dashboardUsername, settings.system.dashboardPassword)) {
        server_->requestAuthentication(BASIC_AUTH, "Adversary Dashboard");
        return false;
    }
    return true;
#else
    return true;
#endif
}

int ServerManager::getConnectedStations() const {
#ifdef ESP32
    return WiFi.softAPgetStationNum();
#else
    return 0;
#endif
}

const char* ServerManager::getIPAddress() const {
    return ipAddress_;
}

uint32_t ServerManager::getUptime() const {
    if (!running_) return 0;
    return (millis() - startTime_) / 1000;
}

} // namespace adversary
