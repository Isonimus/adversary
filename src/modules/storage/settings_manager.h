/**
 * @file settings_manager.h
 * @brief Persistent settings manager with JSON config file
 * 
 * Manages runtime settings stored in /adversary/config/adversary.conf
 * Settings persist across reboots and are editable via Settings screen.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include "../../utils/mac_utils.h"
#include "../../hal/expansion/expansion_cap.h"
#include "../ir/ir_capability.h"

#ifdef ESP32
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#if defined(TARGET_M5STICK)
#include <LittleFS.h>
#endif
#endif

namespace adversary {

// =============================================================================
// Settings Structures
// =============================================================================

struct WiFiCredential {
    char ssid[33];
    char password[65];
};

struct WirelessSettings {
    uint8_t karmaChannel = 6;
    bool karmaAutoRotate = true;
    uint8_t karmaRotationSpeed = 1;   // 0=Fast(15s), 1=Normal(30s), 2=Slow(60s)
    uint8_t karmaCooldownMin = 5;     // SSID cooldown in minutes (0=Off, default=5)
    uint8_t deauthBurstCount = 5;
    uint32_t deauthIntervalMs = 100;
    uint32_t beaconIntervalMs = 100;
    uint32_t probeFloodIntervalMs = 10;
    uint32_t scanTimeoutMs = 5000;
    uint32_t wardrivingScanIntervalMs = 3000;  // Wardriving WiFi scan frequency (1s-10s)
    
    char fixedBleMac[18] = {0}; // Persistent MAC for BLE HID (format: "AA:BB:CC:DD:EE:FF")
    char bleName[33] = "Universal Key"; // Custom BLE Broadcast Name
    uint16_t bleAppearance = 0x03C1; // Default: Keyboard
    uint8_t keyboardLayout = 0;      // 0 = US, 1 = ES

    // Multi-radio expansion-cap detection override (slice-0002). Auto trusts the
    // SPI probe; the forced values let an operator pin the answer when a
    // half-seated cap probes falsely.
    hal::CapOverride capOverride = hal::CapOverride::Auto;

    // Which emitter IR replay drives (slice-0004). Defaults to the proven
    // built-in IR LED; the cap's 3-emitter array is the range/angle override.
    ir::IrTxSource irTxSource = ir::IrTxSource::BuiltIn;

    // Saved WiFi credentials for STA connection
    std::vector<WiFiCredential> savedCredentials;
};

struct DisplaySettings {
    uint8_t brightness = 100;
    uint16_t screenTimeoutSec = 0;  // 0 = never
    uint8_t toastPosition = 0;      // 0=TOP, 1=CENTER, 2=BOTTOM
    uint8_t themePreset = 0;        // 0=Red Team, 1=Matrix, 2=T800, 3=Fallout, 4=Cyberpunk, 5=M5Stick
};

struct SystemSettings {
    char deviceName[33] = "Adversary";  // Device name for AP/dashboard/mDNS
    bool serialDebug = true;
    bool notifySounds = true;   // Enable audio notifications
    bool notifyLeds = true;     // Enable LED notifications
    bool showHeapBadge = false; // Show free heap in status bar
    
    // Dashboard Server Settings
    bool dashboardAuthEnabled = true;       // Enable HTTP basic auth (secure default)
    char dashboardUsername[33] = "admin";   // Auth username (if enabled)
    char dashboardPassword[33] = "";        // Auth password (if enabled)
};

struct ApiKeys {
    char wpasec[65] = {0};      // WPA-SEC API key (32-char hex)
    char wigle[129] = {0};      // WiGLE pre-encoded Base64 key
    char pwncrack[97] = {0};    // pwncrack.org API key

    bool hasWpaSec() const { return wpasec[0] != '\0'; }
    bool hasWigle() const { return wigle[0] != '\0'; }
    bool hasPwncrack() const { return pwncrack[0] != '\0'; }
};

struct Settings {
    uint8_t version = 1;
    WirelessSettings wireless;
    DisplaySettings display;
    SystemSettings system;
    ApiKeys apiKeys;
};

// =============================================================================
// Whitelist Entry (for Auto Hunt exclusion)
// =============================================================================

struct WhitelistEntry {
    uint8_t bssid[6] = {0};
    char ssid[33] = {0};
    bool hasBssid = false;
    bool hasSsid = false;
    
    bool matches(const uint8_t* checkBssid, const char* checkSsid) const {
        // If entry has both, both must match
        if (hasBssid && hasSsid) {
            return (memcmp(bssid, checkBssid, 6) == 0) && 
                   (strcmp(ssid, checkSsid) == 0);
        }
        // If only BSSID, match BSSID
        if (hasBssid) {
            return memcmp(bssid, checkBssid, 6) == 0;
        }
        // If only SSID, match SSID
        if (hasSsid) {
            return strcmp(ssid, checkSsid) == 0;
        }
        return false;
    }
};

// =============================================================================
// SettingsManager Singleton
// =============================================================================

class SettingsManager {
public:
    static SettingsManager& getInstance() {
        static SettingsManager instance;
        return instance;
    }
    
    /**
     * @brief Load settings from SD card
     * Creates default config file if missing
     * @return true if loaded successfully
     */
    bool load();
    
    /**
     * @brief Save current settings to SD card
     * @return true if saved successfully
     */
    bool save();
    
    /**
     * @brief Reset to factory defaults
     */
    void reset();
    
    /**
     * @brief Check if settings have unsaved changes
     */
    bool isDirty() const { return dirty_; }
    
    /**
     * @brief Mark settings as modified
     */
    void markDirty() { dirty_ = true; }
    
    /**
     * @brief Access current settings (read-only)
     */
    const Settings& get() const { return settings_; }
    
    /**
     * @brief Access current settings (mutable)
     */
    Settings& getMutable() { dirty_ = true; return settings_; }
    
    // =========================================================================
    // Whitelist Management
    // =========================================================================
    
    /**
     * @brief Load whitelist from SD card
     * Creates empty whitelist if missing
     */
    bool loadWhitelist();
    
    /**
     * @brief Save whitelist to SD card
     */
    bool saveWhitelist();
    
    /**
     * @brief Check if a network is whitelisted
     */
    bool isWhitelisted(const uint8_t* bssid, const char* ssid) const;
    
    /**
     * @brief Get whitelist entries (for UI display)
     */
    const std::vector<WhitelistEntry>& getWhitelist() const { return whitelist_; }
    
    /**
     * @brief Add entry to whitelist
     */
    void addToWhitelist(const WhitelistEntry& entry) { whitelist_.push_back(entry); }
    
    /**
     * @brief Remove entry from whitelist by index
     */
    void removeFromWhitelist(size_t index) {
        if (index < whitelist_.size()) {
            whitelist_.erase(whitelist_.begin() + index);
        }
    }
    
    /**
     * @brief Clear whitelist
     */
    void clearWhitelist() { whitelist_.clear(); }
    
    // =========================================================================
    // WiFi Connection Credentials
    // =========================================================================
    
    const std::vector<WiFiCredential>& getSavedCredentials() const { return settings_.wireless.savedCredentials; }
    
    void addWiFiCredential(const char* ssid, const char* password) {
        if (!ssid || ssid[0] == '\0') return;
        
        // Check if already exists
        for (auto& cred : settings_.wireless.savedCredentials) {
            if (strcmp(cred.ssid, ssid) == 0) {
                if (password) strncpy(cred.password, password, 64);
                else cred.password[0] = '\0';
                dirty_ = true;
                return;
            }
        }
        
        // Add new
        WiFiCredential cred;
        strncpy(cred.ssid, ssid, 32);
        cred.ssid[32] = '\0';
        if (password) {
            strncpy(cred.password, password, 64);
            cred.password[64] = '\0';
        } else {
            cred.password[0] = '\0';
        }
        settings_.wireless.savedCredentials.push_back(cred);
        dirty_ = true;
    }
    
    void removeWiFiCredential(size_t index) {
        if (index < settings_.wireless.savedCredentials.size()) {
            settings_.wireless.savedCredentials.erase(settings_.wireless.savedCredentials.begin() + index);
            dirty_ = true;
        }
    }
    
    void clearWiFiCredentials() {
        if (!settings_.wireless.savedCredentials.empty()) {
            settings_.wireless.savedCredentials.clear();
            dirty_ = true;
        }
    }
    
    const char* getPasswordForSSID(const char* ssid) const {
        if (!ssid) return nullptr;
        for (const auto& cred : settings_.wireless.savedCredentials) {
            if (strcmp(cred.ssid, ssid) == 0) return cred.password;
        }
        return nullptr;
    }

    bool hasCredentialForSSID(const char* ssid) const {
        return getPasswordForSSID(ssid) != nullptr;
    }
    
    // =========================================================================
    // API Keys
    // =========================================================================
    
    const char* getWpaSecKey() const { return settings_.apiKeys.wpasec; }
    bool hasWpaSecKey() const { return settings_.apiKeys.hasWpaSec(); }
    
    void setWpaSecKey(const char* key) {
        if (key) {
            strncpy(settings_.apiKeys.wpasec, key, 64);
            settings_.apiKeys.wpasec[64] = '\0';
        } else {
            settings_.apiKeys.wpasec[0] = '\0';
        }
        dirty_ = true;
    }
    
    // WiGLE API key (pre-encoded Base64 "Encoded for use")
    const char* getWigleKey() const { return settings_.apiKeys.wigle; }
    bool hasWigleKey() const { return settings_.apiKeys.hasWigle(); }
    
    void setWigleKey(const char* key) {
        if (key) {
            strncpy(settings_.apiKeys.wigle, key, 128);
            settings_.apiKeys.wigle[128] = '\0';
        } else {
            settings_.apiKeys.wigle[0] = '\0';
        }
        dirty_ = true;
    }

    // pwncrack.org API key
    const char* getPwncrackKey() const { return settings_.apiKeys.pwncrack; }
    bool hasPwncrackKey() const { return settings_.apiKeys.hasPwncrack(); }

    void setPwncrackKey(const char* key) {
        if (key) {
            strncpy(settings_.apiKeys.pwncrack, key, 96);
            settings_.apiKeys.pwncrack[96] = '\0';
        } else {
            settings_.apiKeys.pwncrack[0] = '\0';
        }
        dirty_ = true;
    }
    
    /**
     * @brief Check for API key files and import if present
     * Looks for /adversary/config/wpasec.txt and /adversary/config/wigle.txt
     * Deletes files after successful import
     */
    void checkApiKeyFile();

