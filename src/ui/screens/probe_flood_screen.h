/**
 * @file probe_flood_screen.h
 * @brief Probe Flood Attack UI screen
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../../modules/attack/probe_flood.h"
#include "../../core/event_bus.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/list_scroll_model.h"
#include "../components/scrollable_list.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Probe flood screen states
 */
enum class ProbeFloodScreenState {
    CONFIG,         // Configuring attack parameters
    RUNNING,        // Attack in progress
    PAUSED,         // Attack paused
    COMPLETED       // Attack completed
};

/**
 * @brief Probe Flood attack screen for UI
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class ProbeFloodScreen : public IScreen {
public:
    ProbeFloodScreen();
    ~ProbeFloodScreen() override;
    
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
    
    const char* getName() const override { return "Probe Flood"; }
    ScreenId getId() const override { return ScreenId::PROBE_FLOOD; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    
    // State
    ProbeFloodScreenState getScreenState() const { return screenState_; }
    bool isAttackRunning() const;
    
    // Pre-configure for targeted mode from scanner
    void setTarget(const char* ssid, uint8_t channel);
    
    // Factory lazy-loading: receive target via ScreenManager::navigateWithParams()
    void setParams(const ScreenParams& params) override {
        setTarget(params.ssid, params.channel);
    }

private:
    void drawHeader(Canvas& canvas, const char* title);
    void drawConfig(Canvas& canvas);
    void drawRunning(Canvas& canvas);
    void drawStats(Canvas& canvas);
    
    void handleSelect();
    // Single source of per-field stepping (ADR-0092): delta +1 next, -1 previous. ENTER on a
    // value row, '/' and '<'/'>' all route through here so ENTER and '/' stay identical.
    void handleEdit(int delta);
    void handleBack();
    void startAttack();
    void stopAttack();
    void togglePause();
    
    const char* getModeName(ProbeFloodMode mode) const;
    
    bool visible_;
    bool initialized_;
    bool needsRedraw_;
    bool shouldExit_;
    uint32_t lastUpdate_;
    
    ProbeFloodScreenState screenState_;
    
    // Config options. The shared model owns selection + scroll window + wrap; the renderer
    // feeds it the live viewport each frame so the two can't drift (slice-0062).
    static constexpr int CONFIG_OPTION_COUNT = 6;
    ListScrollModel configList_;
    
    // Config values
    ProbeFloodMode mode_;
    uint8_t channel_;
    uint32_t intervalMs_;
    bool randomizeMac_;
    bool channelHopEnabled_;
    char targetSsid_[33];
    
    // UI Components
    ui::FooterHints footerHints_;
    
    // EventBus subscription
    uint32_t stateHandlerId_ = 0;
    
    // Display dimensions
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 50;
};

} // namespace adversary
