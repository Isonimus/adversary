/**
 * @file wardriving_manager.cpp
 * @brief Wardriving session manager implementation
 */

#include "wardriving_manager.h"
#include "../wifi/wifi_scanner.h"
#include "../gps/gps_manager.h"
#include "../storage/settings_manager.h"
#ifdef ARDUINO
#include "../ble/ble_scanner.h"   // NimBLE; device-only (not in the native build filter)
#endif
#include <Arduino.h>
#include <SD.h>
#include <cmath>
#include <time.h>
#include "../system/time_manager.h"

#include "utils/log.h"

#if defined(WARDRIVE_HEAP_TRACE) && defined(ARDUINO)
// Throwaway diagnostic (build with -DWARDRIVE_HEAP_TRACE): per-scan-cycle heap slope to
// SD, for the untethered BOTH-mode OOM hunt. Remove once the slope is characterized
// (LEDGER: "Remove the WARDRIVE_HEAP_TRACE instrument"). esp_heap_caps gives the
// largest-free-block, which separates fragmentation from plain exhaustion.
#include "esp_heap_caps.h"
#endif

namespace adversary {
namespace wardriving {

#if defined(WARDRIVE_HEAP_TRACE) && defined(ARDUINO)
namespace {
// Append one heap sample to a dedicated sidecar file, using the same open/append/close
// idiom as writeNetworkToCSV so it adds no new file handle or cadence of its own. Writes
// internal-RAM free and largest-free-block: free-vs-largest is the fragmentation tell.
void appendHeapSample(uint32_t uptimeMs, uint32_t deviceCount, uint8_t mode) {
    static bool headerWritten = false;
    const char* path = "/adversary/logs/wardrive-heap.csv";
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        if (!SD.exists("/adversary/logs")) SD.mkdir("/adversary/logs");
        f = SD.open(path, FILE_APPEND);
        if (!f) {
            // Non-fatal by design: a dropped diagnostic sample must not abort a field
            // run, but say so rather than masking it.
            LOG_W("HEAPTRACE", "SD open failed; sample dropped");
            return;
        }
    }
    if (!headerWritten) {
        f.println("uptimeMs,devices,freeInternal,largestBlockInternal,mode");
        headerWritten = true;
    }
    char line[96];
    snprintf(line, sizeof(line), "%lu,%lu,%u,%u,%u\n",
             (unsigned long)uptimeMs, (unsigned long)deviceCount,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned int)mode);
    f.print(line);
    f.close();
}
}  // namespace
#endif

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
    , saturationWarned_(false)
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
        LOG_W("WARDRV", "Session already active");
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
    // Reset the bounded dedup filter + exact counter (ADR-0103)
    seenFilter_.clear();
    session_.networksFound = 0;
    saturationWarned_ = false;

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
            LOG_E("WARDRV", "Failed to create wardriving directory");
            // Continue anyway - we'll try again when writing
        }
    }
    
    LOG_I("WARDRV", "Stream-to-SD mode - networks written directly to: %s", csvPath_);
    
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
        LOG_E("WARDRV", "Failed to initialize WiFi scanner");
        active_ = false;
        return false;
    }
    
    active_ = true;
    paused_ = false;
    
    LOG_I("WARDRV", "Session started: %s (memory-safe streaming mode)", session_.sessionId);
    
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

#ifdef ARDUINO
    // Release the BLE controller radio before touching SD (ADR-0060).
    if (BLEScanner::isInitialized()) {
        BLEScanner::getInstance().deinit();
    }
#endif

    delay(100);

    // Deleting partial CSV from SD
    if (SD.begin()) {
        if (SD.exists(discardedPath)) {
            SD.remove(discardedPath);
            LOG_I("WARDRV", "Session discarded. Deleted: %s", discardedPath);
        }
    }

    seenFilter_.clear();
    session_.networksFound = 0;
    saturationWarned_ = false;
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

