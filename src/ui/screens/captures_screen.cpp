/**
 * @file captures_screen.cpp
 * @brief Captures browser screen implementation
 */

#include "captures_screen.h"
#include "../../modules/network/pwncrack_service.h"

#ifdef ESP32
#include <SD.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "../../modules/storage/capture_registry.h"
#include "../../modules/storage/wpasec_cache.h"
#include "../../modules/wifi/wifi_connection.h"
#include "../../modules/system/system_manager.h"
#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif
#endif

namespace adversary {

#ifdef ESP32
// One-line heap trace: free + largest contiguous internal-DRAM block. The
// largest block is what actually decides whether a ~62KB TLS handshake
// (16KB IN + 16KB OUT record buffers) can allocate; free heap alone hides
// fragmentation. Tagged so a sync session is greppable on the serial log.
static void logHeap(const char* where) {
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("[Captures] heap @ %s: free=%u largest=%u\n",
                  where, (unsigned)ESP.getFreeHeap(), (unsigned)largest);
}

// Draw sync progress straight to the panel (NOT the canvas). During a bulk
// sync the canvas is purged for the whole run, so the normal render path is
// skipped and the screen would otherwise freeze on the last frame. These
// direct M5.Display draws land on top of that frozen frame and give live
// per-file progress; the canvas restore at the end repaints over them.
static void drawSyncProgressDirect(int pos, int total, const char* ssid, const char* svc) {
    const int w = 200, h = 66;
    const int x = (config::SCREEN_WIDTH  - w) / 2;
    const int y = (config::SCREEN_HEIGHT - h) / 2;

    M5.Display.fillRoundRect(x, y, w, h, 4, theme::BG_SECONDARY());
    M5.Display.drawRoundRect(x, y, w, h, 4, theme::ACCENT());

    M5.Display.setTextDatum(top_left);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(theme::ACCENT());
    M5.Display.setCursor(x + 8, y + 6);
    M5.Display.printf("Cloud Sync  %d/%d", pos, total);

    // SSID (truncated) + service tag on the next line.
    char line[40];
    snprintf(line, sizeof(line), "%.20s [%s]", ssid ? ssid : "?", svc);
    M5.Display.setTextColor(theme::TEXT_PRIMARY());
    M5.Display.setCursor(x + 8, y + 24);
    M5.Display.print(line);

    // Progress bar (files completed so far = pos-1 of total).
    const int bx = x + 8, by = y + 44, bw = w - 16, bh = 8;
    M5.Display.drawRect(bx, by, bw, bh, theme::TEXT_SECONDARY());
    int done = (total > 0) ? ((pos - 1) * (bw - 2)) / total : 0;
    if (done > 0) M5.Display.fillRect(bx + 1, by + 1, done, bh - 2, theme::ACCENT());
}
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

CapturesScreen::CapturesScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , screenState_(CapturesScreenState::CATEGORY_SELECT)
    , selectedCategory_(CaptureCategory::HANDSHAKES)
    , categorySelection_(0)
    , passwordsMasked_(true)
    , pendingLoad_(0)
    , pendingRefresh_(0)
    , pendingUpload_(0)
    , selection_(0)
    , scrollOffset_(0)
{
    memset(categoryCounts_, 0, sizeof(categoryCounts_));
    memset(credentialSSID_, 0, sizeof(credentialSSID_));
    memset(pendingUploadFilename_, 0, sizeof(pendingUploadFilename_));
    pendingWigleUpload_ = 0;
    memset(pendingWigleFilename_, 0, sizeof(pendingWigleFilename_));
}

CapturesScreen::~CapturesScreen() {
    hide();
}

// ============================================================================
// Lifecycle Methods
// ============================================================================

void CapturesScreen::init() {
    // Set counts to -1 (unknown) - will be populated lazily when category is entered
    // This avoids slow SD card enumeration on screen entry
    for (int i = 0; i < CATEGORY_COUNT; i++) {
        categoryCounts_[i] = static_cast<size_t>(-1);
    }
}

void CapturesScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    screenState_ = CapturesScreenState::CATEGORY_SELECT;
    categorySelection_ = 0;
    init();
}

void CapturesScreen::hide() {
    visible_ = false;
    files_.clear();
}

