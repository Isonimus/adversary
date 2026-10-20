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
#include "../components/list_scroll_model.h"
#include "../components/scrollable_list.h"
#include "../components/confirm_dialog.h"
#include "modules/storage/settings_manager.h"
#include "utils/mac_utils.h"
#include "screen_interface.h"

namespace adversary {

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
    void showDeleteConfirm();
    void loadWhitelist();
    void deleteSelectedEntry();

    // State
    bool visible_;
    bool needsRedraw_;
    bool shouldExit_;

    // List state. The shared model owns selection + scroll window + wrap; drawList feeds it the
    // live viewport each frame so the two can't drift (slice-0062).
    std::vector<WhitelistEntry> entries_;
    ListScrollModel entryList_;

    // Shared confirm overlay for entry deletion (ADR-0085); replaces the bespoke Yes/No buttons.
    ui::ConfirmDialog confirmDialog_;

    // UI Components
    ui::FooterHints footerHints_;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t ROW_HEIGHT = theme::LIST_ROW_HEIGHT;
};

} // namespace adversary
