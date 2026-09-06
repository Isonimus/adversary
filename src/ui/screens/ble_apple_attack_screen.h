/**
 * @file ble_apple_attack_screen.h
 * @brief UI for executing Apple-specific BLE attacks (Applejuice/SourApple)
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "modules/ble/ble_spanner.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include <vector>

namespace adversary {

class BleAppleAttackScreen : public IScreen {
public:
    BleAppleAttackScreen();
    virtual ~BleAppleAttackScreen() = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    
    const char* getName() const override { return "Apple Attack"; }
    ScreenId getId() const override { return ScreenId::BLE_APPLE_ATTACK; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    enum class State {
        CONFIG,
        ATTACK
    };

    struct PromptOption {
        BleSpamPrompt prompt;
        const char* label;
    };

    void drawConfig(Canvas& canvas);
    void drawAttack(Canvas& canvas);
    void startAttack();
    void stopAttack();

    State m_state;
    BleSpamPrompt m_selectedPrompt;
    int m_configSelection;
    
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    
    ui::FooterHints footerHints_;
    
    uint32_t m_pulseStartTime;
    std::vector<PromptOption> m_promptOptions;
};

} // namespace adversary