void CapturesScreen::update() {
    if (!visible_) return;

    // Handle deferred file loading - wait one frame for render to show "Loading..."
    // pendingLoad_ counts down: 2 -> 1 (render happens) -> 0 (now load)
    if (pendingLoad_) {
        if (--pendingLoad_ == 0) {
            loadFileList();
            // Update count from loaded files
            categoryCounts_[static_cast<int>(selectedCategory_)] = getListSize();
            needsRedraw_ = true;
        }
    }
    
    // Handle deferred sync upload - countdown allows the progress toast to render
    // first. Each tick runs ONE job (one service, one file); the canvas is purged
    // once on the first job and restored on the last, with WiFi held up across the
    // whole run. See the detailed note below.
    if (pendingUpload_) {
        if (--pendingUpload_ == 0) {
            if (!uploadAllActive_ || uploadAllPos_ >= (int)syncQueue_.size()) {
                // Defensive: nothing queued (shouldn't happen).
                uploadAllActive_ = false;
                syncQueue_.clear();
                uploadAllPos_ = 0;
                needsRedraw_ = true;
                return;
            }

            SyncJob job = syncQueue_[uploadAllPos_];
            bool lastJob = (uploadAllPos_ + 1 >= (int)syncQueue_.size());

            // Purge the 64800-byte canvas for the WHOLE run, not per file.
            // A TLS handshake costs ~62KB (16KB IN + 16KB OUT record buffers,
            // precompiled in the mbedTLS lib — not shrinkable). This board has no
            // usable PSRAM, so the canvas lives in internal DRAM and competes with
            // those buffers directly. Keeping it resident works only on a healthy
            // heap; after an Auto Hunt / capture session fragments DRAM the two
            // 16KB buffers can't find room alongside the canvas and connect()
            // fails ("Connection failed") — even though the largest free block can
            // still be >30KB, so the old adaptive purge never tripped.
            //
            // prepareForMemoryIntensiveTask() is state-guarded: it purges once (on
            // this first job), then early-returns on jobs 2..N. keepWifiUp keeps
            // the single association across the run (per-file WiFi off→on crashed
            // the stack after ~20 files). The canvas is restored ONCE on the last
            // job below, after WiFi is torn down — so we avoid the per-file
            // restore-starvation that failed after ~5 files. Trade-off: the UI
            // can't render while the canvas is gone, so progress freezes on the
            // last drawn frame until the run finishes (ESC still stops it).
            SystemManager::getInstance().prepareForMemoryIntensiveTask(/*purgeCanvas=*/true,
                                                                       /*keepWifiUp=*/true);
            if (uploadAllPos_ == 0) logHeap("sync start (post-purge)");
            delay(300);  // let RTOS reclaim the freed buffers

            // Connect once for the run; reconnect only if the link actually dropped.
            auto& wifiConn = WiFiConnection::getInstance();
            if (!WiFi.isConnected()) {
                WiFi.mode(WIFI_STA);
                wifiConn.autoConnect();
                uint32_t startWait = millis();
                while (!WiFi.isConnected() && (millis() - startWait) < 10000) {
                    wifiConn.update();
                    delay(100);
                }
            }

            if (!WiFi.isConnected()) {
                showErrorToast("WiFi connect failed");
                WiFi.mode(WIFI_OFF);
                SystemManager::getInstance().restoreFromMemoryIntensiveTask();
                // Abort the run (don't leave it stalled).
                uploadAllActive_ = false;
                syncQueue_.clear();
                uploadAllPos_ = 0;
                needsRedraw_ = true;
                return;
            }

            // Dispatch the job to the right service with the right artifact.
            // deferCleanup=true keeps WiFi up + skips the per-file rescan; we own
            // the single disconnect on the last job.
            logHeap("pre-connect");
            const char* svcName = (job.svc == SyncService::WPASEC) ? "WPA-SEC" : "pwncrack";
            // Live progress straight to the panel (canvas is purged this whole run).
            drawSyncProgressDirect(uploadAllPos_ + 1, (int)syncQueue_.size(), job.ssid, svcName);
            char filepath[160];
            if (job.svc == SyncService::WPASEC) {
                snprintf(filepath, sizeof(filepath),
                         "/adversary/captures/handshakes/%s.pcap", job.ssid);
                WpaSecService::getInstance().uploadHandshake(filepath, /*deferCleanup=*/true);
            } else {
                snprintf(filepath, sizeof(filepath),
                         "/adversary/captures/handshakes/%s.22000", job.ssid);
                PwncrackService::getInstance().uploadHandshake(filepath, /*quiet=*/true);
            }

            // Tear WiFi down once, on the final job (reclaims ~34KB, clean idle),
            // then restore the canvas from the cleanest possible heap state.
            if (lastJob) {
                wifiConn.disconnect();
                WiFi.mode(WIFI_OFF);
                logHeap("pre-restore (wifi off)");
                SystemManager::getInstance().restoreFromMemoryIntensiveTask();
                logHeap("post-restore");
            }

            // Reflect the registry's ACTUAL post-upload status in the WPA-SEC list
            // cache (the service set it in the manifest, the source of truth).
            // pwncrack status is read live from the summaries at render time.
            const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
            for (size_t i = 0; i < handshakeIndices_.size(); i++) {
                size_t regIdx = handshakeIndices_[i];
                if (regIdx < summaries.size() && strcmp(summaries[regIdx].ssid, job.ssid) == 0) {
                    if (i < wpaSecStatusCache_.size()) wpaSecStatusCache_[i] = summaries[regIdx].wpaSecStatus;
                    break;
                }
            }

            needsRedraw_ = true;

            // This cycle finished (canvas restored). Chain the next job, or done.
            kickNextSync();
        }
    }

    // Handle deferred WPA-SEC refresh - countdown allows "Refreshing..." toast to render first
    // pendingRefresh_ counts down: 2-> 1 (render happens) -> 0 (now fetch)
    if (pendingRefresh_) {
        if (--pendingRefresh_ == 0) {
            // Memory stabilization before WiFi/SSL — but KEEP the canvas.
            // A cracked-results fetch (HTTPClient + WiFiClientSecure) only needs
            // MIN_CONTIG_FOR_TLS (~20KB) contiguous, which fits alongside the
            // resident 64800-byte canvas. Purging the canvas frees that block to
            // TLS, but the post-fetch heap is then too fragmented to re-allocate
            // 64800 contiguous, leaving the UI dead. Not purging removes the
            // restore step entirely; if heap is ever too tight the TLS gate bails
            // with a toast instead of freezing.
            SystemManager::getInstance().prepareForMemoryIntensiveTask(false);

            // STABILIZATION: Yield to allow RTOS memory reclamation
            delay(500);

            // Connect WiFi on-demand
            auto& wifiConn = WiFiConnection::getInstance();
            WiFi.mode(WIFI_STA);
            wifiConn.autoConnect();
            
            // Wait for connection (blocking, max 10 seconds)
            uint32_t startWait = millis();
            while (!WiFi.isConnected() && (millis() - startWait) < 10000) {
                wifiConn.update();
                delay(100);
            }
            
            if (!WiFi.isConnected()) {
                showErrorToast("WiFi connect failed");
                SystemManager::getInstance().restoreFromMemoryIntensiveTask();
                needsRedraw_ = true;
                return;
            }
            
            // Blocking HTTPS call - fetches cracked results and updates metadata.
            // Fetch from every keyed service in the one WiFi session (each is a
            // validated-TLS GET; no canvas purge needed, see the note above).
            if (WpaSecService::getInstance().hasApiKey()) {
                int crackedCount = WpaSecService::getInstance().fetchCrackedResults();
                (void)crackedCount;
            }
            if (PwncrackService::getInstance().hasApiKey()) {
                // quiet: a 404 (no potfile yet) shouldn't shout over the WPA-SEC
                // refresh result; a real pwncrack crack still toasts.
                int pcCracked = PwncrackService::getInstance().fetchCrackedResults(/*quiet=*/true);
                (void)pcCracked;
            }

            // Disconnect WiFi after SSL
            wifiConn.disconnect();

            // Restore state
            SystemManager::getInstance().restoreFromMemoryIntensiveTask();
            
            // Update status cache from the fresh registry summaries
            const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
            for (size_t i = 0; i < handshakeIndices_.size(); i++) {
                size_t regIdx = handshakeIndices_[i];
                if (regIdx < summaries.size()) {
                    wpaSecStatusCache_[i] = summaries[regIdx].wpaSecStatus;
                }
            }
            
            needsRedraw_ = true;
        }
    }
    
    // Handle deferred WiGLE upload - same pattern as WPA-SEC
    if (pendingWigleUpload_) {
        if (--pendingWigleUpload_ == 0) {
            // Memory stabilization before WiFi/SSL
            SystemManager::getInstance().prepareForMemoryIntensiveTask();
            delay(500);

            // Connect WiFi on-demand
            auto& wifiConn = WiFiConnection::getInstance();
            WiFi.mode(WIFI_STA);
            wifiConn.autoConnect();
            
            uint32_t startWait = millis();
            while (!WiFi.isConnected() && (millis() - startWait) < 10000) {
                wifiConn.update();
                delay(100);
            }

            if (!WiFi.isConnected()) {
                showErrorToast("WiFi connect failed");
                SystemManager::getInstance().restoreFromMemoryIntensiveTask();
                needsRedraw_ = true;
                return;
            }

            // Upload CSV to WiGLE
            char filepath[128];
            snprintf(filepath, sizeof(filepath), "/adversary/captures/wardriving/%s", pendingWigleFilename_);
            WigleService::getInstance().uploadCSV(filepath);
            wifiConn.disconnect();
            SystemManager::getInstance().restoreFromMemoryIntensiveTask();
            needsRedraw_ = true;
        }
    }
}

// ============================================================================
// Input Handling
// ============================================================================

