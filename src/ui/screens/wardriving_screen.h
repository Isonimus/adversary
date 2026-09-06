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
    const char* getSecurityString(uint8_t security);
    
    // Member Variables
    WardrivingState state_;
    WardrivingState returnState_;
    WardrivingState previousState_;
    bool visible_;
    bool exitToMenu_;
    uint32_t lastUpdateMs_;
    
    // Network log tracking (circular buffer)
    static constexpr size_t MAX_LOG_NETWORKS = 25;
    struct LoggedNetwork {
        char ssid[33];
        int8_t rssi;
        uint8_t channel;
        uint8_t security;
        uint32_t timestamp;
        bool valid;
    };
    LoggedNetwork networkLog_[MAX_LOG_NETWORKS];
    size_t logWriteIndex_;
    size_t logCount_;
    
    // Scroll state for network log
    int logScroll_;
    int logSelection_;
    
    // UI Components
    ui::FooterHints footerHints_;
    ui::ActionMenu actionMenu_;
};

} // namespace adversary
