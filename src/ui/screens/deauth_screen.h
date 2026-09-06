/**
 * @file deauth_screen.h
 * @brief Deauth Attack UI screen
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../../modules/attack/deauth.h"
#include "../../core/event_bus.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Deauth attack screen states
 */
enum class DeauthScreenState {
    TARGET_SELECT,  // Selecting target from scan results
    CONFIG,         // Configuring attack parameters
    RUNNING,        // Attack in progress
    RESULTS         // Attack completed, showing results
};

/**
 * @brief Deauth attack screen for UI
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class DeauthScreen : public IScreen {
public:
    DeauthScreen();
    ~DeauthScreen() override;
    
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
    
    const char* getName() const override { return "Deauth Attack"; }
    ScreenId getId() const override { return ScreenId::DEAUTH; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    
    // Target selection (called from scanner with selected network)
    void setTarget(const uint8_t* bssid, const char* ssid, uint8_t channel);
    bool hasTarget() const { return hasTarget_; }
    
    // Factory lazy-loading: receive target data via ScreenManager::navigateWithParams()
    void setParams(const ScreenParams& params) override {
        setTarget(params.bssid, params.ssid, params.channel);
    }
    
    // State
    DeauthScreenState getScreenState() const { return screenState_; }
    bool isAttackRunning() const;

private:
    void drawHeader(Canvas& canvas, const char* title);
    void drawFooter(Canvas& canvas);
    void drawConfig(Canvas& canvas);
    void drawRunning(Canvas& canvas);
    void drawResults(Canvas& canvas);
    void drawProgressBar(Canvas& canvas, int x, int y, int width, int height, float progress);
    
    void handleUp();
    void handleDown();
    void handleSelect();
    void handleBack();
    void startAttack();
    void stopAttack();
    
    bool visible_;
    bool initialized_;
    bool needsRedraw_;
    bool shouldExit_;
    uint32_t lastUpdate_;
    
    DeauthScreenState screenState_;
    
    // Target info
    bool hasTarget_;
    uint8_t targetBssid_[6];
    char targetSsid_[33];
    uint8_t targetChannel_;
    
    // Config options
    int configSelection_;
    int scrollOffset_;
    static constexpr int CONFIG_OPTION_COUNT = 5;
    static constexpr int VISIBLE_CONFIG_ITEMS = 3;  // Max items visible without scrolling
    
    // Config values
    DeauthTargetType targetType_;
    DeauthReason reasonCode_;
    uint32_t packetCount_;
    uint32_t delayMs_;
    bool sendDisassoc_;
    
    // UI Components
    ui::FooterHints footerHints_;
    
    // Double buffer canvas
    M5Canvas* canvas_;
    bool canvasInitialized_;
    
    // EventBus subscription
    uint32_t stateHandlerId_ = 0;
    
    // Display dimensions
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 100;
};

} // namespace adversary