bool CapturesScreen::handleInput(char key) {
    needsRedraw_ = true;

    // While a sync run is active, ESC/back requests a graceful stop (after the
    // file currently uploading finishes — input is only seen between files).
    if (uploadAllActive_ && (key == '`' || key == 0x1B)) {
        if (!syncCancelRequested_) {
            syncCancelRequested_ = true;
            showToast("Stopping sync...");
        }
        return true;
    }

    // Upload action menu (modal) takes input while visible.
    if (uploadMenu_.isVisible()) {
        return uploadMenu_.handleInput(key);
    }

    switch (screenState_) {
        case CapturesScreenState::CATEGORY_SELECT:
            switch (key) {
                case ';':  // Up (wraps)
                    categorySelection_ = (categorySelection_ + CATEGORY_COUNT - 1) % CATEGORY_COUNT;
                    return true;
                case '.':  // Down (wraps)
                    categorySelection_ = (categorySelection_ + 1) % CATEGORY_COUNT;
                    return true;
                case '\n':
                case '\r':
                    selectedCategory_ = static_cast<CaptureCategory>(categorySelection_);
                    files_.clear();  // Clear for loading state
                    pendingLoad_ = 2;  // Countdown: 2->1 (render) -> 0 (load)
                    screenState_ = CapturesScreenState::FILE_LIST;
                    selection_ = 0;
                    scrollOffset_ = 0;
                    needsRedraw_ = true;
                    return true;
                case '`':
                    shouldExit_ = true;
                    return true;
            }
            break;
            
        case CapturesScreenState::FILE_LIST:
            // Handle footer input (space toggle, navigation, Enter dispatch)
            {
                char footerAction = 0;
                if (footerHints_.handleInputWithDispatch(key, footerAction)) {
                    if (footerAction) return handleInput(footerAction);  // Dispatch action
                    return true;  // Consumed (navigation/toggle)
                }
            }
            
            // List navigation and direct key shortcuts
            switch (key) {
                case ';': {  // Up (wraps to last)
                    int n = (int)getListSize();
                    if (n > 0) {
                        selection_ = (selection_ - 1 + n) % n;
                        if (selection_ < scrollOffset_) scrollOffset_ = selection_;
                        else if (selection_ >= scrollOffset_ + VISIBLE_FILES)
                            scrollOffset_ = selection_ - VISIBLE_FILES + 1;
                    }
                    return true;
                }
                case '.': {  // Down (wraps to first)
                    int n = (int)getListSize();
                    if (n > 0) {
                        selection_ = (selection_ + 1) % n;
                        if (selection_ >= scrollOffset_ + VISIBLE_FILES)
                            scrollOffset_ = selection_ - VISIBLE_FILES + 1;
                        else if (selection_ < scrollOffset_) scrollOffset_ = selection_;
                    }
                    return true;
                }
                case 'd':
                case 'D':
                    if (getListSize() > 0) {
                        screenState_ = CapturesScreenState::DELETE_CONFIRM;
                    }
                    return true;
                case 'i':
                case 'I':
                    if (getListSize() > 0) {
                        screenState_ = CapturesScreenState::FILE_INFO;
                    }
                    return true;
                case 'v':
                case 'V':
                    if (!files_.empty() && selectedCategory_ == CaptureCategory::CREDENTIALS) {
                        loadCredentials();
                        screenState_ = CapturesScreenState::CREDENTIAL_VIEW;
                    }
                    return true;
                case 'u':
                case 'U':
                    // Upload to WPA-SEC (handshakes) or WiGLE (wardriving)
                    if (getListSize() > 0 && selectedCategory_ == CaptureCategory::HANDSHAKES &&
                        !pendingUpload_ && !uploadAllActive_) {
                        // Offer single vs bulk via a modal (like the scanner action menu)
                        openUploadMenu();
                    } else if (getListSize() > 0 && selectedCategory_ == CaptureCategory::WARDRIVING && !pendingWigleUpload_) {
                        showToast("Uploading to WiGLE...");
                        int sel = selection_;
                        if (sel >= 0 && sel < (int)files_.size()) {
                            strncpy(pendingWigleFilename_, files_[sel].name, sizeof(pendingWigleFilename_));
                            pendingWigleFilename_[sizeof(pendingWigleFilename_) - 1] = '\0';
                        }
                        pendingWigleUpload_ = 2;
                        needsRedraw_ = true;
                    }
                    return true;
                case 'r':
                case 'R':
                    // Refresh WPA-SEC status for handshakes (deferred to allow toast to render)
                    if (selectedCategory_ == CaptureCategory::HANDSHAKES && !pendingRefresh_) {
                        showToast("Refreshing...");
                        
                        // Persistent list: we no longer clear indices/cache here
                        // The 64KB Canvas Purge provides enough room for these small vectors.
                        
                        pendingRefresh_ = 2;  // Countdown: 2->1 (render) -> 0 (fetch)
                        needsRedraw_ = true;
                    }
                    return true;
                case '`':
                    screenState_ = CapturesScreenState::CATEGORY_SELECT;
                    init();  // Refresh counts
                    return true;
            }
            break;
            
        case CapturesScreenState::DELETE_CONFIRM:
            switch (key) {
                case 'y':
                case 'Y':
                    deleteSelectedFile();
                    loadFileList();
                    if (getListSize() == 0) {
                        screenState_ = CapturesScreenState::CATEGORY_SELECT;
                        init();
                    } else {
                        screenState_ = CapturesScreenState::FILE_LIST;
                        if (selection_ >= (int)getListSize()) {
                            selection_ = getListSize() - 1;
                        }
                    }
                    return true;
                case 'n':
                case 'N':
                case '`':
                    screenState_ = CapturesScreenState::FILE_LIST;
                    return true;
            }
            break;
            
        case CapturesScreenState::FILE_INFO:
            switch (key) {
                case 'm':
                case 'M':
                    // Toggle password mask (for cracked handshakes)
                    passwordsMasked_ = !passwordsMasked_;
                    return true;
                case '`':
                case '\n':
                case '\r':
                    screenState_ = CapturesScreenState::FILE_LIST;
                    return true;
            }
            break;
            
        case CapturesScreenState::CREDENTIAL_VIEW:
            switch (key) {
                case 'm':
                case 'M':
                    passwordsMasked_ = !passwordsMasked_;
                    return true;
                case '`':
                case '\n':
                case '\r':
                    screenState_ = CapturesScreenState::FILE_LIST;
                    credentials_.clear();
                    return true;
            }
            break;
            
        default:
            break;
    }
    
    return false;
}

// ============================================================================
// File Operations
// ============================================================================

void CapturesScreen::loadFileList() {
    files_.clear();
    handshakeIndices_.clear();
    wpaSecStatusCache_.clear();
    
#ifdef ESP32
    uint32_t startTime = millis();
    
    // OPTIMIZATION: Use sorted indices into CaptureRegistry (saves ~9KB vs copying)
    // Only for HANDSHAKES - registry is normally populated at boot, but may have been
    // cleared by memory-intensive tasks (like BLE).
    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        auto& registry = CaptureRegistry::getInstance();
        
        // Lazy-load registry data if it was cleared
        if (registry.getHandshakeCount() == 0) {
            Serial.println("[Captures] Registry empty, rescanning summaries (lazy load)...");
            registry.rescanSummaries();
        }
        
        const auto& summaries = registry.getHandshakeSummaries();
        
        // Create indices 0, 1, 2, ... N-1
        handshakeIndices_.reserve(summaries.size());
        for (size_t i = 0; i < summaries.size(); i++) {
            handshakeIndices_.push_back(i);
        }
        
        // Sort indices alphabetically by SSID (lastWrite removed from struct)
        std::sort(handshakeIndices_.begin(), handshakeIndices_.end(), 
            [&summaries](size_t a, size_t b) {
                return strcmp(summaries[a].ssid, summaries[b].ssid) < 0;
            });
        
        // Get status directly from summary (no longer lazy-loaded from WpaSecCache)
        wpaSecStatusCache_.resize(summaries.size());
        for (size_t i = 0; i < handshakeIndices_.size(); i++) {
            size_t regIdx = handshakeIndices_[i];
            wpaSecStatusCache_[i] = summaries[regIdx].wpaSecStatus;
        }
        
        uint32_t elapsed = millis() - startTime;
        Serial.printf("[Captures] Loaded %zu handshakes via indices (%lums)\n", 
                      handshakeIndices_.size(), elapsed);
        return;
    }
    
    // For other categories, scan SD (credentials/packets are less common)
    const char* path = getCategoryPath(selectedCategory_);
    
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            bool include = false;
            
            switch (selectedCategory_) {
                case CaptureCategory::CREDENTIALS:
                    include = strstr(name, ".json") != nullptr;
                    break;
                case CaptureCategory::PACKETS:
                    include = strstr(name, ".pcapng") != nullptr || strstr(name, ".pcap") != nullptr;
                    break;
                case CaptureCategory::RFID:
                    include = strstr(name, ".nfc") != nullptr;
                    break;
                case CaptureCategory::WARDRIVING:
                    include = strstr(name, ".csv") != nullptr;
                    break;
                default:
                    include = true;
            }
            
            if (include) {
                CaptureFileEntry fe;
                strncpy(fe.name, name, sizeof(fe.name) - 1);
                fe.size = entry.size();
                fe.lastWrite = entry.getLastWrite();
                files_.push_back(fe);
            }
        }
        entry.close();
    }
    dir.close();
    
    // Sort by newest first
    std::sort(files_.begin(), files_.end(), [](const CaptureFileEntry& a, const CaptureFileEntry& b) {
        return a.lastWrite > b.lastWrite;
    });
    
    uint32_t elapsed = millis() - startTime;
    Serial.printf("[Captures] Loaded %zu files from SD (%lums)\n", files_.size(), elapsed);