#ifdef ARDUINO
    // Release the BLE controller radio before the WiFi/SD bus recovery below (ADR-0060).
    if (BLEScanner::isInitialized()) {
        BLEScanner::getInstance().deinit();
    }
#endif

    // CRITICAL: WiFi operations can corrupt SPI bus state, preventing SD access
    // Small delay to let WiFi hardware settle before accessing SD card
    delay(100);
    
    // Reinitialize SD card to recover from potential SPI bus conflict
    if (!SD.begin()) {
        LOG_E("WARDRV", "SD card re-init failed, trying again...");
        delay(200);
        if (!SD.begin()) {
            LOG_E("WARDRV", "SD card unavailable - data may be lost!");
        }
    }
    
    // Flush any remaining pending networks to SD
    flushToSD();
    
    LOG_I("WARDRV", "Session stopped: %s (%lu networks, %.2f km)",
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
        LOG_I("WARDRV", "Session summary saved: %s", jsonPath);
    }
    
    // Release the dedup filter + pending buffer: the heap these hold is the OOM lever
    // (ADR-0103). session_.networksFound is a value-type session summary, not heap, so it is
    // deliberately left intact here: the saving screen reports it after stop, and zeroing it
    // showed "0 networks exported" in the field. startSession is the sole reset point.
    seenFilter_.clear();
    saturationWarned_ = false;
    pendingNetworks_.clear();
    pendingNetworks_.shrink_to_fit();
    
    // Cleanup WiFiScanner results to free heap
    scanner.clearResults();
    
    LOG_I("WARDRV", "Memory released - heap: %lu bytes", (unsigned long)ESP.getFreeHeap());
}

void WardrivingManager::pauseSession() {
    if (!active_ || paused_) {
        return;
    }
    
    paused_ = true;
    LOG_I("WARDRV", "Session paused");
}

void WardrivingManager::resumeSession() {
    if (!active_ || !paused_) {
        return;
    }
    
    paused_ = false;
    LOG_I("WARDRV", "Session resumed");
}

// ============================================================================
// Runtime Updates
// ============================================================================

