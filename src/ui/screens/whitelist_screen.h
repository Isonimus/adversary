/**
 * @file whitelist_screen.h
 * @brief Whitelist management screen for viewing/deleting whitelisted SSIDs
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "modules/storage/settings_manager.h"
#include "utils/mac_utils.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Screen states
 */
enum class WhitelistScreenState : uint8_t {
    LIST,               // Show whitelist entries
    DELETE_CONFIRM      // Confirm deletion dialog
};

/**
 * @brief Whitelist management screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class WhitelistScreen : public IScreen {
public:
    WhitelistScreen();
    ~WhitelistScreen() override;
    
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
    
    const char* getName() const override { return "Whitelist"; }
    ScreenId getId() const override { return ScreenId::WHITELIST; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;

private:
    void drawHeader(Canvas& canvas, const char* title);
    void drawFooter(Canvas& canvas);
    void drawList(Canvas& canvas);
    void drawDeleteConfirm(Canvas& canvas);
    void loadWhitelist();
    void deleteSelectedEntry();
    
    // State
    bool visible_;
    bool needsRedraw_;
    bool shouldExit_;
    WhitelistScreenState state_;
    
    // List state
    std::vector<WhitelistEntry> entries_;
    int16_t selectedIndex_;
    int16_t scrollOffset_;
    
    // Delete confirm state
    bool deleteConfirmYes_;
    
    // UI Components
    ui::FooterHints footerHints_;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t ROW_HEIGHT = 16;
};

} // namespace adversary