#endif
}

void CapturesScreen::deleteSelectedFile() {
#ifdef ESP32
    size_t listSize = getListSize();
    int sel = selection_;
    if (sel < 0 || sel >= (int)listSize) return;
    
    const char* path = getCategoryPath(selectedCategory_);
    const char* filename = getSelectedFilename();
    if (!filename) return;
    
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", path, filename);
    
    if (SD.remove(filepath)) {
        Serial.printf("[Captures] Deleted: %s\n", filepath);
        
        // Also delete associated .json metadata file for handshakes
        if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
            // Derive SSID from filename (remove .pcap extension)
            char ssid[48];
            strncpy(ssid, filename, sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
            char* ext = strstr(ssid, ".pcap");
            if (ext) *ext = '\0';
            
            // Remove from caches by SSID
            CaptureRegistry::getInstance().removeHandshake(ssid);
            WpaSecCache::getInstance().removeEntry(ssid);
            
            char jsonPath[128];
            strncpy(jsonPath, filepath, sizeof(jsonPath) - 1);
            char* jsonExt = strstr(jsonPath, ".pcap");
            if (jsonExt) {
                strcpy(jsonExt, ".json");
                if (SD.remove(jsonPath)) {
                    Serial.printf("[Captures] Deleted metadata: %s\n", jsonPath);
                }
            }
        }
    } else {
        Serial.printf("[Captures] Failed to delete: %s\n", filepath);
    }
#endif
}

// ============================================================================
// Helper Methods for Index-Based Access
// ============================================================================
// Upload action menu + bulk "Upload all new" (WPA-SEC)
// ============================================================================

void CapturesScreen::appendJobsForIndex(std::vector<SyncJob>& out,
                                        const HandshakeSummary& s,
                                        bool wpaKeyed, bool pcKeyed) {
#ifdef ESP32
    if (wpaKeyed && s.wpaSecStatus == WpaSecStatus::NOT_UPLOADED) {
        SyncJob j;
        j.svc = SyncService::WPASEC;
        strncpy(j.ssid, s.ssid, sizeof(j.ssid) - 1);
        j.ssid[sizeof(j.ssid) - 1] = '\0';
        out.push_back(j);
    }
    // Only queue pwncrack when a {ssid}.22000 exists — tracked in the manifest
    // (RF2_HAS_HC22000) so this is a RAM check, no per-file SD.exists.
    if (pcKeyed && s.pwncrackStatus == WpaSecStatus::NOT_UPLOADED && s.has22000) {
        SyncJob j;
        j.svc = SyncService::PWNCRACK;
        strncpy(j.ssid, s.ssid, sizeof(j.ssid) - 1);
        j.ssid[sizeof(j.ssid) - 1] = '\0';
        out.push_back(j);
    }
#else
    (void)out; (void)s; (void)wpaKeyed; (void)pcKeyed;
#endif
}

void CapturesScreen::enqueueSyncJobs(bool selectedOnly) {
    syncQueue_.clear();
    uploadAllPos_ = 0;
#ifdef ESP32
    bool wpaKeyed = WpaSecService::getInstance().hasApiKey();
    bool pcKeyed = PwncrackService::getInstance().hasApiKey();
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();

    if (selectedOnly) {
        int sel = selection_;
        if (sel >= 0 && sel < (int)handshakeIndices_.size()) {
            size_t r = handshakeIndices_[sel];
            if (r < summaries.size()) appendJobsForIndex(syncQueue_, summaries[r], wpaKeyed, pcKeyed);
        }
    } else {
        for (size_t i = 0; i < handshakeIndices_.size(); i++) {
            size_t r = handshakeIndices_[i];
            if (r < summaries.size()) appendJobsForIndex(syncQueue_, summaries[r], wpaKeyed, pcKeyed);
        }
    }
#else
    (void)selectedOnly;
#endif
}

int CapturesScreen::countPendingSyncJobs(bool selectedOnly) const {
    // RAM-only count for the menu label — NO SD I/O. The pwncrack-eligibility
    // (.22000 exists) is read from the manifest flag (summary.has22000), so this
    // matches the actual run exactly while the modal opens instantly. (The old
    // version did ~1 SD.exists per pending capture, which made the modal lag.)
    int n = 0;
#ifdef ESP32
    bool wpaKeyed = WpaSecService::getInstance().hasApiKey();
    bool pcKeyed = PwncrackService::getInstance().hasApiKey();
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
    auto countOne = [&](const HandshakeSummary& s) {
        if (wpaKeyed && s.wpaSecStatus == WpaSecStatus::NOT_UPLOADED) n++;
        if (pcKeyed && s.pwncrackStatus == WpaSecStatus::NOT_UPLOADED && s.has22000) n++;
    };
    if (selectedOnly) {
        int sel = selection_;
        if (sel >= 0 && sel < (int)handshakeIndices_.size()) {
            size_t r = handshakeIndices_[sel];
            if (r < summaries.size()) countOne(summaries[r]);
        }
    } else {
        for (size_t i = 0; i < handshakeIndices_.size(); i++) {
            size_t r = handshakeIndices_[i];
            if (r < summaries.size()) countOne(summaries[r]);
        }
    }
#else
    (void)selectedOnly;
#endif
    return n;
}

int CapturesScreen::countPendingSyncCaptures(bool selectedOnly) const {
    // Distinct handshakes with at least one pending upload — this is what the
    // user counts in the list. (countPendingSyncJobs counts per-service upload
    // operations, so a 9-handshake batch to two services reads as 18.)
    int n = 0;
#ifdef ESP32
    bool wpaKeyed = WpaSecService::getInstance().hasApiKey();
    bool pcKeyed = PwncrackService::getInstance().hasApiKey();
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
    auto pending = [&](const HandshakeSummary& s) -> bool {
        return (wpaKeyed && s.wpaSecStatus == WpaSecStatus::NOT_UPLOADED) ||
               (pcKeyed && s.pwncrackStatus == WpaSecStatus::NOT_UPLOADED && s.has22000);
    };
    if (selectedOnly) {
        int sel = selection_;
        if (sel >= 0 && sel < (int)handshakeIndices_.size()) {
            size_t r = handshakeIndices_[sel];
            if (r < summaries.size() && pending(summaries[r])) n++;
        }
    } else {
        for (size_t i = 0; i < handshakeIndices_.size(); i++) {
            size_t r = handshakeIndices_[i];
            if (r < summaries.size() && pending(summaries[r])) n++;
        }
    }
#else
    (void)selectedOnly;
#endif
    return n;
}