void WardrivingManager::update() {
    if (!active_ || paused_) {
        return;
    }
    
    uint32_t now = millis();

    // Get scan interval and radio mode from settings (allows user configuration)
    const auto& wireless = SettingsManager::getInstance().get().wireless;
    uint32_t scanInterval = wireless.wardrivingScanIntervalMs;
    RadioMode mode = static_cast<RadioMode>(wireless.wardrivingRadioMode);

    // Process scans per radio mode. WiFi + BLE run concurrently in BOTH mode via
    // ESP-IDF coexistence -- no application-level interleaving (ADR-0060).
    if (now - lastWiFiScanMs_ >= scanInterval) {
        if (mode == RadioMode::WIFI_ONLY || mode == RadioMode::BOTH) {
            processWiFiScan();
        }
#ifdef ARDUINO
        if (mode == RadioMode::BLE_ONLY || mode == RadioMode::BOTH) {
            processBLEScan();
        } else if (BLEScanner::isInitialized()) {
            // WiFi-only: release the controller so this session pays neither the
            // ~58.6 KB heap nor the scan-throughput cost (ADR-0060).
            BLEScanner::getInstance().deinit();
        }
#endif
#if defined(WARDRIVE_HEAP_TRACE) && defined(ARDUINO)
        // Sample once per scan cycle (~scanInterval) so the slope is dense enough to read,
        // unlike the 30 s SD flush. Placed after the scans so it reflects post-scan heap.
        appendHeapSample(now, session_.networksFound, static_cast<uint8_t>(mode));
#endif
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
        LOG_I("WARDRV", "Heap: %lu, seen: %lu, pending: %lu", 
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)session_.networksFound,
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
    // Exact unique count: incremented once per genuinely-new (mac,type), the Bloom cannot
    // count so this is the authoritative figure (ADR-0103).
    return session_.networksFound;
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
    uint32_t newCount = 0;

    for (const auto& net : results) {
        if (ingestObservation(net.bssid, ObservationType::WIFI, net.rssi, net.channel,
                              static_cast<uint8_t>(net.security),
                              net.ssid.empty() ? nullptr : net.ssid.c_str())) {
            newCount++;
        }
    }

    if (newCount > 0) {
        LOG_I("WARDRV", "Found %lu new networks (total: %lu)",
              (unsigned long)newCount, (unsigned long)session_.networksFound);
    }
}

bool WardrivingManager::ingestObservation(const uint8_t* mac, ObservationType type,
                                          int rssi, uint8_t channel,
                                          uint8_t encryptionType, const char* name) {
    uint32_t now = millis();

    // O(1) lookup: is this (mac, type) already in our hash set?
    if (hasSeenBSSID(mac, type)) {
        // Already seen - update the matching pending entry if it is still buffered.
        for (auto& pending : pendingNetworks_) {
            if (pending.type == type && memcmp(pending.bssid, mac, 6) == 0) {
                if (rssi > pending.rssi) {
                    pending.rssi = static_cast<int8_t>(rssi);
                }
                pending.lastSeenMs = now;
                pending.seenCount++;
                if (!pending.hasGPS) {
                    updateNetworkGPS(&pending);
                }
                break;
            }
        }
        return false;
    }

    // New observation.
    NetworkEntry* entry = addNewNetwork(mac, type);
    if (!entry) {
        // Max observations reached or buffer full
        return false;
    }

    entry->rssi = static_cast<int8_t>(rssi);
    entry->channel = channel;
    entry->encryptionType = encryptionType;
    entry->lastSeenMs = now;
    entry->seenCount = 1;
    if (name && name[0] != '\0') {
        strncpy(entry->ssid, name, sizeof(entry->ssid) - 1);
    }
    updateNetworkGPS(entry);
    return true;
}

#ifdef ARDUINO
void WardrivingManager::processBLEScan() {
    auto& scanner = BLEScanner::getInstance();

    // Self-initialize: bring the controller + continuous scan up on first use so a
    // mid-session switch into a BLE mode is safe. init()/startScan() are idempotent.
    if (!BLEScanner::isInitialized()) {
        scanner.init();
    }
    if (!scanner.isScanning()) {
        // Passive + continuous: active scan would retain a NimBLEAdvertisedDevice per unique
        // address for the whole session (ADR-0103 phase 3), fatal under BLE MAC randomization.
        scanner.startScan(0, false);   // 0 = continuous (manual stop on session end)
    }

    uint32_t newCount = 0;
    auto devices = scanner.getDevices();
    for (const auto& dev : devices) {
        // NimBLEAddress::getNative() is little-endian; reverse to canonical BD_ADDR
        // order so the CSV MAC matches the human-readable address (confirm on-device).
        const uint8_t* native = dev.address.getNative();
        uint8_t addr[6];
        for (int i = 0; i < 6; i++) {
            addr[i] = native[5 - i];
        }

        // BLE rows carry channel 0 and no WiFi encryption (WiGLE BT/BLE spec, D6).
        if (ingestObservation(addr, ObservationType::BLE, dev.rssi, 0, 0,
                              dev.name.empty() ? nullptr : dev.name.c_str())) {
            newCount++;
        }
    }

    if (newCount > 0) {
        LOG_I("WARDRV", "Found %lu new BLE devices (total: %lu)",
              (unsigned long)newCount, (unsigned long)session_.networksFound);
    }
}
#endif

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

bool WardrivingManager::hasSeenBSSID(const uint8_t* bssid, ObservationType type) const {
    uint64_t key = observationKey(bssid, type);
    return seenFilter_.test(key);
}

NetworkEntry* WardrivingManager::addNewNetwork(const uint8_t* bssid, ObservationType type) {
    uint64_t key = observationKey(bssid, type);

    // Cap reached: the Bloom is sized for MAX_NETWORKS at ~1% FP, so past it accuracy
    // degrades. Stop adding and warn once (fail loud) rather than silently dropping devices
    // at a climbing false-positive rate (ADR-0103).
    if (session_.networksFound >= MAX_NETWORKS) {
        if (!saturationWarned_) {
            LOG_W("WARDRV", "Dedup at capacity (%u) - new devices dropped, coverage degraded",
                  (unsigned)MAX_NETWORKS);
            saturationWarned_ = true;
        }
        return nullptr;
    }

    // Record in the bounded dedup filter and bump the exact unique counter
    seenFilter_.add(key);
    session_.networksFound++;

    // Add to pending buffer for GPS tagging
    // If buffer is full, flush first
    if (pendingNetworks_.size() >= PENDING_BUFFER_SIZE) {
        flushToSD();
    }

    // Create new entry in pending buffer
    NetworkEntry entry;
    memcpy(entry.bssid, bssid, 6);
    entry.type = type;
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
    
    LOG_I("WARDRV", "Flushed %lu networks to SD (total: %lu)", 
          (unsigned long)flushedCount, (unsigned long)session_.networksFound);
    
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
            LOG_E("WARDRV", "Failed to open CSV: %s", csvPath_);
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
    
    // Real wall-clock FirstSeen is reconstructed inside formatWigleRow from these live
    // clock values; the formatter itself stays pure so the row is unit-testable.
    char row[160];
    formatWigleRow(entry, millis(), TimeManager::getInstance().now(), row, sizeof(row));
    file.print(row);

    file.close();
    return true;
}

void WardrivingManager::formatWigleRow(const NetworkEntry& entry, uint32_t currentMillis,
                                       time_t currentEpoch, char* out, size_t outLen) {
    // Address: same AA:BB:CC:DD:EE:FF formatting for WiFi BSSID and BLE BD_ADDR.
    char addrStr[18];
    snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             entry.bssid[0], entry.bssid[1], entry.bssid[2],
             entry.bssid[3], entry.bssid[4], entry.bssid[5]);

    // Real wall-clock FirstSeen reconstructed from device uptime (see formatWigleTimestamp).
    char timeStr[32];
    formatWigleTimestamp(currentMillis, currentEpoch, entry.firstSeenMs, timeStr, sizeof(timeStr));

    // The three fields that differ by observation type (ADR-0060 / slice-0061 D6).
    const char* authMode;
    const char* typeStr;
    uint8_t channel;
    if (entry.type == ObservationType::BLE) {
        // WiGLE 1.4: Bluetooth rows use channel 0 and a capability list; the spec's
        // reference BLE example is "Misc [LE]" (api.wigle.net/csvFormat-1_4.html).
        authMode = "Misc [LE]";
        channel = 0;
        typeStr = "BLE";
    } else {
        // Map encryption type to WiGLE AuthMode capability string.
        authMode = "[WPA2-PSK-CCMP][ESS]";  // Default
        switch (entry.encryptionType) {
            case 0: authMode = "[ESS]"; break;                   // OPEN
            case 1: authMode = "[WEP][ESS]"; break;              // WEP
            case 2: authMode = "[WPA-PSK][ESS]"; break;          // WPA_PSK
            case 3: authMode = "[WPA2-PSK-CCMP][ESS]"; break;    // WPA2_PSK
            case 4: authMode = "[WPA-PSK+WPA2-PSK][ESS]"; break; // WPA_WPA2_PSK
            case 5: authMode = "[WPA2-EAP][ESS]"; break;         // WPA2_ENTERPRISE
            case 6: authMode = "[WPA3][ESS]"; break;             // WPA3_PSK
            case 7: authMode = "[WPA2-PSK+WPA3][ESS]"; break;    // WPA2_WPA3_PSK
        }
        channel = entry.channel;
        typeStr = "WIFI";
    }

    snprintf(out, outLen, "%s,\"%s\",%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,%s\n",
             addrStr,
             entry.ssid,
             authMode,
             timeStr,
             channel,
             entry.rssi,
             entry.hasGPS ? entry.latitude : 0.0,
             entry.hasGPS ? entry.longitude : 0.0,
             entry.hasGPS ? entry.altitude : 0.0f,
             entry.hasGPS ? 10.0f : 0.0f,   // Accuracy estimate
             typeStr);
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

uint64_t WardrivingManager::observationKey(const uint8_t* mac, ObservationType type) {
    // Low 48 bits: the 6 MAC bytes (unchanged from the historical BSSID hash, so a
    // WIFI key == the old value and existing dedup is preserved). High bits: the
    // observation type, so a BLE BD_ADDR never collides with a same-valued WiFi BSSID.
    uint64_t hash = 0;
    for (int i = 0; i < 6; i++) {
        hash = (hash << 8) | mac[i];
    }
    return hash | (static_cast<uint64_t>(type) << 48);
}

void WardrivingManager::generateSessionId(char* buffer) {
    // Size comes from the real destination buffer, not a literal (see formatSessionId).
    formatSessionId(TimeManager::getInstance().now(), millis(),
                    buffer, sizeof(session_.sessionId));
}

// Below this epoch (2001-09-09) a computed timestamp cannot be real wall-clock:
// TimeManager::now() returns 0 when it has never synced, so any eventEpoch at or
// below this is the unsynced case, not a genuine early-2000s observation.
static constexpr time_t MIN_SANE_EPOCH = 1000000000;

void WardrivingManager::formatWigleTimestamp(uint32_t currentMillis, time_t currentEpoch,
                                             uint32_t eventMs, char* out, size_t outLen) {
    // How long ago (device uptime) the network was first seen, mapped onto real time.
    uint32_t elapsedMs = currentMillis - eventMs;
    time_t eventEpoch = currentEpoch - static_cast<time_t>(elapsedMs / 1000);

    struct tm timeinfo;
    if (eventEpoch >= MIN_SANE_EPOCH && gmtime_r(&eventEpoch, &timeinfo) != nullptr) {
        strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &timeinfo);
    } else {
        // Clock was never synced to a real epoch: state "time unknown" with the
        // universally recognised epoch-zero sentinel rather than a fabricated date.
        snprintf(out, outLen, "1970-01-01 00:00:00");
    }
}

