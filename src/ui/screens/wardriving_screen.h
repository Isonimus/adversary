/**
 * @file wardriving_screen.h
 * @brief Wardriving mode UI screen
 * 
 * Displays wardriving session statistics, GPS status, and real-time network discovery.
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/wardriving/wardriving_manager.h"
#include "modules/gps/gps_manager.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "ui/components/action_menu.h"
#include "ui/components/list_scroll_model.h"
#include "ui/components/scrollable_list.h"
#include "network_log_ring.h"
#include "screen_interface.h"
#include <Arduino.h>

namespace adversary {

/**
 * @brief Wardriving screen states
 */
enum class WardrivingState {
    IDLE,           ///< Not started, show start prompt
    SCANNING,       ///< Active session, collecting data
    PAUSED,         ///< Session paused
    SAVING,         ///< Exporting data to SD card
    NETWORK_LOG,    ///< Scrollable network log view
    CONFIRM_EXIT    ///< Prompt to save/discard before exiting
};

/**
 * @brief Wardriving screen UI component
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class WardrivingScreen : public IScreen {
public:
    WardrivingScreen();
    ~WardrivingScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    
    void update() override;
    void render(Canvas& canvas) override;
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return exitToMenu_; }
    void resetExitFlag() override { exitToMenu_ = false; }
    
    const char* getName() const override { return "Wardriving"; }
    ScreenId getId() const override { return ScreenId::WARDRIVING; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;

private:
    // Drawing Functions
    void drawIdle(Canvas& canvas);
    void drawScanning(Canvas& canvas);
    void drawPaused(Canvas& canvas);
    void drawSaving(Canvas& canvas);
    void drawNetworkLog(Canvas& canvas);
    void drawGPSStatus(Canvas& canvas, int16_t y);
    void drawTime(Canvas& canvas, int16_t y);
    void drawSessionStats(Canvas& canvas, int16_t y);
    
    // Helper Functions
    String formatDuration(uint32_t seconds);
    String formatDistance(float km);
    uint16_t getRSSIColor(int8_t rssi);

    // Per-run radio-mode quick-config on the idle screen (ADR-0111). These bind to the
    // canonical WirelessSettings::wardrivingRadioMode field rather than a screen-local
    // copy: currentRadioMode reads (clamping a corrupt value), cycleRadioMode advances
    // and persists, radioModeLabel renders the operator-facing name.
    wardriving::RadioMode currentRadioMode() const;
    void cycleRadioMode();
    static const char* radioModeLabel(wardriving::RadioMode mode);

    // Heap readout for the status-bar centre. Returns nullptr when the global heap
    // badge is on, so the two never show heap at once (the badge already does, with
    // health colouring); writes "<n>K" into `buf` and returns it when the badge is off.
    const char* statusHeapText(char* buf, size_t len) const;
    
    // Member Variables
    WardrivingState state_;
    WardrivingState returnState_;
    WardrivingState previousState_;
    bool visible_;
    bool exitToMenu_;
    uint32_t lastUpdateMs_;
    
    // Live network-log view. The ring reserves a per-type minimum so BLE's high-volume stream
    // cannot evict every WiFi row (NetworkLogRing doc); dedup by (type, bssid) shows each
    // distinct radio once (slice-0075).
    NetworkLogRing networkLog_;

    // Selection + scroll window for the network log. The shared model keeps the selection on
    // screen from the viewport the renderer derives each frame, so the off-screen/premature
    // scroll bugs can't recur here, and it wraps at the ends (slice-0062).
    ListScrollModel logList_;
    
    // UI Components
    ui::FooterHints footerHints_;
    ui::ActionMenu actionMenu_;
};

} // namespace adversary