void CapturesScreen::openUploadMenu() {
    int allCount = countPendingSyncCaptures(false);

    uploadMenu_.setTitle("Sync to services");
    uploadMenu_.setSubtitle(nullptr);
    uploadMenu_.clearItems();
    uploadMenu_.addItem('1', "Sync this", true);
    // ActionMenu keeps the label pointer (no copy), so use a persistent buffer.
    snprintf(uploadAllLabel_, sizeof(uploadAllLabel_), "Sync all new (%d)", allCount);
    uploadMenu_.addItem('2', uploadAllLabel_, allCount > 0);  // disabled when none
    uploadMenu_.addItem('`', "Cancel", true);
    uploadMenu_.setOnAction([this](char action) {
        uploadMenu_.hide();
        needsRedraw_ = true;
        if (action == '1') startSync(true);
        else if (action == '2') startSync(false);
    });
    uploadMenu_.setOnDismiss([this]() { needsRedraw_ = true; });
    uploadMenu_.show();
}

void CapturesScreen::startSync(bool selectedOnly) {
    if (uploadAllActive_ || pendingUpload_) return;

    enqueueSyncJobs(selectedOnly);
    if (syncQueue_.empty()) {
        showToast("Nothing to sync");
        return;
    }

    // Drive the queue through the pendingUpload_ handler: purge the canvas once
    // up front, run every job under that single purge (WiFi held up across the
    // run), then restore once at the end after WiFi is torn down. See the long
    // note in update()'s pendingUpload_ block for the memory rationale.
    uploadAllActive_ = true;
    syncCancelRequested_ = false;
    uploadAllPos_ = 0;
    pendingUpload_ = 2;  // deferred so the toast renders first
    char msg[40];
    snprintf(msg, sizeof(msg), "Syncing 1/%d (ESC=stop)", (int)syncQueue_.size());
    showToast(msg);
    needsRedraw_ = true;
}

void CapturesScreen::kickNextSync() {
    // ESC requested mid-run: stop cleanly (the just-finished file is kept).
    if (syncCancelRequested_) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Sync stopped (%d/%d)",
                 uploadAllPos_ + 1, (int)syncQueue_.size());
        showToast(msg);
        uploadAllActive_ = false;
        syncCancelRequested_ = false;
        syncQueue_.clear();
        uploadAllPos_ = 0;
        needsRedraw_ = true;
        return;
    }

    uploadAllPos_++;
    if (uploadAllPos_ < (int)syncQueue_.size()) {
        pendingUpload_ = 2;  // next single-upload cycle (toast renders first)
        char msg[40];
        snprintf(msg, sizeof(msg), "Syncing %d/%d (ESC=stop)",
                 uploadAllPos_ + 1, (int)syncQueue_.size());
        showToast(msg);
    } else {
        char msg[40];
        snprintf(msg, sizeof(msg), "Sync complete (%d)", (int)syncQueue_.size());
        showSuccessToast(msg);
        uploadAllActive_ = false;
        syncQueue_.clear();
        uploadAllPos_ = 0;
    }
    needsRedraw_ = true;
}

const char* CapturesScreen::getSelectedFilename() const {
    static char derivedFilename[64];  // Static buffer for derived filename
    
    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        int sel = selection_;
        if (sel < 0 || sel >= (int)handshakeIndices_.size()) return nullptr;
        size_t regIdx = handshakeIndices_[sel];
        const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
        if (regIdx >= summaries.size()) return nullptr;
        // Derive filename from SSID
        summaries[regIdx].getFilename(derivedFilename, sizeof(derivedFilename));
        return derivedFilename;
    } else {
        int sel = selection_;
        if (sel < 0 || sel >= (int)files_.size()) return nullptr;
        return files_[sel].name;
    }
}

uint32_t CapturesScreen::getSelectedSize() const {
    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        int sel = selection_;
        if (sel < 0 || sel >= (int)handshakeIndices_.size()) return 0;
        size_t regIdx = handshakeIndices_[sel];
        const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
        if (regIdx >= summaries.size()) return 0;
        return summaries[regIdx].size;
    } else {
        int sel = selection_;
        if (sel < 0 || sel >= (int)files_.size()) return 0;
        return files_[sel].size;
    }
}

size_t CapturesScreen::getListSize() const {
    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        return handshakeIndices_.size();
    } else {
        return files_.size();
    }
}

size_t CapturesScreen::getFileCount(const char* path) {
#ifdef ESP32
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        return 0;
    }
    
    // Determine which extension to filter by based on path
    const char* ext = nullptr;
    if (strstr(path, "handshakes")) ext = ".pcap";
    else if (strstr(path, "credentials")) ext = ".json";
    else if (strstr(path, "packets")) ext = ".pcap";
    else if (strstr(path, "RFID")) ext = ".nfc";
    else if (strstr(path, "wardriving")) ext = ".csv";
    
    size_t count = 0;
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            // Only count files with matching extension
            if (ext == nullptr || strstr(name, ext) != nullptr) {
                count++;
            }
        }
        entry.close();
    }
    dir.close();
    return count;
#else
    (void)path;
    return 0;
#endif
}

// ============================================================================
// Helpers
// ============================================================================

const char* CapturesScreen::getCategoryPath(CaptureCategory cat) const {
    switch (cat) {
        case CaptureCategory::PACKETS:     return config::SD_PACKETS_PATH;
        case CaptureCategory::HANDSHAKES:  return config::SD_HANDSHAKES_PATH;
        case CaptureCategory::CREDENTIALS: return config::SD_CREDENTIALS_PATH;
        case CaptureCategory::RFID:        return "/adversary/captures/RFID";
        case CaptureCategory::WARDRIVING:  return "/adversary/captures/wardriving";
        default: return config::SD_CAPTURES_PATH;
    }
}

const char* CapturesScreen::getCategoryName(CaptureCategory cat) const {
    switch (cat) {
        case CaptureCategory::PACKETS:     return "PACKETS";
        case CaptureCategory::HANDSHAKES:  return "HANDSHAKES";
        case CaptureCategory::CREDENTIALS: return "CREDENTIALS";
        case CaptureCategory::RFID:        return "RFID";
        case CaptureCategory::WARDRIVING:  return "WARDRIVING";
        default: return "CAPTURES";
    }
}

String CapturesScreen::formatSize(uint32_t bytes) {
#ifdef ESP32
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return String(bytes / 1024.0f, 1) + " KB";
    } else {
        return String(bytes / (1024.0f * 1024.0f), 1) + " MB";
    }
#else
    (void)bytes;
    return "0 B";
#endif
}

