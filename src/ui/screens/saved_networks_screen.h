/**
 * @file saved_networks_screen.h
 * @brief UI screen for managing saved WiFi credentials
 */

#pragma once

#include "screen_interface.h"
#include "modules/storage/settings_manager.h"
#include "modules/wifi/wifi_connection.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/action_menu.h"
#include "ui/components/footer_hints.h"
#include <vector>

#ifdef ESP32
#include <M5GFX.h>
#endif

namespace adversary {

/**
 * @brief Screen to list and manage saved WiFi credentials
 */
class SavedNetworksScreen : public IScreen {
public:
    SavedNetworksScreen();
    ~SavedNetworksScreen() override = default;

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
    const char* getName() const override { return "Saved Networks"; }
    ScreenId getId() const override { return ScreenId::SAVED_NETWORKS; }

private:
    void navigateUp();
    void navigateDown();
    void selectCurrent();
    void handleAction(char action);
    void setupActionMenu();

    bool visible_ = false;
    bool shouldExit_ = false;
    bool needsRedraw_ = true;
    
    int selectedIndex_ = 0;
    int scrollOffset_ = 0;
    
    ui::ActionMenu actionMenu_;
    ui::FooterHints footerHints_;
    WiFiCredential selectedCred_;
    
    static constexpr int ENTRY_HEIGHT = 28;
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int FOOTER_HEIGHT = 16;
};

} // namespace adversary