private:
    SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    bool createDefaultConfig();
    bool createDefaultWhitelist();
    
    Settings settings_;
    std::vector<WhitelistEntry> whitelist_;
    bool dirty_ = false;
    
    static constexpr const char* CONFIG_PATH = "/adversary/config/adversary.conf";
    static constexpr const char* WHITELIST_PATH = "/adversary/config/ssid_whitelist.json";
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline bool SettingsManager::load() {
#ifdef ESP32
    Serial.println("[Settings] Loading configuration...");
    
#if defined(TARGET_M5STICK)
    // M5Stick uses LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[Settings] LittleFS mount failed");
        return createDefaultConfig();
    }
    
    if (!LittleFS.exists(CONFIG_PATH)) {
        Serial.println("[Settings] Config not found, creating defaults...");
        return createDefaultConfig();
    }
    
    File file = LittleFS.open(CONFIG_PATH, "r");
#else
    // Cardputer uses SD
    // Check if config file exists
    if (!SD.exists(CONFIG_PATH)) {
        Serial.println("[Settings] Config not found, creating defaults...");
        return createDefaultConfig();
    }
    
    File file = SD.open(CONFIG_PATH, FILE_READ);
#endif
    if (!file) {
        Serial.println("[Settings] Failed to open config file");
        return createDefaultConfig();
    }
    
    // Parse JSON. The config grew (saved creds + dashboard auth + 3 API keys), so
    // a fixed 2048 doc could silently overflow on save (dropping the last-added
    // members, e.g. the pwncrack key) and fail to parse on load. A generous heap
    // doc keeps the whole config intact.
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[Settings] JSON parse error: %s\n", error.c_str());
        return createDefaultConfig();
    }

    // Load version
    settings_.version = doc["version"] | 1;
    
    // Load wireless settings
    JsonObject wireless = doc["wireless"];
    if (wireless) {
        settings_.wireless.karmaChannel = wireless["karmaChannel"] | 6;
        settings_.wireless.karmaAutoRotate = wireless["karmaAutoRotate"] | true;
        settings_.wireless.karmaRotationSpeed = wireless["karmaRotationSpeed"] | 1;
        settings_.wireless.karmaCooldownMin = wireless["karmaCooldownMin"] | 5;
        settings_.wireless.deauthBurstCount = wireless["deauthBurstCount"] | 5;
        settings_.wireless.deauthIntervalMs = wireless["deauthIntervalMs"] | 100;
        settings_.wireless.beaconIntervalMs = wireless["beaconIntervalMs"] | 100;
        settings_.wireless.probeFloodIntervalMs = wireless["probeFloodIntervalMs"] | 10;
        settings_.wireless.scanTimeoutMs = wireless["scanTimeoutMs"] | 5000;
        settings_.wireless.scanTimeoutMs = wireless["scanTimeoutMs"] | 5000;
        settings_.wireless.wardrivingScanIntervalMs = wireless["wardrivingScanIntervalMs"] | 3000;
        
        const char* fixedMac = wireless["fixedBleMac"] | "";
        strncpy(settings_.wireless.fixedBleMac, fixedMac, 17);
        settings_.wireless.fixedBleMac[17] = '\0';
        
        const char* bName = wireless["bleName"] | "Universal Key";
        strncpy(settings_.wireless.bleName, bName, 32);
        settings_.wireless.bleName[32] = '\0';
        
        settings_.wireless.bleAppearance  = wireless["bleAppearance"]  | 0x03C1;
        settings_.wireless.keyboardLayout  = wireless["keyboardLayout"] | 0;
        settings_.wireless.capOverride =
            static_cast<hal::CapOverride>(
                static_cast<uint8_t>(wireless["capOverride"] | 0));
        settings_.wireless.irTxSource =
            static_cast<ir::IrTxSource>(
                static_cast<uint8_t>(wireless["irTxSource"] | 0));

        // Load saved WiFi credentials
        JsonArray credentials = wireless["savedWiFiCredentials"];
        settings_.wireless.savedCredentials.clear();
        if (credentials) {
            for (JsonObject credObj : credentials) {
                WiFiCredential cred;
                const char* ssid = credObj["ssid"] | "";
                const char* pass = credObj["password"] | "";
                strncpy(cred.ssid, ssid, 32);
                cred.ssid[32] = '\0';
                strncpy(cred.password, pass, 64);
                cred.password[64] = '\0';
                settings_.wireless.savedCredentials.push_back(cred);
            }
        } else {
            // Migration: Load old single credential if exists
            const char* savedSSID = wireless["savedWiFiSSID"] | "";
            const char* savedPass = wireless["savedWiFiPassword"] | "";
            if (savedSSID && savedSSID[0] != '\0') {
                WiFiCredential cred;
                strncpy(cred.ssid, savedSSID, 32);
                cred.ssid[32] = '\0';
                strncpy(cred.password, savedPass, 64);
                cred.password[64] = '\0';
                settings_.wireless.savedCredentials.push_back(cred);
            }
        }
    }
    
    // Load display settings
    JsonObject display = doc["display"];
    if (display) {
        settings_.display.brightness = display["brightness"] | 100;
        settings_.display.screenTimeoutSec = display["screenTimeoutSec"] | 0;
        settings_.display.toastPosition = display["toastPosition"] | 0;
        settings_.display.themePreset = display["themePreset"] | 0;
    }
    
    // Load system settings
    JsonObject system = doc["system"];
    if (system) {
        const char* devName = system["deviceName"] | "Adversary";
        strncpy(settings_.system.deviceName, devName, 32);
        settings_.system.deviceName[32] = '\0';
        settings_.system.serialDebug = system["serialDebug"] | true;
        settings_.system.notifySounds = system["notifySounds"] | true;
        settings_.system.notifyLeds = system["notifyLeds"] | true;
        settings_.system.showHeapBadge = system["showHeapBadge"] | false;
        
        // Dashboard Server Settings
        settings_.system.dashboardAuthEnabled = system["dashboardAuthEnabled"] | true;
        const char* dashUser = system["dashboardUsername"] | "admin";
        strncpy(settings_.system.dashboardUsername, dashUser, 32);
        settings_.system.dashboardUsername[32] = '\0';
        const char* dashPass = system["dashboardPassword"] | "";
        strncpy(settings_.system.dashboardPassword, dashPass, 32);
        settings_.system.dashboardPassword[32] = '\0';
    }
    
    // Load API keys
    JsonObject apiKeys = doc["apiKeys"];
    if (apiKeys) {
        const char* wpasecKey = apiKeys["wpasec"] | "";
        strncpy(settings_.apiKeys.wpasec, wpasecKey, 64);
        settings_.apiKeys.wpasec[64] = '\0';
        
        const char* wigleKey = apiKeys["wigle"] | "";
        strncpy(settings_.apiKeys.wigle, wigleKey, 128);
        settings_.apiKeys.wigle[128] = '\0';

        const char* pwncrackKey = apiKeys["pwncrack"] | "";
        strncpy(settings_.apiKeys.pwncrack, pwncrackKey, 96);
        settings_.apiKeys.pwncrack[96] = '\0';
    }
    
    dirty_ = false;
    return true;