void CapturesScreen::loadCredentials() {
    credentials_.clear();
    memset(credentialSSID_, 0, sizeof(credentialSSID_));
    
#ifdef ESP32
    int sel = selection_;
    if (sel < 0 || sel >= (int)files_.size()) return;
    
    const char* path = getCategoryPath(selectedCategory_);
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", path, files_[sel].name);
    
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        Serial.printf("[Captures] Failed to open: %s\n", filepath);
        return;
    }
    
    // Read entire file (credential files are small)
    String content = file.readString();
    file.close();
    
    // Simple JSON parsing (looking for ssid, username, password fields)
    // Format: {"ssid": "...", "credentials": [{"username": "...", "password": "..."}, ...]}
    
    // Extract SSID
    int ssidStart = content.indexOf("\"ssid\"");
    if (ssidStart >= 0) {
        int colonPos = content.indexOf(':', ssidStart);
        int quoteStart = content.indexOf('"', colonPos + 1);
        int quoteEnd = content.indexOf('"', quoteStart + 1);
        if (quoteStart >= 0 && quoteEnd > quoteStart) {
            String ssid = content.substring(quoteStart + 1, quoteEnd);
            strncpy(credentialSSID_, ssid.c_str(), sizeof(credentialSSID_) - 1);
        }
    }
    
    // Extract credentials array entries
    int searchStart = 0;
    while (true) {
        int userStart = content.indexOf("\"username\"", searchStart);
        if (userStart < 0) break;
        
        ParsedCredential cred;
        
        // Username
        int colonPos = content.indexOf(':', userStart);
        int quoteStart = content.indexOf('"', colonPos + 1);
        int quoteEnd = content.indexOf('"', quoteStart + 1);
        if (quoteStart >= 0 && quoteEnd > quoteStart) {
            String username = content.substring(quoteStart + 1, quoteEnd);
            strncpy(cred.username, username.c_str(), sizeof(cred.username) - 1);
        }
        
        // Password (search after username)
        int passStart = content.indexOf("\"password\"", quoteEnd);
        if (passStart >= 0) {
            colonPos = content.indexOf(':', passStart);
            quoteStart = content.indexOf('"', colonPos + 1);
            quoteEnd = content.indexOf('"', quoteStart + 1);
            if (quoteStart >= 0 && quoteEnd > quoteStart) {
                String password = content.substring(quoteStart + 1, quoteEnd);
                strncpy(cred.password, password.c_str(), sizeof(cred.password) - 1);
            }
        }
        
        if (cred.username[0] != '\0') {
            credentials_.push_back(cred);
        }
        
        searchStart = quoteEnd + 1;
        if (credentials_.size() >= 10) break;  // Limit to 10 entries for display
    }
    
    Serial.printf("[Captures] Loaded %zu credentials from %s\n", credentials_.size(), files_[sel].name);
#endif
}

// ============================================================================
// Rendering
// ============================================================================

void CapturesScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
#ifdef ESP32
    uint32_t now = millis();
    if (!needsRedraw_ && (now - lastUpdate_) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    lastUpdate_ = now;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (screenState_) {
        case CapturesScreenState::CATEGORY_SELECT:
            drawHeader(canvas, "CAPTURES");
            drawCategorySelect(canvas);
            drawFooter(canvas, ";/. Nav  Enter:Open  `:Back");
            break;
            
        case CapturesScreenState::FILE_LIST: {
            drawHeader(canvas, getCategoryName(selectedCategory_));
            drawFileList(canvas);
            
            // Configure hints based on category and settings. Upload/Refresh are
            // enabled when EITHER cracking service is keyed (sync covers both).
            bool hasApiKey = WpaSecService::getInstance().hasApiKey() ||
                             PwncrackService::getInstance().hasApiKey();
            bool hasFiles = getListSize() > 0;

            if (selectedCategory_ == CaptureCategory::HANDSHAKES && hasFiles) {
                footerHints_.setHints({
                    {'I', "Info", true},
                    {'U', "Sync", hasApiKey},
                    {'R', "Refresh", hasApiKey},
                    {'D', "Del", true}
                });
            } else if (selectedCategory_ == CaptureCategory::CREDENTIALS && hasFiles) {
                footerHints_.setHints({
                    {'D', "Del", true},
                    {'V', "View", true}
                });
            } else if (selectedCategory_ == CaptureCategory::WARDRIVING && hasFiles) {
                bool hasWigleKey = WigleService::getInstance().hasApiKey();
                footerHints_.setHints({
                    {'U', "Upload", hasWigleKey},
                    {'D', "Del", true},
                    {'I', "Info", true}
                });
            } else {
                footerHints_.setHints({
                    {'D', "Del", hasFiles},
                    {'I', "Info", hasFiles}
                });
            }
            
            // Set item count on right side (e.g., "1/10")
            if (hasFiles) {
                static char countBuf[16];
                snprintf(countBuf, sizeof(countBuf), "%d/%zu", selection_ + 1, getListSize());
                footerHints_.setRightContent(countBuf);
            } else {
                footerHints_.setRightContent(nullptr);
            }
            
            footerHints_.render(canvas);
            break;
        }
            
        case CapturesScreenState::DELETE_CONFIRM:
            drawHeader(canvas, "DELETE?");
            drawDeleteConfirm(canvas);
            drawFooter(canvas, "Y:Yes  N:No");
            break;
            
        case CapturesScreenState::FILE_INFO:
            drawHeader(canvas, "FILE INFO");
            drawFileInfo(canvas);
            // Show M:Mask hint for cracked handshakes
            {
                int sel = selection_;
                bool isCracked = false;
                if (selectedCategory_ == CaptureCategory::HANDSHAKES &&
                    sel >= 0 && sel < (int)wpaSecStatusCache_.size()) {
                    isCracked = (wpaSecStatusCache_[sel] == WpaSecStatus::CRACKED);
                    size_t regIdx = handshakeIndices_[sel];
                    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
                    if (regIdx < summaries.size() && summaries[regIdx].pwncrackStatus == WpaSecStatus::CRACKED) {
                        isCracked = true;
                    }
                }
                if (isCracked) {
                    drawFooter(canvas, "M:Mask  Enter/`:Back");
                } else {
                    drawFooter(canvas, "Enter/`:Back");
                }
            }
            break;
            
        case CapturesScreenState::CREDENTIAL_VIEW:
            drawHeader(canvas, "CREDENTIALS");
            drawCredentialView(canvas);
            drawFooter(canvas, "M:Mask  `:Back");
            break;
            
        default:
            break;
    }

    // Upload action menu overlays the list when open.
    if (uploadMenu_.isVisible()) {
        uploadMenu_.render(canvas);
    }
#else
    (void)canvas;
#endif
}

