/**
 * @file server_menu_screen.h
 * @brief Server Mode submenu screen
 * 
 * Provides options to start/stop the dashboard server, view status,
 * and configure server settings.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/action_menu.h"
#include "../components/toast_manager.h"
#include "screen_interface.h"
#include "modules/storage/settings_manager.h"

namespace adversary {

/**
 * @brief Server Mode submenu screen
 * 
 * Shows options for the dashboard web server:
 * - Start Server (launches AP + WebServer)
 * - Server Status (when running)
 * - Configuration shortcuts
 */
class ServerMenuScreen : public IScreen {
public:
    ServerMenuScreen();
    ~ServerMenuScreen() override;
    
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
    
    const char* getName() const override { return "Server Menu"; }
    ScreenId getId() const override { return ScreenId::SERVER_MENU; }

    void init() override;
    void deinit();
    
private:
    void drawHeader(Canvas& canvas);
    void drawMenu(Canvas& canvas);
    void drawFooter(Canvas& canvas);
    
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    
    // Menu state
    int selection_;
    static constexpr int MENU_ITEMS = 3;
    const char* menuLabels_[MENU_ITEMS] = {
        "Start Server",
        "Server Status", 
        "Configure Auth"
    };
    
    // UI Components
    ui::FooterHints footerHints_;
    ui::ActionMenu actionMenu_;
    
    // Server state (will be managed by ServerManager later)
    bool serverRunning_;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t ITEM_HEIGHT = 20;
};

} // namespace adversary
