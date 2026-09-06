/**
 * @file wardriving_manager.cpp
 * @brief Wardriving session manager implementation
 */

#include "wardriving_manager.h"
#include "wardriving_exporter.h"
#include "../wifi/wifi_scanner.h"
#include "../gps/gps_manager.h"
#include "../storage/settings_manager.h"
#include <Arduino.h>
#include <SD.h>
#include <cmath>
#include <time.h>
#include "../system/time_manager.h"

// Logging macros
#define LOG_I(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_E(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

namespace adversary {
namespace wardriving {

// ============================================================================
// Singleton Access
// ============================================================================

static WardrivingManager* s_instance = nullptr;

WardrivingManager& WardrivingManager::getInstance() {
    if (!s_instance) s_instance = new WardrivingManager();
    return *s_instance;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WardrivingManager::WardrivingManager()
    : active_(false)
    , paused_(false)
    , csvHeaderWritten_(false)
    , lastProcessedScanCount_(0)
    , lastLat_(0.0)
    , lastLon_(0.0)
    , hasLastPosition_(false)
    , lastWiFiScanMs_(0)
    , lastGPSUpdateMs_(0)
    , lastFlushMs_(0)
{
    memset(csvPath_, 0, sizeof(csvPath_));
}

WardrivingManager::~WardrivingManager() {
    if (active_) {
        stopSession();
    }
}

// ============================================================================
// Session Control
// ============================================================================

bool WardrivingManager::startSession() {
    if (active_) {
        LOG_W("[Wardriving] Session already active");
        return false;
    }
    
    // Generate session ID
    generateSessionId(session_.sessionId);
    
    // Initialize session
    session_.startTimeMs = millis();
    session_.endTimeMs = 0;
    session_.networksFound = 0;
    session_.networksWithGPS = 0;
    session_.distanceTraveledKm = 0.0f;
    
    // =========================================================================
    // STREAM-TO-SD ARCHITECTURE
    // =========================================================================
    // Clear hash set (BSSID deduplication - fixed memory ~8 bytes per network)
    seenBSSIDs_.clear();
    
    // Clear and pre-allocate small pending buffer (~20 entries max for GPS updates)
    // This is much smaller than before - we write to SD as soon as GPS is obtained
    pendingNetworks_.clear();
    pendingNetworks_.reserve(PENDING_BUFFER_SIZE);
    
    // Create CSV file path upfront (we'll write header on first network)
    snprintf(csvPath_, sizeof(csvPath_), "%s/%s.csv", WARDRIVING_DIR, session_.sessionId);
    csvHeaderWritten_ = false;
    
    // Ensure wardriving directory exists
    if (!SD.exists(WARDRIVING_DIR)) {
        if (!SD.mkdir(WARDRIVING_DIR)) {
            LOG_E("[Wardriving] Failed to create wardriving directory");
            // Continue anyway - we'll try again when writing
        }
    }
    
    LOG_I("[Wardriving] Stream-to-SD mode - networks written directly to: %s", csvPath_);
    
    lastProcessedScanCount_ = 0;
    
    // Reset GPS tracking
    hasLastPosition_ = false;
    lastLat_ = 0.0;
    lastLon_ = 0.0;
    
    // Reset timing
    lastWiFiScanMs_ = 0;
    lastGPSUpdateMs_ = 0;
    lastFlushMs_ = millis();
    
    // Initialize WiFi scanner (init() is idempotent - safe to call multiple times)
    auto& scanner = WiFiScanner::getInstance();
    if (!scanner.init()) {
        LOG_E("[Wardriving] Failed to initialize WiFi scanner");
        active_ = false;
        return false;
    }
    
    active_ = true;
    paused_ = false;
    
    LOG_I("[Wardriving] Session started: %s (memory-safe streaming mode)", session_.sessionId);
    
    return true;
}

void WardrivingManager::discardSession() {
    if (!active_) {
        return;
    }
    
    char discardedPath[128];
    strncpy(discardedPath, csvPath_, sizeof(discardedPath));
    
    active_ = false;
    paused_ = false;
    
    // Stop any ongoing WiFi scan
    auto& scanner = WiFiScanner::getInstance();
    scanner.stopScan();
    
    delay(100);
    
    // Deleting partial CSV from SD
    if (SD.begin()) {
        if (SD.exists(discardedPath)) {
            SD.remove(discardedPath);
            LOG_I("[Wardriving] Session discarded. Deleted: %s", discardedPath);
        }
    }
    
    seenBSSIDs_.clear();
    pendingNetworks_.clear();
}

void WardrivingManager::stopSession() {
    if (!active_) {
        return;
    }
    
    session_.endTimeMs = millis();
    active_ = false;
    paused_ = false;
    
    // Stop any ongoing WiFi scan
    auto& scanner = WiFiScanner::getInstance();
    scanner.stopScan();
    
    // CRITICAL: WiFi operations can corrupt SPI bus state, preventing SD access
    // Small delay to let WiFi hardware settle before accessing SD card
    delay(100);
    
    // Reinitialize SD card to recover from potential SPI bus conflict
    if (!SD.begin()) {
        LOG_E("[Wardriving] SD card re-init failed, trying again...");
        delay(200);
        if (!SD.begin()) {
            LOG_E("[Wardriving] SD card unavailable - data may be lost!");
        }
    }
    
    // Flush any remaining pending networks to SD
    flushToSD();
    
    LOG_I("[Wardriving] Session stopped: %s (%lu networks, %.2f km)",
          session_.sessionId, (unsigned long)session_.networksFound, session_.distanceTraveledKm);
    
    // Export JSON summary (CSV was written incrementally via streaming)
    char jsonPath[128];
    snprintf(jsonPath, sizeof(jsonPath), "%s/%s.json", WARDRIVING_DIR, session_.sessionId);
    
    // For JSON export, we need to read back CSV or just export session summary
    // Since we don't keep all networks in RAM, create a simple session summary JSON
    File jsonFile = SD.open(jsonPath, FILE_WRITE);
    if (jsonFile) {
        jsonFile.printf("{\n");
        jsonFile.printf("  \"sessionId\": \"%s\",\n", session_.sessionId);
        jsonFile.printf("  \"networksFound\": %lu,\n", (unsigned long)session_.networksFound);
        jsonFile.printf("  \"networksWithGPS\": %lu,\n", (unsigned long)session_.networksWithGPS);
        jsonFile.printf("  \"distanceKm\": %.2f,\n", session_.distanceTraveledKm);
        jsonFile.printf("  \"durationMs\": %lu,\n", session_.endTimeMs - session_.startTimeMs);
        jsonFile.printf("  \"csvFile\": \"%s\"\n", csvPath_);
        jsonFile.printf("}\n");
        jsonFile.close();
        LOG_I("[Wardriving] Session summary saved: %s", jsonPath);
    }
    
    // Clear memory - this is the key to avoiding OOM
    seenBSSIDs_.clear();
    pendingNetworks_.clear();
    pendingNetworks_.shrink_to_fit();
    
    // Cleanup WiFiScanner results to free heap
    scanner.clearResults();
    
    LOG_I("[Wardriving] Memory released - heap: %lu bytes", (unsigned long)ESP.getFreeHeap());
}

void WardrivingManager::pauseSession() {
    if (!active_ || paused_) {
        return;
    }
    
    paused_ = true;
    LOG_I("[Wardriving] Session paused");
}

void WardrivingManager::resumeSession() {
    if (!active_ || !paused_) {
        return;
    }
    
    paused_ = false;
    LOG_I("[Wardriving] Session resumed");
}

// ============================================================================
// Runtime Updates
// ============================================================================

void WardrivingManager::update() {
    if (!active_ || paused_) {
        return;
    }
    
    uint32_t now = millis();
    
    // Get scan interval from settings (allows user configuration)
    uint32_t scanInterval = SettingsManager::getInstance().get().wireless.wardrivingScanIntervalMs;
    
    // Process WiFi scans
    if (now - lastWiFiScanMs_ >= scanInterval) {
        processWiFiScan();
        lastWiFiScanMs_ = now;
    }
    
    // Process GPS updates for pending networks
    if (now - lastGPSUpdateMs_ >= GPS_UPDATE_INTERVAL_MS) {
        processGPSUpdate();
        lastGPSUpdateMs_ = now;
    }
    
    // Flush pending networks to SD periodically
    // Trigger conditions: time exceeded OR buffer getting full
    bool shouldFlush = (now - lastFlushMs_ >= FLUSH_INTERVAL_MS) ||
                       (pendingNetworks_.size() >= MAX_PENDING_BEFORE_FLUSH);
    
    if (shouldFlush && !pendingNetworks_.empty()) {
        flushToSD();
        lastFlushMs_ = now;
    }
    
    // Debug: Periodic heap monitoring (every 60 seconds)
    static uint32_t lastHeapLog = 0;
    if (now - lastHeapLog > 60000) {
        LOG_I("[Wardriving] Heap: %lu, seen: %lu, pending: %lu", 
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)seenBSSIDs_.size(),
              (unsigned long)pendingNetworks_.size());
        lastHeapLog = now;
    }
}

// ============================================================================
// State Queries
// ============================================================================

bool WardrivingManager::isActive() const {
    return active_;
}

bool WardrivingManager::isPaused() const {
    return paused_;
}

const WardrivingSession& WardrivingManager::getSession() const {
    return session_;
}

uint32_t WardrivingManager::getNetworkCount() const {
    // Hash set contains all unique BSSIDs - this is the accurate count
    return static_cast<uint32_t>(seenBSSIDs_.size());
}

std::vector<NetworkEntry> WardrivingManager::getNetworks() const {
    // Return pending buffer - for UI display only
    // Note: Most networks have been flushed to SD already
    return pendingNetworks_;
}

// ============================================================================
// Internal Processing
// ============================================================================

void WardrivingManager::processWiFiScan() {
    auto& scanner = WiFiScanner::getInstance();
    
    // Start scan if not already running
    if (scanner.getState() == ScannerState::IDLE || 
        scanner.getState() == ScannerState::COMPLETED) {
        scanner.startScan();  // Async scan, EventBus scan completion handled internally
    }
    
    // Process results from last completed scan
    const auto& results = scanner.getNetworks();
    
    // Optimization: avoid re-processing same scan multiple times
    if (results.empty() || scanner.getScanCount() == lastProcessedScanCount_) {
        return;
    }
    
    lastProcessedScanCount_ = scanner.getScanCount();
    uint32_t now = millis();
    uint32_t newCount = 0;
    
    for (const auto& net : results) {
        // O(1) lookup: is this BSSID already in our hash set?
        if (hasSeenBSSID(net.bssid)) {
            // Already seen - just update pending buffer entry if exists
            for (auto& pending : pendingNetworks_) {
                if (memcmp(pending.bssid, net.bssid, 6) == 0) {
                    // Update RSSI if stronger
                    if (net.rssi > pending.rssi) {
                        pending.rssi = net.rssi;
                    }
                    pending.lastSeenMs = now;
                    pending.seenCount++;
                    
                    // Try to tag with GPS if it doesn't have it yet
                    if (!pending.hasGPS) {
                        updateNetworkGPS(&pending);
                    }
                    break;
                }
            }
            continue;
        }
        
        // New network! Add to hash set and pending buffer
        NetworkEntry* entry = addNewNetwork(net.bssid);
        
        if (!entry) {
            // Max networks reached or buffer full
            continue;
        }
        
        newCount++;
        
        // Populate metadata
        entry->rssi = net.rssi;
        entry->channel = net.channel;
        entry->encryptionType = static_cast<uint8_t>(net.security);
        entry->lastSeenMs = now;
        entry->seenCount = 1;
        
        // Copy SSID
        if (!net.ssid.empty()) {
            strncpy(entry->ssid, net.ssid.c_str(), sizeof(entry->ssid) - 1);
        }
        
        // Tag with GPS immediately if available
        updateNetworkGPS(entry);
    }
    
    if (newCount > 0) {
        LOG_I("[Wardriving] Found %lu new networks (total: %lu)", 
              (unsigned long)newCount, (unsigned long)seenBSSIDs_.size());
    }
}

void WardrivingManager::processGPSUpdate() {
    using namespace gps;
    
    auto& gps = GPSManager::getInstance();
    
    if (!gps.isDetected() || !gps.hasValidFix()) {
        return;
    }
    
    const auto& gpsData = gps.getCurrentData();
    const auto& coord = gpsData.coordinate;
    
    // Calculate distance if we have a previous position
    if (hasLastPosition_) {
        float distKm = calculateDistance(lastLat_, lastLon_, coord.latitude, coord.longitude);
        
        // Only update if movement is significant
        if (distKm >= MIN_DISTANCE_DELTA_M / 1000.0f) {
            session_.distanceTraveledKm += distKm;
            lastLat_ = coord.latitude;
            lastLon_ = coord.longitude;
        }
    } else {
        // First position
        lastLat_ = coord.latitude;
        lastLon_ = coord.longitude;
        hasLastPosition_ = true;
    }
    
    // Tag all pending networks that lack GPS
    for (auto& pending : pendingNetworks_) {
        if (!pending.hasGPS) {
            updateNetworkGPS(&pending);
        }
    }
}

bool WardrivingManager::hasSeenBSSID(const uint8_t* bssid) const {
    uint64_t hash = hashBSSID(bssid);
    return seenBSSIDs_.find(hash) != seenBSSIDs_.end();
}

NetworkEntry* WardrivingManager::addNewNetwork(const uint8_t* bssid) {
    uint64_t hash = hashBSSID(bssid);
    
    // Check if max networks reached
    if (seenBSSIDs_.size() >= MAX_NETWORKS) {
        return nullptr;
    }
    
    // Add to hash set for future deduplication
    seenBSSIDs_.insert(hash);
    session_.networksFound++;
    
    // Add to pending buffer for GPS tagging
    // If buffer is full, flush first
    if (pendingNetworks_.size() >= PENDING_BUFFER_SIZE) {
        flushToSD();
    }
    
    // Create new entry in pending buffer
    NetworkEntry entry;
    memcpy(entry.bssid, bssid, 6);
    entry.firstSeenMs = millis();
    entry.lastSeenMs = entry.firstSeenMs;
    pendingNetworks_.push_back(entry);
    
    return &pendingNetworks_.back();
}

void WardrivingManager::flushToSD() {
    if (pendingNetworks_.empty()) {
        return;
    }
    
    uint32_t flushedCount = 0;
    
    for (const auto& entry : pendingNetworks_) {
        if (writeNetworkToCSV(entry)) {
            flushedCount++;
        }
    }
    
    LOG_I("[Wardriving] Flushed %lu networks to SD (total: %lu)", 
          (unsigned long)flushedCount, (unsigned long)seenBSSIDs_.size());
    
    // Clear pending buffer - memory is now free
    pendingNetworks_.clear();
}

bool WardrivingManager::writeNetworkToCSV(const NetworkEntry& entry) {
    // Open file in append mode
    File file = SD.open(csvPath_, FILE_APPEND);
    if (!file) {
        // Try to create parent directory if file open fails
        if (!SD.exists(WARDRIVING_DIR)) {
            SD.mkdir(WARDRIVING_DIR);
        }
        file = SD.open(csvPath_, FILE_APPEND);
        if (!file) {
            LOG_E("[Wardriving] Failed to open CSV: %s", csvPath_);
            return false;
        }
    }
    
    // Write header if this is the first write
    if (!csvHeaderWritten_) {
        // WiGLE CSV format header
        file.println("WigleWifi-1.4,appRelease=1.0,model=M5Cardputer,release=1,device=ESP32,display=na,board=ESP32-S3,brand=Adversary");
        file.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
        csvHeaderWritten_ = true;
    }
    
    // Format BSSID
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             entry.bssid[0], entry.bssid[1], entry.bssid[2],
             entry.bssid[3], entry.bssid[4], entry.bssid[5]);
    
