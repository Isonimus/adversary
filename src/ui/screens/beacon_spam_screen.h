/**
 * @file beacon_spam_screen.h
 * @brief Beacon Spam Attack UI screen
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../../modules/attack/beacon_spam.h"
#include "../../core/event_bus.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Beacon spam screen states
 */
enum class BeaconSpamScreenState {
    CONFIG,         // Configuring attack parameters
    RUNNING,        // Attack in progress
    PAUSED,         // Attack paused
    COMPLETED       // Attack completed
};

/**
 * @brief Beacon Spam attack screen for UI
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class BeaconSpamScreen : public IScreen {
public:
    BeaconSpamScreen();
    ~BeaconSpamScreen() override;
    
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
    
    const char* getName() const override { return "Beacon Spam"; }
    ScreenId getId() const override { return ScreenId::BEACON_SPAM; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    
    // State
    BeaconSpamScreenState getScreenState() const { return screenState_; }
    bool isAttackRunning() const;

private:
    void drawHeader(Canvas& canvas, const char* title);
    void drawConfig(Canvas& canvas);
    void drawRunning(Canvas& canvas);
    void drawStats(Canvas& canvas);
    
    void handleUp();
    void handleDown();
    void handleSelect();
    void handleBack();
    void startAttack();
    void stopAttack();
    void togglePause();
    
    const char* getModeName(BeaconSpamMode mode) const;
    
    bool visible_;
    bool initialized_;
    bool needsRedraw_;
    bool shouldExit_;
    uint32_t lastUpdate_;
    
    BeaconSpamScreenState screenState_;
    
    // Config options
    int configSelection_;
    int scrollOffset_;
    static constexpr int CONFIG_OPTION_COUNT = 7;
    static constexpr int VISIBLE_CONFIG_ITEMS = 4;
    
    // Config values
    BeaconSpamMode mode_;
    uint8_t channel_;
    uint32_t intervalMs_;
    bool randomizeBssid_;
    bool encryptedNetwork_;
    bool channelHopEnabled_;
    char customSsid_[33];
    
    // UI Components
    ui::FooterHints footerHints_;
    
    // EventBus subscription
    uint32_t stateHandlerId_ = 0;
    
    // Display dimensions
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 50;
};

} // namespace adversary