#else
    dirty_ = false;
    return true;
#endif
}

inline bool SettingsManager::save() {
#ifdef ESP32
    Serial.println("[Settings] Saving configuration...");
    
#if defined(TARGET_M5STICK)
    // M5Stick uses LittleFS
    if (!LittleFS.exists("/adversary/config")) {
        LittleFS.mkdir("/adversary");
        LittleFS.mkdir("/adversary/config");
    }
    
    File file = LittleFS.open(CONFIG_PATH, "w");
#else
    // Cardputer uses SD
    // Ensure directory exists
    if (!SD.exists("/adversary/config")) {
        SD.mkdir("/adversary/config");
    }
    
    File file = SD.open(CONFIG_PATH, FILE_WRITE);
#endif
    if (!file) {
        Serial.println("[Settings] Failed to open config for writing");
        return false;
    }
    
    // Build JSON. Use a generous heap doc: a fixed 2048 stack doc silently
    // dropped later sections (system/display/api) once the config grew, which
    // then loaded as defaults (lost device name / theme).
    DynamicJsonDocument doc(8192);
    doc["version"] = settings_.version;
    
    // Wireless
    JsonObject wireless = doc.createNestedObject("wireless");
    wireless["karmaChannel"] = settings_.wireless.karmaChannel;
    wireless["karmaAutoRotate"] = settings_.wireless.karmaAutoRotate;
    wireless["karmaRotationSpeed"] = settings_.wireless.karmaRotationSpeed;
    wireless["karmaCooldownMin"] = settings_.wireless.karmaCooldownMin;
    wireless["deauthBurstCount"] = settings_.wireless.deauthBurstCount;
    wireless["deauthIntervalMs"] = settings_.wireless.deauthIntervalMs;
    wireless["beaconIntervalMs"] = settings_.wireless.beaconIntervalMs;
    wireless["probeFloodIntervalMs"] = settings_.wireless.probeFloodIntervalMs;
    wireless["scanTimeoutMs"] = settings_.wireless.scanTimeoutMs;
    wireless["scanTimeoutMs"] = settings_.wireless.scanTimeoutMs;
    wireless["wardrivingScanIntervalMs"] = settings_.wireless.wardrivingScanIntervalMs;
    
    if (settings_.wireless.fixedBleMac[0] != '\0') {
        wireless["fixedBleMac"] = settings_.wireless.fixedBleMac;
    }
    
    wireless["bleName"]        = settings_.wireless.bleName;
    wireless["bleAppearance"]  = settings_.wireless.bleAppearance;
    wireless["keyboardLayout"] = settings_.wireless.keyboardLayout;
    wireless["capOverride"]    = static_cast<uint8_t>(settings_.wireless.capOverride);
    wireless["irTxSource"]     = static_cast<uint8_t>(settings_.wireless.irTxSource);

    // Save WiFi credentials
    if (!settings_.wireless.savedCredentials.empty()) {
        JsonArray credentials = wireless.createNestedArray("savedWiFiCredentials");
        for (const auto& cred : settings_.wireless.savedCredentials) {
            JsonObject credObj = credentials.createNestedObject();
            credObj["ssid"] = cred.ssid;
            credObj["password"] = cred.password;
        }
    }
    
    // Display
    JsonObject display = doc.createNestedObject("display");
    display["brightness"] = settings_.display.brightness;
    display["screenTimeoutSec"] = settings_.display.screenTimeoutSec;
    display["toastPosition"] = settings_.display.toastPosition;
    display["themePreset"] = settings_.display.themePreset;
    
    // System
    JsonObject system = doc.createNestedObject("system");
    system["deviceName"] = settings_.system.deviceName;
    system["serialDebug"] = settings_.system.serialDebug;
    system["notifySounds"] = settings_.system.notifySounds;
    system["notifyLeds"] = settings_.system.notifyLeds;
    system["showHeapBadge"] = settings_.system.showHeapBadge;
    
    // Dashboard Server Settings
    system["dashboardAuthEnabled"] = settings_.system.dashboardAuthEnabled;
    system["dashboardUsername"] = settings_.system.dashboardUsername;
    if (settings_.system.dashboardPassword[0] != '\0') {
        system["dashboardPassword"] = settings_.system.dashboardPassword;
    }
    
    // API keys
    if (settings_.apiKeys.hasWpaSec() || settings_.apiKeys.hasWigle() ||
        settings_.apiKeys.hasPwncrack()) {
        JsonObject apiKeys = doc.createNestedObject("apiKeys");
        if (settings_.apiKeys.hasWpaSec()) {
            apiKeys["wpasec"] = settings_.apiKeys.wpasec;
        }
        if (settings_.apiKeys.hasWigle()) {
            apiKeys["wigle"] = settings_.apiKeys.wigle;
        }
        if (settings_.apiKeys.hasPwncrack()) {
            apiKeys["pwncrack"] = settings_.apiKeys.pwncrack;
        }
    }
    
    // Write with pretty formatting
    serializeJsonPretty(doc, file);
    file.close();
    
    dirty_ = false;
    return true;
#else
    dirty_ = false;
    return true;
#endif
}

