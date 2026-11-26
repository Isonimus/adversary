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
#include <esp_app_desc.h>
#include "utils/log.h"
#endif

#include "../../config/config.h"
#include "dashboard_assets.h"
#include "../../utils/wifi_password.h"

#ifdef ESP32
#include <esp_random.h>
#endif

namespace adversary {

#ifdef ESP32
// Dashboard shell embedded into the image via board_build.embed_files (ADR-0168,
// slice-0169). The _binary_<path>_start/_end symbols are linker-emitted and bound
// each file's bytes in flash .rodata; same idiom as the CA bundle in tls_upload.cpp.
// Symbol names derive from the embed path (dashboard_dev/<file>, non-alnum -> '_').
extern const uint8_t dashboard_index_start[] asm("_binary_dashboard_dev_index_html_start");
extern const uint8_t dashboard_index_end[]   asm("_binary_dashboard_dev_index_html_end");
extern const uint8_t dashboard_js_start[]     asm("_binary_dashboard_dev_dashboard_js_start");
extern const uint8_t dashboard_js_end[]       asm("_binary_dashboard_dev_dashboard_js_end");
extern const uint8_t dashboard_css_start[]    asm("_binary_dashboard_dev_index_css_start");
extern const uint8_t dashboard_css_end[]      asm("_binary_dashboard_dev_index_css_end");
extern const uint8_t dashboard_login_start[]  asm("_binary_dashboard_dev_login_html_start");
extern const uint8_t dashboard_login_end[]    asm("_binary_dashboard_dev_login_html_end");

namespace {
const DashboardAsset kDashboardAssets[] = {
    {"/index.html",   dashboard_index_start, dashboard_index_end, "text/html"},
    {"/dashboard.js", dashboard_js_start,    dashboard_js_end,    "application/javascript"},
    {"/index.css",    dashboard_css_start,   dashboard_css_end,   "text/css"},
    {"/login.html",   dashboard_login_start, dashboard_login_end, "text/html"},
};
constexpr size_t kDashboardAssetCount = sizeof(kDashboardAssets) / sizeof(kDashboardAssets[0]);
}  // namespace
#endif

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
    LOG_I("SERVER", "Starting server mode...");

    if (!setupAP()) {
        LOG_E("SERVER", "AP setup failed");
        return false;
    }

    if (!setupMDNS()) {
        LOG_W("SERVER", "mDNS setup failed");
    }

    if (!setupWebServer()) {
        LOG_E("SERVER", "WebServer setup failed");
        stop();
        return false;
    }

    startTime_ = millis();
    requestCount_ = 0;
    lastRequestTime_ = 0;
    running_ = true;
    LOG_I("SERVER", "Ready at http://%s.local or http://%s",
          SettingsManager::getInstance().get().system.deviceName,
          getIPAddress());
#endif
    
    return true;
}

void ServerManager::stop() {
    if (!running_) return;

#ifdef ESP32
    LOG_I("SERVER", "Stopping server mode...");

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

bool ServerManager::ensureApKey() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance().getMutable();
    if (settings.system.dashboardApPassword[0] != '\0') {
        return true;  // already provisioned
    }
    // WPA2-protect the dashboard AP. Auto-generate a per-device key the first time:
    // the operator's management plane is never brought up open, which would otherwise
    // leak the Basic-auth header and reviewed captures over the air (slice-0036). The
    // key is persisted so a client's saved network keeps working across reboots.
    uint8_t entropy[utils::kDashboardApPasswordChars];
    esp_fill_random(entropy, sizeof(entropy));
    if (!utils::fillWifiPassword(settings.system.dashboardApPassword,
                                 sizeof(settings.system.dashboardApPassword),
                                 utils::kDashboardApPasswordChars,
                                 entropy, sizeof(entropy))) {
        return false;  // never fall through to an open AP
    }
    SettingsManager::getInstance().save();
    return true;
#else
    return false;
#endif
}