// A 4-digit year is the widest the fixed "WD-YYYYMMDD-HHMMSS" id can hold; a clock reading
// beyond it (corrupt or far-future) must fall back, not overflow the buffer with a
// truncated datestamp.
static constexpr int MAX_SESSION_ID_YEAR = 9999;

void WardrivingManager::formatSessionId(time_t now, uint32_t uptimeMs,
                                        char* out, size_t outLen) {
    struct tm timeinfo;
    int year = 0;
    if (now >= MIN_SANE_EPOCH && gmtime_r(&now, &timeinfo) != nullptr &&
        (year = timeinfo.tm_year + 1900) <= MAX_SESSION_ID_YEAR) {
        // The modulo reductions are no-ops on this validated data (year is bounded above,
        // and gmtime_r's own fields are already in range); they hand the compiler the
        // field-width proof that satisfies -Wformat-truncation without a pragma.
        snprintf(out, outLen, "WD-%04u%02u%02u-%02u%02u%02u",
                 static_cast<unsigned>(year) % 10000u,
                 static_cast<unsigned>(timeinfo.tm_mon + 1) % 100u,
                 static_cast<unsigned>(timeinfo.tm_mday) % 100u,
                 static_cast<unsigned>(timeinfo.tm_hour) % 100u,
                 static_cast<unsigned>(timeinfo.tm_min) % 100u,
                 static_cast<unsigned>(timeinfo.tm_sec) % 100u);
    } else {
        // No real time sync (or an out-of-range clock): uptime-hex id, never a truncated date.
        snprintf(out, outLen, "WD-%08lX", static_cast<unsigned long>(uptimeMs / 1000));
    }
}

} // namespace wardriving
} // namespace adversary
