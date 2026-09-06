/**
 * @file captures_screen.h
 * @brief Captures browser screen for viewing/managing captured files
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/toast_manager.h"
#include "../components/footer_hints.h"
#include "../components/action_menu.h"
#include "modules/network/wpasec_service.h"
#include "modules/network/wigle_service.h"
#include "utils/handshake_utils.h"
#include "modules/storage/handshake_metadata.h"
#include "modules/storage/capture_registry.h"
#include "screen_interface.h"

#ifdef ESP32
#include <Arduino.h>
#include <M5GFX.h>
#include <SD.h>
#endif

namespace adversary {

/**
 * @brief Capture category types
 */
enum class CaptureCategory : uint8_t {
    PACKETS,
    HANDSHAKES,
    CREDENTIALS,
    RFID,
    WARDRIVING
};

/**
 * @brief Screen states
 */
enum class CapturesScreenState : uint8_t {
    CATEGORY_SELECT,    // Show Packets/Handshakes/Credentials
    FILE_LIST,          // Show files in selected category
    FILE_INFO,          // Show detailed file info
    DELETE_CONFIRM,     // Confirm deletion dialog
    CREDENTIAL_VIEW     // Preview credential JSON content
};

/**
 * @brief File entry for listing
 */
struct CaptureFileEntry {
    char name[64];      // Filename without path
    uint32_t size;      // Size in bytes
    time_t lastWrite;   // Last modification time
    WpaSecStatus wpaSecStatus = WpaSecStatus::NOT_UPLOADED;  // WPA-SEC upload status
    char handshakeType[8] = "4WAY";  // Type: "4WAY", "PMKID", "EAPOL" (parsed from PCAP)
    bool metadataLoaded = false;     // Flag for lazy metadata loading (FILE INFO)
    bool statusLoaded = false;       // Flag for lazy WPA-SEC status loading (list view)
    
    // GPS and signal info (from metadata)
    bool hasGPS = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude = 0.0f;
    uint8_t satellites = 0;
    int8_t signalStrength = 0;  // RSSI in dBm
    
    CaptureFileEntry() : size(0), lastWrite(0), wpaSecStatus(WpaSecStatus::NOT_UPLOADED), 
                         metadataLoaded(false), statusLoaded(false) {
        memset(name, 0, sizeof(name));
        strcpy(handshakeType, "...");  // Placeholder until loaded
    }
};

/**
 * @brief Parsed credential entry
 */
struct ParsedCredential {
    char username[64];
    char password[64];
    
    ParsedCredential() {
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));
    }
};

/**
 * @brief Captures browser screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class CapturesScreen : public IScreen {
public:
    CapturesScreen();
    ~CapturesScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    
    const char* getName() const override { return "Captures"; }
    ScreenId getId() const override { return ScreenId::CAPTURES; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;

private:
    // Rendering methods (implemented in cpp)
    void drawHeader(Canvas& canvas, const char* title);
    void drawFooter(Canvas& canvas, const char* text);
    void drawCategorySelect(Canvas& canvas);
    void drawFileList(Canvas& canvas);
    void drawDeleteConfirm(Canvas& canvas);
    void drawFileInfo(Canvas& canvas);
    void drawCredentialView(Canvas& canvas);
    
    // Business logic methods
    void loadFileList();
    void deleteSelectedFile();
    size_t getFileCount(const char* path);
    const char* getCategoryPath(CaptureCategory cat) const;
    const char* getCategoryName(CaptureCategory cat) const;
    static String formatSize(uint32_t bytes);
    void loadCredentials();
    
    // Helper to get selected file info (abstracts indices vs files_)
    const char* getSelectedFilename() const;
    uint32_t getSelectedSize() const;
    size_t getListSize() const;
    
    // State
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    uint32_t lastUpdate_;
    
    CapturesScreenState screenState_;
    CaptureCategory selectedCategory_;
    
    // Category selection
    int categorySelection_;
    static constexpr int CATEGORY_COUNT = 5;
    size_t categoryCounts_[CATEGORY_COUNT];
    
    // File list - for CREDENTIALS/PACKETS only (scan SD)
    std::vector<CaptureFileEntry> files_;
    
    // HANDSHAKES: use indices into registry (saves ~9KB vs copying)
    // These are sorted indices pointing to CaptureRegistry::getHandshakeSummaries()
    std::vector<size_t> handshakeIndices_;
    
    // WPA-SEC status cache for visible handshakes (lazy loaded)
    std::vector<WpaSecStatus> wpaSecStatusCache_;
    
    // Display constants
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t FOOTER_HEIGHT = 16;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 100;
    static constexpr int VISIBLE_FILES = 5;
    static constexpr int16_t ROW_HEIGHT = 20;
    
    // Credential view
    std::vector<ParsedCredential> credentials_;
    char credentialSSID_[33];
    bool passwordsMasked_;
    
    // Deferred loading counter (0=no load, >0=countdown then load)
    int pendingLoad_;
    
    // Deferred WPA-SEC operations (critical for upload/refresh flows)
    int pendingRefresh_;  // 0=no refresh, >0=countdown then fetch
    int pendingUpload_;   // 0=no upload, >0=countdown then upload
    char pendingUploadFilename_[64];  // Filename for deferred upload
    
    // Deferred WiGLE upload
    int pendingWigleUpload_;   // 0=no upload, >0=countdown then upload
    char pendingWigleFilename_[64];  // Filename for deferred WiGLE upload
    
    // Interactive UI components
    ui::FooterHints footerHints_;
    ui::ActionMenu uploadMenu_;  // "Sync this / Sync all new" popup (handshakes)

    // Unified sync to every keyed cracking service. The atom of work is ONE TLS
    // upload of ONE file to ONE service (WPA-SEC <- {ssid}.pcap, pwncrack <-
    // {ssid}.22000). Each runs the proven single-upload cycle (purge → connect →
    // upload one → disconnect → RESTORE) chained through pendingUpload_:
    // restoring the 64KB canvas after only ONE TLS cycle is reliable, whereas one
    // purge + N TLS connections fragments the heap so the canvas can't be
    // restored (dead UI). The canvas is back between jobs, so badges update live
    // and progress shows per job. A capture going to both services is two jobs.
    enum class SyncService : uint8_t { WPASEC, PWNCRACK };
    struct SyncJob { SyncService svc; char ssid[33]; };

    bool uploadAllActive_ = false;
    bool syncCancelRequested_ = false;  // ESC during a run: stop after current file
    std::vector<SyncJob> syncQueue_;  // pending (service, capture) upload jobs
    int uploadAllPos_ = 0;
    char uploadAllLabel_[28] = {0};   // persistent storage for the menu item label
                                      // (ActionMenu stores the char* without copying)

    // Append a job per keyed service the capture isn't done on (WPA-SEC <- .pcap,
    // pwncrack <- .22000, only when the .22000 exists).
    static void appendJobsForIndex(std::vector<SyncJob>& out, const HandshakeSummary& s,
                                   bool wpaKeyed, bool pcKeyed);
    int countPendingSyncJobs(bool selectedOnly) const;  // # upload operations (per-service)
    int countPendingSyncCaptures(bool selectedOnly) const;  // # distinct handshakes (for the label)
    void enqueueSyncJobs(bool selectedOnly);             // fill syncQueue_
    void openUploadMenu();                               // populate + show uploadMenu_
    void startSync(bool selectedOnly);                   // enqueue + kick the first job
    void kickNextSync();                                 // chain the next job
    
    // List state
    int selection_;
    int scrollOffset_;
};

} // namespace adversary