void CapturesScreen::drawHeader(Canvas& canvas, const char* title) {
    // Use StatusBar with accent color
    ui::StatusBar::render(canvas, title, nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void CapturesScreen::drawFooter(Canvas& canvas, const char* text) {
    int16_t screenWidth = canvas.width();
    int16_t screenHeight = canvas.height();
    
    canvas.fillRect(0, screenHeight - FOOTER_HEIGHT, screenWidth, FOOTER_HEIGHT, theme::BG_SECONDARY());
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, screenHeight - 12);
    canvas.print(text);
}

void CapturesScreen::drawCategorySelect(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 2;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 16;
    
    const char* names[] = {"Packets", "Handshakes", "Credentials", "RFID", "Wardriving"};
    
    canvas.setTextSize(1);
    
    for (int i = 0; i < CATEGORY_COUNT; i++) {
        bool selected = (i == categorySelection_);
        
        if (selected) {
            canvas.fillRect(0, y, screenWidth, lineHeight, theme::ACCENT());
        }
        
        // Selected text uses BG_PRIMARY for contrast with ACCENT background
        canvas.setTextColor(selected ? theme::BG_PRIMARY() : theme::TEXT_PRIMARY());
        canvas.setCursor(8, y + 4);
        canvas.print(names[i]);
        
        y += lineHeight;
    }
    
    // Free space at bottom
    y = canvas.height() - FOOTER_HEIGHT - 14;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(8, y);
#ifdef ESP32
    uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
    canvas.printf("Free: %.1f GB", freeBytes / (1024.0 * 1024.0 * 1024.0));
#else
    canvas.print("Free: N/A");
#endif
}

void CapturesScreen::drawFileList(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 2;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 20;
    
    canvas.setTextSize(1);
    
    if (pendingLoad_) {
        // Loading state - shown before deferred load completes
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(screenWidth / 2 - 36, y + 30);
        canvas.print("Loading...");
        return;
    }
    
    
    // Get list size based on category
    size_t listSize = (selectedCategory_ == CaptureCategory::HANDSHAKES) 
                      ? handshakeIndices_.size() : files_.size();
    
    if (listSize == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(screenWidth / 2 - 60, y + 30);
        canvas.print("No captures yet");
        return;
    }
    
    int scrollOff = scrollOffset_;
    int selectedIdx = selection_;
    int endIdx = scrollOff + VISIBLE_FILES;
    if (endIdx > (int)listSize) endIdx = listSize;
    
    // For HANDSHAKES, access registry directly via indices
    const auto& summaries = CaptureRegistry::getInstance().getHandshakeSummaries();
    
    for (int i = scrollOff; i < endIdx; i++) {
        bool selected = (i == selectedIdx);
        
        // Get file data based on category
        static char fileNameBuf[64];  // Static buffer for derived filename
        const char* fileName = nullptr;
        uint32_t fileSize = 0;
        WpaSecStatus wpaStatus = WpaSecStatus::NOT_UPLOADED;
        
        if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
            // Access via index into registry summaries
            size_t regIdx = handshakeIndices_[i];
            // Derive filename from SSID
            summaries[regIdx].getFilename(fileNameBuf, sizeof(fileNameBuf));
            fileName = fileNameBuf;
            fileSize = summaries[regIdx].size;
            
            // Get WPA-SEC status from summary (now stored in registry)
            wpaStatus = wpaSecStatusCache_[i];
        } else {
            // For CREDENTIALS/PACKETS, use files_ vector
            fileName = files_[i].name;
            fileSize = files_[i].size;
        }
        
        // Only show list selection highlight when footer doesn't have focus
        if (selected && !footerHints_.hasFocus()) {
            canvas.fillRect(0, y, screenWidth, lineHeight, theme::ACCENT());
        }
        
        int16_t textX = 4;
        
        // Draw status circles for handshakes (WPA-SEC) and wardriving (WiGLE)
        if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
            // Single combined dot across both cracking services: best outcome wins
            // (cracked > uploaded), else fall back to WPA-SEC's view. We do NOT mix
            // pwncrack's negative states in — its status defaults to NOT_UPLOADED
            // for every capture even when unkeyed, which would wrongly turn
            // incomplete/invalid rows red. FILE INFO shows the per-service detail.
            WpaSecStatus pcStatus = summaries[handshakeIndices_[i]].pwncrackStatus;
            WpaSecStatus combined = wpaStatus;
            if (wpaStatus == WpaSecStatus::CRACKED || pcStatus == WpaSecStatus::CRACKED) {
                combined = WpaSecStatus::CRACKED;
            } else if (wpaStatus == WpaSecStatus::UPLOADED || pcStatus == WpaSecStatus::UPLOADED) {
                combined = WpaSecStatus::UPLOADED;
            }

            uint16_t circleColor;
            switch (combined) {
                case WpaSecStatus::NOT_UPLOADED: circleColor = theme::ERROR();         break;  // Red
                case WpaSecStatus::UPLOADED:     circleColor = theme::WARNING();       break;  // Orange
                case WpaSecStatus::CRACKED:      circleColor = theme::SUCCESS();       break;  // Green
                case WpaSecStatus::INVALID:      circleColor = theme::TEXT_DISABLED(); break;  // Gray
                case WpaSecStatus::INCOMPLETE:   circleColor = 0xFFFF;                 break;  // White
                default:                         circleColor = theme::ERROR();
            }
            canvas.fillCircle(8, y + 6, 4, circleColor);  // single dot (original style)
            textX = 18;  // Offset for circle
        } else if (selectedCategory_ == CaptureCategory::WARDRIVING) {
            int16_t circleX = 8;
            int16_t circleY = y + 6;
            int16_t radius = 4;
            
            WigleStatus wigleStatus = WigleService::getInstance().getStatus(fileName);
            uint16_t circleColor = (wigleStatus == WigleStatus::UPLOADED) 
                                   ? theme::SUCCESS() : theme::ERROR();
            
            canvas.fillCircle(circleX, circleY, radius, circleColor);
            textX = 18;
        }
        
        // Filename - use selected style only when list has focus
        bool showAsSelected = selected && !footerHints_.hasFocus();
        canvas.setTextColor(showAsSelected ? theme::BG_PRIMARY() : theme::TEXT_PRIMARY());
        canvas.setCursor(textX, y + 2);
        
        char displayName[32];
        int maxLen = (selectedCategory_ == CaptureCategory::HANDSHAKES) ? 24 : 28;
        strncpy(displayName, fileName, maxLen);
        displayName[maxLen] = '\0';
        if ((int)strlen(fileName) > maxLen) {
            strcat(displayName, "...");
        }
        canvas.print(displayName);
        
        // Size on second line
        canvas.setTextColor(showAsSelected ? theme::BG_PRIMARY() : theme::TEXT_SECONDARY());
        canvas.setCursor(textX, y + 11);
        canvas.print(formatSize(fileSize));
        
        // Type badge moved to FILE INFO screen to avoid metadata loading in list
        
        y += lineHeight;
    }
    
    // Draw vertical scrollbar when items exceed visible count
    if (listSize > VISIBLE_FILES) {
        int16_t barX = screenWidth - 5;
        int16_t barY = HEADER_HEIGHT + 2;
        int16_t barHeight = VISIBLE_FILES * lineHeight - 2;
        
        // Track background
        canvas.fillRect(barX, barY, 4, barHeight, theme::BG_TERTIARY());
        
        // Thumb - size proportional to visible/total items
        int16_t thumbHeight = (VISIBLE_FILES * barHeight) / listSize;
        if (thumbHeight < 6) thumbHeight = 6;
        
        int maxScroll = listSize - VISIBLE_FILES;
        int16_t thumbY = barY + (scrollOff * (barHeight - thumbHeight)) / maxScroll;
        canvas.fillRect(barX, thumbY, 4, thumbHeight, theme::ACCENT());
    }
}

void CapturesScreen::drawDeleteConfirm(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 16;
    int16_t screenWidth = canvas.width();
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(screenWidth / 2 - 60, y);
    canvas.print("Delete this file?");
    
    y += 20;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(8, y);
    
    const char* filename = getSelectedFilename();
    uint32_t fileSize = getSelectedSize();
    if (filename) {
        canvas.print(filename);
        y += 12;
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(8, y);
        canvas.print(formatSize(fileSize));
    }
}