    // Map encryption type to WiGLE format
    const char* authMode = "[WPA2-PSK-CCMP][ESS]";  // Default
    switch (entry.encryptionType) {
        case 0: authMode = "[ESS]"; break;              // OPEN
        case 1: authMode = "[WEP][ESS]"; break;         // WEP
        case 2: authMode = "[WPA-PSK][ESS]"; break;     // WPA_PSK
        case 3: authMode = "[WPA2-PSK-CCMP][ESS]"; break; // WPA2_PSK
        case 4: authMode = "[WPA-PSK+WPA2-PSK][ESS]"; break; // WPA_WPA2_PSK
        case 5: authMode = "[WPA2-EAP][ESS]"; break;    // WPA2_ENTERPRISE
        case 6: authMode = "[WPA3][ESS]"; break;        // WPA3_PSK
        case 7: authMode = "[WPA2-PSK+WPA3][ESS]"; break; // WPA2_WPA3_PSK
    }
    
    // Format timestamp (approximate - millis to date string)
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "2024-01-01 %02u:%02u:%02u",
             (unsigned int)((entry.firstSeenMs / 3600000) % 24),
             (unsigned int)((entry.firstSeenMs / 60000) % 60),
             (unsigned int)((entry.firstSeenMs / 1000) % 60));
    
