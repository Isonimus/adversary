/**
 * @file server_status_screen.h
 * @brief Dashboard Server real-time status and monitoring screen.
 */

#pragma once

#include "screen_interface.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../theme.h"
#include <cstdint>

namespace adversary {

/**
 * @brief Dashboard Server Status Screen
 * 
 * Displays: 
 * - Running Status
 * - IP Address
 * - mDNS Hostname
 * - Connected Clients count
 * - Request Count (stats)
 * - Uptime
 */
class ServerStatusScreen : public IScreen {
public:
    ServerStatusScreen();
    ~ServerStatusScreen() override;

    // IScreen Interface
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    
    const char* getName() const override { return "Server Status"; }
    ScreenId getId() const override { return ScreenId::SERVER_STATUS; }

private:
    void drawStats(Canvas& canvas);
    void drawFooter(Canvas& canvas);
    
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    
    uint32_t lastUpdateMillis_;
    
    // UI Components
    ui::FooterHints footerHints_;
    
    static constexpr int16_t ROW_HEIGHT = 16;
    static constexpr int16_t START_Y = 24;
};

} // namespace adversary
