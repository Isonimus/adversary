/**
 * @file ble_spam_screen.h
 * @brief UI for configuring and executing BLE Spam attacks
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "modules/ble/ble_spanner.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include <vector>

namespace adversary {

class BleSpamScreen : public IScreen {
public:
    BleSpamScreen();
    virtual ~BleSpamScreen() = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    
    const char* getName() const override { return "BLE Spam"; }
    ScreenId getId() const override { return ScreenId::BLE_SPAM; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    enum class State {
        CONFIG,
        ATTACK
    };

    void drawConfig(Canvas& canvas);
    void drawAttack(Canvas& canvas);
    void startAttack();
    void stopAttack();

    State m_state;
    BleSpamConfig m_config;
    int m_configSelection;    // 0=Provider, 1=Prompt, 2=Intensity, 3=START
    int m_configScrollOffset; // For list scrolling
    
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    
    ui::StatusBar m_statusBar;
    ui::FooterHints footerHints_;
    
    uint32_t m_pulseStartTime;
    
    static constexpr int CONFIG_OPTION_COUNT = 4;
    static constexpr int LINE_HEIGHT = 15;
    static constexpr int MARGIN_LEFT = 4;
    static constexpr int FIRST_LINE_Y = 26;
};

} // namespace adversary