    // Write CSV row
    file.printf("%s,\"%s\",%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI\n",
                bssidStr,
                entry.ssid,
                authMode,
                timeStr,
                entry.channel,
                entry.rssi,
                entry.hasGPS ? entry.latitude : 0.0,
                entry.hasGPS ? entry.longitude : 0.0,
                entry.hasGPS ? entry.altitude : 0.0f,
                entry.hasGPS ? 10.0f : 0.0f);  // Accuracy estimate
    
    file.close();
    return true;
}

void WardrivingManager::updateNetworkGPS(NetworkEntry* entry) {
    using namespace gps;
    
    auto& gps = GPSManager::getInstance();
    
    if (!gps.isDetected() || !gps.hasValidFix()) {
        return;
    }
    
    const auto& gpsData = gps.getCurrentData();
    const auto& coord = gpsData.coordinate;
    
    // Check GPS quality thresholds
    if (coord.satellites < MIN_SATELLITES_FOR_TAGGING) {
        return;
    }
    
    if (coord.hdop > MAX_HDOP_FOR_TAGGING) {
        return;
    }
    
    // Tag network with GPS
    entry->latitude = coord.latitude;
    entry->longitude = coord.longitude;
    entry->altitude = coord.altitude;
    entry->satellites = coord.satellites;
    entry->hasGPS = true;
    
    session_.networksWithGPS++;
}