inline void SettingsManager::reset() {
    settings_ = Settings();  // Reset to defaults
    dirty_ = true;
#ifdef ESP32
    Serial.println("[Settings] Reset to defaults");
#endif
}

inline bool SettingsManager::createDefaultConfig() {
    settings_ = Settings();  // Use default values
    dirty_ = true;
    return save();
}

inline bool SettingsManager::loadWhitelist() {
#ifdef ESP32
    Serial.println("[Settings] Loading whitelist...");
    
    whitelist_.clear();
    
    if (!SD.exists(WHITELIST_PATH)) {
        Serial.println("[Settings] Whitelist not found, creating empty...");
        return createDefaultWhitelist();
    }
    
    File file = SD.open(WHITELIST_PATH, FILE_READ);
    if (!file) {
        Serial.println("[Settings] Failed to open whitelist");
        return createDefaultWhitelist();
    }
    
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("[Settings] Whitelist parse error: %s\n", error.c_str());
        return createDefaultWhitelist();
    }
    
    JsonArray networks = doc["networks"];
    for (JsonObject net : networks) {
        WhitelistEntry entry;
        
        // Parse BSSID if present
        const char* bssidStr = net["bssid"];
        if (bssidStr && strlen(bssidStr) == 17) {
            // Parse AA:BB:CC:DD:EE:FF format
            unsigned int b[6];
            if (sscanf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                       &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
                for (int i = 0; i < 6; i++) entry.bssid[i] = b[i];
                entry.hasBssid = true;
            }
        }
        
        // Parse SSID if present
        const char* ssidStr = net["ssid"];
        if (ssidStr && strlen(ssidStr) > 0) {
            strncpy(entry.ssid, ssidStr, 32);
            entry.ssid[32] = '\0';
            entry.hasSsid = true;
        }
        
        if (entry.hasBssid || entry.hasSsid) {
            whitelist_.push_back(entry);
        }
    }
    
    Serial.printf("[Settings] Loaded %d whitelist entries\n", (int)whitelist_.size());
    return true;
