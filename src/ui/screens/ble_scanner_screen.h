/**
 * @file ble_scanner_screen.h
 * @brief UI screen for BLE device discovery
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "ui/components/footer_hints.h"
#include "ui/components/action_menu.h"
#include "modules/ble/ble_scanner.h"
#include <vector>

namespace adversary {

class BleScannerScreen : public IScreen {
public:
    BleScannerScreen();
    virtual ~BleScannerScreen();

    // IScreen interface
    void show() override;
    void hide() override;
    bool isVisible() const override { return m_active; }
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }
    const char* getName() const override { return "BLE Scanner"; }
    ScreenId getId() const override { return ScreenId::BLE_SCANNER; }

private:
    void drawList(Canvas& canvas);
    void drawHeader(Canvas& canvas);
    void drawFooter(Canvas& canvas);
    
    void setupActionMenu(const BLEDeviceInfo& device);
    void handleAction(char action);

    BLEScanner& m_scanner;
    ui::FooterHints footerHints_;
    ui::ActionMenu actionMenu_;
    std::vector<BLEDeviceInfo> m_deviceList;
    BLEDeviceInfo m_contextDevice;
    int m_selectedIndex;
    int m_scrollOffset;
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    bool m_isInitializing;
    int m_pendingInit;
    uint32_t m_lastUpdate;
    
    static const int ITEMS_PER_PAGE = 5;
};

} // namespace adversary
