/**
 * @file ble_spoof_screen.h
 * @brief UI for BLE Identity Spoofing (Cloning)
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "modules/ble/ble_spanner.h"
#include "modules/ble/ble_scanner.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"

namespace adversary {

class BleSpoofScreen : public IScreen {
public:
    BleSpoofScreen();
    virtual ~BleSpoofScreen() = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    
    void setTarget(const BLEDeviceInfo& device);

    /// The target arrives via the navigation params when launched from the BLE scanner:
    /// bssid = the device address bytes (NimBLEAddress::getNative order), ssid = the name.
    /// Only name + address are carried because the CLONE path uses nothing else.
    void setParams(const ScreenParams& params) override;

    const char* getName() const override { return "BLE Spoof"; }
    ScreenId getId() const override { return ScreenId::BLE_SPOOF; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    void drawStatus(Canvas& canvas);

    BLEDeviceInfo m_target;
    bool m_active;
    bool m_spoofing;
    bool m_shouldExit;
    bool m_needsRedraw;
    
    ui::FooterHints footerHints_;
    uint32_t m_lastUpdate;
};

} // namespace adversary