void CapturesScreen::drawFileInfo(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 6;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 14;
    
    size_t listSize = getListSize();
    int sel = selection_;
    if (sel < 0 || sel >= (int)listSize) return;
    
    // For HANDSHAKES, get data from registry; for others use files_
    const char* fileName = getSelectedFilename();
    uint32_t fileSize = getSelectedSize();
    if (!fileName) return;
    
    // Local variables for metadata (loaded fresh for HANDSHAKES)
    char handshakeType[8] = "...";
    bool hasGPS = false;
    double latitude = 0, longitude = 0;
    float altitude = 0; // Removed usage
    (void)altitude;
    uint8_t satellites = 0;
    int8_t signalStrength = 0;
    uint8_t channel = 0;
    WpaSecStatus wpaStatus = WpaSecStatus::NOT_UPLOADED;
    WpaSecStatus pcStatus = WpaSecStatus::NOT_UPLOADED;
    char pcPassword[65] = {0};

    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        // Load metadata on demand for FILE INFO from registry
        auto& registry = CaptureRegistry::getInstance();
        const HandshakeMetadata* metadata = registry.getMetadata(fileName);
        if (metadata) {
            strncpy(handshakeType, metadata->type, sizeof(handshakeType) - 1);
            hasGPS = metadata->hasGPS;
            if (metadata->hasGPS) {
                latitude = metadata->latitude;
                longitude = metadata->longitude;
                altitude = metadata->altitude;
                satellites = metadata->satellites;
            }
            signalStrength = metadata->signalStrength;
            channel = metadata->channel;
            pcStatus = metadata->pwncrackStatus;
            strncpy(pcPassword, metadata->pwncrackPassword, sizeof(pcPassword) - 1);
        } else {
            strcpy(handshakeType, "UNK");
        }
        
        // Get WPA-SEC status from cache or query fresh
        if (sel < (int)wpaSecStatusCache_.size()) {
            wpaStatus = wpaSecStatusCache_[sel];
        }
        if (wpaStatus == WpaSecStatus::NOT_UPLOADED) {
            // Lazy load status if not cached
            wpaStatus = WpaSecService::getInstance().getStatus(fileName);
        }
    } else if (sel < (int)files_.size()) {
        // For other categories, use files_ vector
        auto& file = files_[sel];
        strncpy(handshakeType, file.handshakeType, sizeof(handshakeType) - 1);
        hasGPS = file.hasGPS;
        latitude = file.latitude;
        longitude = file.longitude;
        altitude = file.altitude;
        satellites = file.satellites;
        signalStrength = file.signalStrength;
        wpaStatus = file.wpaSecStatus;
    }
    
    canvas.setTextSize(1);
    
    // Row 1: Name and Size (Header style)
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, y);
    canvas.print(fileName);
    
    // Right-aligned size
    String sizeStr = formatSize(fileSize);
    int16_t sizeW = sizeStr.length() * 6;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(screenWidth - sizeW - 4, y);
    canvas.print(sizeStr);
    y += lineHeight + 2;
    
    // Divider
    canvas.drawFastHLine(4, y - 4, screenWidth - 8, theme::BG_TERTIARY());
    
    // Metadata rows (Category-specific)
    if (selectedCategory_ == CaptureCategory::HANDSHAKES) {
        // Row 2: Type and RSSI
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("Type:");
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(40, y);
        canvas.print(handshakeType);

        // Channel (sourced from the manifest record), between Type and RSSI.
        if (channel > 0) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(88, y);
            canvas.print("Ch");
            canvas.setTextColor(theme::ACCENT());
            canvas.printf(" %d", channel);
        }

        if (signalStrength != 0) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(screenWidth / 2, y);
            canvas.print("RSSI:");
            
            uint16_t rssiColor = (signalStrength > -50) ? theme::SUCCESS() : 
                                 (signalStrength > -70) ? theme::WARNING() : theme::ERROR();
            canvas.setTextColor(rssiColor);
            canvas.printf(" %d dBm", signalStrength);
        }
        y += lineHeight;

        // Shared status string/color for both cracking services.
        auto statusText = [](WpaSecStatus st, const char*& str, uint16_t& col) {
            switch (st) {
                case WpaSecStatus::NOT_UPLOADED: str = "NOT SENT";   col = theme::ERROR(); break;
                case WpaSecStatus::UPLOADED:     str = "UPLOADED";   col = theme::WARNING(); break;
                case WpaSecStatus::CRACKED:      str = "CRACKED!";   col = theme::SUCCESS(); break;
                case WpaSecStatus::INVALID:      str = "INVALID";    col = theme::TEXT_DISABLED(); break;
                case WpaSecStatus::INCOMPLETE:   str = "INCOMPLETE"; col = 0xFFFF; break;
                default:                         str = "Unknown";    col = theme::TEXT_DISABLED();
            }
        };
        // Renders a masked/shown password with the [M]/[S] indicator on one row.
        auto drawKeyRow = [&](const char* pwd) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(4, y);
            canvas.print("Key:");
            canvas.setTextColor(passwordsMasked_ ? theme::WARNING() : theme::SUCCESS());
            canvas.setCursor(screenWidth - 30, y);
            canvas.print(passwordsMasked_ ? "[M]" : "[S]");
            canvas.setCursor(40, y);
            if (passwordsMasked_) {
                canvas.setTextColor(theme::TEXT_SECONDARY());
                int plen = strlen(pwd);
                for (int j = 0; j < (plen > 12 ? 12 : plen); j++) canvas.print("*");
                if (plen > 12) canvas.print("..");
            } else {
                canvas.setTextColor(theme::ACCENT());
                canvas.print(pwd);
            }
            y += lineHeight;
        };

        const char* statusStr = "Unknown";
        uint16_t statusColor = theme::TEXT_DISABLED();

        // Row 3: WPA-SEC status (+ key if cracked)
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("WPA-SEC:");
        statusText(wpaStatus, statusStr, statusColor);
        canvas.setTextColor(statusColor);
        canvas.setCursor(64, y);
        canvas.print(statusStr);
        y += lineHeight;
        if (wpaStatus == WpaSecStatus::CRACKED) {
            const char* pwd = WpaSecService::getInstance().getCrackedPassword(fileName);
            if (pwd) drawKeyRow(pwd);
        }

        // Row 4: pwncrack status (+ key if cracked)
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("pwncrack:");
        statusText(pcStatus, statusStr, statusColor);
        canvas.setTextColor(statusColor);
        canvas.setCursor(64, y);
        canvas.print(statusStr);
        y += lineHeight;
        if (pcStatus == WpaSecStatus::CRACKED && pcPassword[0]) {
            drawKeyRow(pcPassword);
        }

        // Row 5: GPS Info
        if (hasGPS) {
            y += 2;
            canvas.setTextColor(theme::SUCCESS());
            canvas.setCursor(4, y);
            canvas.print("GPS:");
            
            canvas.setTextColor(theme::ACCENT());
            canvas.setCursor(34, y);
            canvas.printf("%.5f,%.5f", latitude, longitude);
            
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.printf(" (%ds)", satellites);
            y += lineHeight;
        }
    } else {
        // Generic metadata for non-handshake files
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("Type:");
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(40, y);
        canvas.print(handshakeType);
        y += lineHeight;
        
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("Full Path:");
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(8, y + lineHeight);
        canvas.print(getCategoryPath(selectedCategory_));
        y += lineHeight * 2;
    }
}

void CapturesScreen::drawCredentialView(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 4;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 12;
    
    canvas.setTextSize(1);
    
    // SSID header
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(4, y);
    canvas.print("SSID: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(credentialSSID_[0] ? credentialSSID_ : "(unknown)");
    y += lineHeight + 4;
    
    if (credentials_.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(screenWidth / 2 - 50, y + 20);
        canvas.print("No credentials");
        return;
    }
    
    // Password mask indicator
    canvas.setTextColor(passwordsMasked_ ? theme::WARNING() : theme::SUCCESS());
    canvas.setCursor(screenWidth - 60, HEADER_HEIGHT + 4);
    canvas.print(passwordsMasked_ ? "[MASKED]" : "[SHOWN]");
    
    // Credential list
    for (size_t i = 0; i < credentials_.size() && y < canvas.height() - FOOTER_HEIGHT - lineHeight; i++) {
        const auto& cred = credentials_[i];
        
        // Index
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(4, y);
        canvas.printf("%d.", (int)(i + 1));
        
        // Username
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(20, y);
        
        char truncUser[20];
        strncpy(truncUser, cred.username, 18);
        truncUser[18] = '\0';
        canvas.print(truncUser);
        
        // Separator
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.print(" / ");
        
        // Password (masked or shown)
        if (passwordsMasked_) {
            canvas.setTextColor(theme::TEXT_SECONDARY());
            int passLen = strlen(cred.password);
            for (int j = 0; j < (passLen > 8 ? 8 : passLen); j++) {
                canvas.print("*");
            }
        } else {
            canvas.setTextColor(theme::SUCCESS());
            char truncPass[12];
            strncpy(truncPass, cred.password, 10);
            truncPass[10] = '\0';
            canvas.print(truncPass);
        }
        
        y += lineHeight;
    }
}

} // namespace adversary