bool ServerManager::setupAP() {
#ifdef ESP32
    auto& settings = SettingsManager::getInstance().get();

    // Configure AP
    WiFi.mode(WIFI_AP);

    // Explicit config for stability
    IPAddress apIP;
    apIP.fromString(kDashboardIp);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    // Defensive: the UI provisions the key before purging the canvas, but guard here
    // too so a server start from any path is never open (slice-0036).
    if (!ensureApKey()) {
        return false;
    }

    if (!WiFi.softAP(settings.system.deviceName, settings.system.dashboardApPassword)) {
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

    // The arduino-esp32 WebServer drops request headers that were not asked
    // for; without this, the Cookie header never reaches the handlers and
    // session auth would always see "no cookie" (ADR-0170).
    const char* kTrackedHeaders[] = {"Cookie"};
    server_->collectHeaders(kTrackedHeaders, 1);

    // Form-based session login (ADR-0170, slice-0171). These three routes are
    // the public surface — they must NOT go through checkAuth(), or we loop
    // the operator back to the login page.
    server_->on("/api/login",  HTTP_POST, [this]() { handleLogin(); });
    server_->on("/api/logout", HTTP_POST, [this]() { handleLogout(); });

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
    // The dashboard index is embedded in flash (ADR-0168, slice-0169), so it is
    // always present; no SD read and no "files missing" fallback.
    const DashboardAsset* index =
        resolveDashboardAsset(kDashboardAssets, kDashboardAssetCount, "/index.html");
    sendDashboardAsset(*index);
#endif
}

#ifdef ESP32
void ServerManager::sendDashboardAsset(const DashboardAsset& asset) {
    // send_P with an explicit length: embed_files bytes are not null-terminated,
    // so a C-string overload would overrun. The bytes are flash-mapped .rodata.
    server_->send_P(200, asset.contentType,
                    reinterpret_cast<const char*>(asset.start),
                    dashboardAssetLen(asset));
}
#endif

void ServerManager::handleNotFound() {
    requestCount_++;
    lastRequestTime_ = millis();
#ifdef ESP32
    // Dashboard shell: served from embedded flash assets (ADR-0168, slice-0169).
    // Exact-match resolution means an unknown or traversal path simply does not
    // match and falls through to the API/captive handling below, so there is no
    // filesystem path to escape and no separate traversal guard is needed.
    const DashboardAsset* asset =
        resolveDashboardAsset(kDashboardAssets, kDashboardAssetCount, server_->uri().c_str());
    if (asset != nullptr) {
        // Public surface: /login.html must be reachable without a session so
        // the redirect target from checkAuth() does not loop (ADR-0170). The
        // other three shell files stay gated.
        bool isPublic = (strcmp(asset->requestPath, "/login.html") == 0);
        if (!isPublic && !checkAuth()) return;
        sendDashboardAsset(*asset);
        return;
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
    LOG_D("SERVER", "Captive portal: %s (Host: %s)", uri.c_str(), host.c_str());
    
    if (settings.system.dashboardAuthEnabled) {
        // Serve the embedded login landing page (ADR-0168, slice-0169); always
        // present in flash, so no SD read and no missing-file fallback.
        const DashboardAsset* login =
            resolveDashboardAsset(kDashboardAssets, kDashboardAssetCount, "/login.html");
        sendDashboardAsset(*login);
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
    JsonDocument doc;
    doc["version"] = esp_app_get_description()->version;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["psram"] = ESP.getFreePsram();
    doc["battery"] = -1; // Battery sense not supported on Cardputer
    
    SDCardInfo sd = SDManager::getInstance().getCardInfo();
    JsonObject sdObj = doc["sd"].to<JsonObject>();
    sdObj["total"] = sd.totalBytes;
    sdObj["used"] = sd.usedBytes;
    sdObj["free"] = sd.freeBytes;
    sdObj["type"] = sd.type;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
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
        JsonDocument doc;

        JsonObject system = doc["system"].to<JsonObject>();
        system["deviceName"] = settings.system.deviceName;
        system["dashboardAuthEnabled"] = settings.system.dashboardAuthEnabled;
        system["dashboardUsername"] = settings.system.dashboardUsername;
        system["theme"] = settings.display.themePreset;
        
        JsonObject wireless = doc["wireless"].to<JsonObject>();
        wireless["karmaChannel"] = settings.wireless.karmaChannel;
        wireless["bleName"] = settings.wireless.bleName;

        JsonObject api = doc["api"].to<JsonObject>();
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
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);
            
            if (error) {
                server_->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            auto& settings = SettingsManager::getInstance().getMutable();
            
            if (doc["system"].is<JsonObject>()) {
                JsonObject system = doc["system"];
                if (system["deviceName"].is<const char*>()) {
                    strncpy(settings.system.deviceName, system["deviceName"], 32);
                    settings.system.deviceName[32] = '\0';
                }
                if (system["dashboardAuthEnabled"].is<bool>()) {
                    settings.system.dashboardAuthEnabled = system["dashboardAuthEnabled"];
                }
                if (system["dashboardUsername"].is<const char*>()) {
                    strncpy(settings.system.dashboardUsername, system["dashboardUsername"], 32);
                    settings.system.dashboardUsername[32] = '\0';
                }
                if (system["dashboardPassword"].is<const char*>()) {
                    strncpy(settings.system.dashboardPassword, system["dashboardPassword"], 32);
                    settings.system.dashboardPassword[32] = '\0';
                }
                if (system["theme"].is<int>()) {
                    settings.display.themePreset = system["theme"].as<uint8_t>();
                }
            }

            if (doc["wireless"].is<JsonObject>()) {
                JsonObject wireless = doc["wireless"];
                if (wireless["karmaChannel"].is<int>()) {
                    settings.wireless.karmaChannel = wireless["karmaChannel"];
                }
                if (wireless["bleName"].is<const char*>()) {
                    strncpy(settings.wireless.bleName, wireless["bleName"], 32);
                    settings.wireless.bleName[32] = '\0';
                }
            }

            if (doc["api"].is<JsonObject>()) {
                JsonObject api = doc["api"];
                if (api["wpasec"].is<const char*>()) {
                    SettingsManager::getInstance().setWpaSecKey(api["wpasec"]);
                }
                if (api["wigle"].is<const char*>()) {
                    SettingsManager::getInstance().setWigleKey(api["wigle"]);
                }
                if (api["pwncrack"].is<const char*>()) {
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

    // The list grows with every handshake; a fixed StaticJsonDocument<2048> once
    // silently truncated it past ~40 entries. The elastic document sizes itself to
    // the entry count, so it neither needs hand-computing nor can truncate.
    JsonDocument doc;
    JsonArray files = doc.to<JsonArray>();

    for (const auto& summary : summaries) {
        JsonObject fileObj = files.add<JsonObject>();
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

    JsonDocument doc;
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
                JsonObject fileObj = files.add<JsonObject>();
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
        LOG_D("SERVER", "Detail not in manifest: %s", filename.c_str());
        server_->send(404, "application/json", "{\"error\":\"Not found\"}");
        return;
    }

    JsonDocument doc;
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
        JsonObject gps = doc["gps"].to<JsonObject>();
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
        LOG_D("SERVER", "Wardriving 404: %s", fullPath.c_str());
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

    // Form-based session auth (ADR-0170). Pull the Cookie header (registered
    // via collectHeaders in setupWebServer; without that the arduino-esp32
    // WebServer drops it), parse out adv_session, and validate against the
    // in-RAM session table. Idle + absolute TTLs both apply.
    char token[33] = {};
    bool haveToken = false;
    if (server_->hasHeader("Cookie")) {
        haveToken = extractSessionToken(server_->header("Cookie").c_str(),
                                        token, sizeof(token));
    }
    if (haveToken &&
        validateSession(sessions_, kMaxSessions, token, millis(), kIdleTtlMs, kAbsTtlMs)) {
        return true;
    }

    // No session. For an API request, hand back a JSON 401 so dashboard.js can
    // detect expiry and redirect; for a page request, 302 to the login form.
    String uri = server_->uri();
    if (uri.startsWith("/api/")) {
        server_->sendHeader("Connection", "close");
        server_->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    } else {
        server_->sendHeader("Location", "/login.html", true);
        server_->send(302, "text/plain", "");
    }
    return false;
#else
    return true;
#endif
}

#ifdef ESP32
void ServerManager::generateSessionToken(char* out) {
    static const char kHex[] = "0123456789abcdef";
    // 128 bits = 16 bytes = 32 hex chars. esp_random() is the established
    // CSPRNG (used for MACs and the AP password).
    for (int i = 0; i < 4; ++i) {
        uint32_t r = esp_random();
        for (int j = 0; j < 8; ++j) {
            out[i * 8 + j] = kHex[(r >> (j * 4)) & 0x0F];
        }
    }
    out[32] = '\0';
}

// Constant-time byte-for-byte compare up to the longer of the two NUL-terminated
// inputs. Avoids a password-length side channel on the login path.
static bool constantTimeEquals(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t n  = (la > lb) ? la : lb;
    unsigned diff = static_cast<unsigned>(la ^ lb);
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (i < la) ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = (i < lb) ? static_cast<unsigned char>(b[i]) : 0;
        diff |= (ca ^ cb);
    }
    return diff == 0;
}

void ServerManager::handleLogin() {
    requestCount_++;
    lastRequestTime_ = millis();

    auto& settings = SettingsManager::getInstance().get();

    // If auth is disabled or no password is set, there is nothing to log into.
    if (!settings.system.dashboardAuthEnabled ||
        settings.system.dashboardPassword[0] == '\0') {
        server_->send(400, "application/json",
                      "{\"error\":\"login_not_available\"}");
        return;
    }

    // Accept both application/x-www-form-urlencoded (WebServer parses args())
    // and application/json. Form is the primary; JSON is a convenience for
    // scripted clients.
    String user;
    String pass;
    if (server_->hasArg("username") && server_->hasArg("password")) {
        user = server_->arg("username");
        pass = server_->arg("password");
    } else if (server_->hasArg("plain")) {
        JsonDocument doc;
        if (deserializeJson(doc, server_->arg("plain"))) {
            server_->send(400, "application/json", "{\"error\":\"bad_body\"}");
            return;
        }
        user = (const char*)(doc["username"] | "");
        pass = (const char*)(doc["password"] | "");
    } else {
        server_->send(400, "application/json", "{\"error\":\"missing_credentials\"}");
        return;
    }

    bool ok = constantTimeEquals(user.c_str(), settings.system.dashboardUsername) &&
              constantTimeEquals(pass.c_str(), settings.system.dashboardPassword);
    if (!ok) {
        // Fixed delay regardless of failure mode; a tiny brake on brute force
        // without a stateful rate limiter (out of scope per ADR-0170).
        delay(250);
        server_->sendHeader("Connection", "close");
        server_->send(401, "application/json", "{\"error\":\"invalid_credentials\"}");
        return;
    }

    char token[33];
    generateSessionToken(token);
    if (!createSession(sessions_, kMaxSessions, token, millis())) {
        server_->send(500, "application/json", "{\"error\":\"session_store_failed\"}");
        return;
    }

    // adv_session: HttpOnly blocks document.cookie; SameSite=Strict blocks
    // cross-site POSTs so we get CSRF protection for free; Path=/ scopes to
    // every dashboard route. No Secure attribute: the dashboard is served
    // over HTTP on the device AP, where confidentiality rests on WPA2
    // (ADR-0170).
    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "adv_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%u",
             token, static_cast<unsigned>(kAbsTtlMs / 1000u));
    server_->sendHeader("Set-Cookie", cookie);
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json", "{\"ok\":true}");
}

void ServerManager::handleLogout() {
    requestCount_++;
    lastRequestTime_ = millis();

    char token[33] = {};
    if (server_->hasHeader("Cookie") &&
        extractSessionToken(server_->header("Cookie").c_str(), token, sizeof(token))) {
        clearSession(sessions_, kMaxSessions, token);
    }
    // Expire the cookie regardless of whether we found a matching session, so
    // a stale cookie on the client is cleared.
    server_->sendHeader("Set-Cookie",
                        "adv_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    server_->sendHeader("Connection", "close");
    server_->send(200, "application/json", "{\"ok\":true}");
}
#endif

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