#else
    return true;
#endif
}

inline bool SettingsManager::saveWhitelist() {
#ifdef ESP32
    Serial.println("[Settings] Saving whitelist...");
    
    if (!SD.exists("/adversary/config")) {
        SD.mkdir("/adversary/config");
    }
    
    File file = SD.open(WHITELIST_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("[Settings] Failed to open whitelist for writing");
        return false;
    }
    
    StaticJsonDocument<2048> doc;
    doc["version"] = 1;
    JsonArray networks = doc.createNestedArray("networks");
    
    for (const auto& entry : whitelist_) {
        JsonObject net = networks.createNestedObject();
        if (entry.hasBssid) {
            char bssidStr[18];
            utils::formatMacBytes(entry.bssid, bssidStr, sizeof(bssidStr));
            net["bssid"] = bssidStr;
        }
        if (entry.hasSsid) {
            net["ssid"] = entry.ssid;
        }
    }
    
    serializeJsonPretty(doc, file);
    file.close();
    
    Serial.printf("[Settings] Saved %d whitelist entries\n", (int)whitelist_.size());
    return true;
#else
    return true;
#endif
}

inline bool SettingsManager::isWhitelisted(const uint8_t* bssid, const char* ssid) const {
    for (const auto& entry : whitelist_) {
        if (entry.matches(bssid, ssid)) {
            return true;
        }
    }
    return false;
}