// ============================================================================
// Helper Functions
// ============================================================================

float WardrivingManager::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    // Haversine formula for great-circle distance
    // https://en.wikipedia.org/wiki/Haversine_formula
    
    constexpr double R = 6371.0;  // Earth radius in kilometers
    
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    
    double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
               sin(dLon / 2.0) * sin(dLon / 2.0);
    
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    
    return R * c;  // Distance in kilometers
}

uint64_t WardrivingManager::hashBSSID(const uint8_t* bssid) const {
    // Simple hash: combine all 6 bytes into uint64_t
    uint64_t hash = 0;
    for (int i = 0; i < 6; i++) {
        hash = (hash << 8) | bssid[i];
    }
    return hash;
}

void WardrivingManager::generateSessionId(char* buffer) {
    // Format: WD-YYYYMMDD-HHMMSS using real wall-clock time
    time_t now = TimeManager::getInstance().now();
    struct tm* timeinfo = localtime(&now);
    
    if (timeinfo && now > 1000000000) {  // Sanity: after ~2001 (time is synced)
        snprintf(buffer, 20, "WD-%04d%02d%02d-%02d%02d%02d",
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        // Fallback if no RTC/time sync
        snprintf(buffer, 20, "WD-%08lX", (unsigned long)(millis() / 1000));
    }
}

} // namespace wardriving
} // namespace adversary