inline bool SettingsManager::createDefaultWhitelist() {
    whitelist_.clear();
    return saveWhitelist();
}

inline void SettingsManager::checkApiKeyFile() {
#ifdef ESP32
    static constexpr const char* WPASEC_KEY_FILE = "/adversary/config/wpasec.txt";
    
#if defined(TARGET_M5STICK)
    if (!LittleFS.exists(WPASEC_KEY_FILE)) {
        return;
    }
    File file = LittleFS.open(WPASEC_KEY_FILE, "r");
#else
    if (!SD.exists(WPASEC_KEY_FILE)) {
        return;
    }
    File file = SD.open(WPASEC_KEY_FILE, FILE_READ);
#endif
    
    if (!file) {
        return;
    }
    
    // Read API key (should be 32-char hex)
    char newKey[65] = {0};
    size_t len = file.readBytesUntil('\n', newKey, 64);
    file.close();
    
    // Trim whitespace
    while (len > 0 && (newKey[len-1] == '\r' || newKey[len-1] == '\n' || newKey[len-1] == ' ')) {
        newKey[--len] = '\0';
    }
    
    if (len == 0) {
        Serial.println("[Settings] WPA-SEC key file is empty");
        return;
    }
    
    // Check if key changed
    if (strcmp(settings_.apiKeys.wpasec, newKey) != 0) {
        Serial.printf("[Settings] Importing WPA-SEC key: %s...\\n", 
                      String(newKey).substring(0, 8).c_str());
        setWpaSecKey(newKey);
        save();
    }
    
    // Delete the key file after import (security)
#if defined(TARGET_M5STICK)
    LittleFS.remove(WPASEC_KEY_FILE);
#else
    SD.remove(WPASEC_KEY_FILE);
#endif
    Serial.println("[Settings] WPA-SEC key file imported and deleted");
    
    // --- WiGLE key file ---
    static constexpr const char* WIGLE_KEY_FILE = "/adversary/config/wigle.txt";
    
#if defined(TARGET_M5STICK)
    if (LittleFS.exists(WIGLE_KEY_FILE)) {
        File wfile = LittleFS.open(WIGLE_KEY_FILE, "r");
#else
    if (SD.exists(WIGLE_KEY_FILE)) {
        File wfile = SD.open(WIGLE_KEY_FILE, FILE_READ);
#endif
        if (wfile) {
            char wigleKey[129] = {0};
            size_t wlen = wfile.readBytesUntil('\n', wigleKey, 128);
            wfile.close();
            
            // Trim whitespace
            while (wlen > 0 && (wigleKey[wlen-1] == '\r' || wigleKey[wlen-1] == '\n' || wigleKey[wlen-1] == ' ')) {
                wigleKey[--wlen] = '\0';
            }
            
            if (wlen > 0 && strcmp(settings_.apiKeys.wigle, wigleKey) != 0) {
                Serial.printf("[Settings] Importing WiGLE key: %s...\\n",
                              String(wigleKey).substring(0, 8).c_str());
                setWigleKey(wigleKey);
                save();
            }
            
#if defined(TARGET_M5STICK)
            LittleFS.remove(WIGLE_KEY_FILE);
#else
            SD.remove(WIGLE_KEY_FILE);
#endif
            Serial.println("[Settings] WiGLE key file imported and deleted");
        }
    }

    // --- pwncrack key file ---
    static constexpr const char* PWNCRACK_KEY_FILE = "/adversary/config/pwncrack.txt";

#if defined(TARGET_M5STICK)
    if (LittleFS.exists(PWNCRACK_KEY_FILE)) {
        File pfile = LittleFS.open(PWNCRACK_KEY_FILE, "r");
#else
    if (SD.exists(PWNCRACK_KEY_FILE)) {
        File pfile = SD.open(PWNCRACK_KEY_FILE, FILE_READ);
#endif
        if (pfile) {
            char pwncrackKey[97] = {0};
            size_t plen = pfile.readBytesUntil('\n', pwncrackKey, 96);
            pfile.close();

            // Trim whitespace
            while (plen > 0 && (pwncrackKey[plen-1] == '\r' || pwncrackKey[plen-1] == '\n' || pwncrackKey[plen-1] == ' ')) {
                pwncrackKey[--plen] = '\0';
            }

            if (plen > 0 && strcmp(settings_.apiKeys.pwncrack, pwncrackKey) != 0) {
                Serial.printf("[Settings] Importing pwncrack key: %s...\\n",
                              String(pwncrackKey).substring(0, 8).c_str());
                setPwncrackKey(pwncrackKey);
                save();
            }

#if defined(TARGET_M5STICK)
            LittleFS.remove(PWNCRACK_KEY_FILE);
#else
            SD.remove(PWNCRACK_KEY_FILE);
#endif
            Serial.println("[Settings] pwncrack key file imported and deleted");
        }
    }

#endif
}

} // namespace adversary
