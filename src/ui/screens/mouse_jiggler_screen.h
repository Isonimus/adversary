#pragma once
#include "ui/screens/screen_interface.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"

#if defined(TARGET_CARDPUTER)
#include "modules/badusb/usb_hid_devices.h"
#endif

namespace adversary {

class MouseJigglerScreen : public IScreen {
public:
    MouseJigglerScreen();
    virtual ~MouseJigglerScreen() = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;

    const char* getName() const override { return "Mouse Jiggler"; }
    ScreenId getId() const override { return ScreenId::HID_MOUSE_JIGGLER; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    static constexpr uint32_t ACTIVATE_MS        = 1500;
    static constexpr uint32_t JIGGLE_INTERVAL_MS = 25000;

    enum class State {
        CONFIRM,     // warn user serial console will drop
        ACTIVATING,  // USB.begin() called; waiting for host enumeration
        RUNNING      // periodically nudging the mouse
    };

    void drawConfirm(Canvas& canvas);
    void drawActivating(Canvas& canvas);
    void drawRunning(Canvas& canvas);

    void activateHid();
    void jiggle();

    State    m_state;
    bool     m_active;
    bool     m_shouldExit;
    bool     m_needsRedraw;
    uint32_t m_activatingStart;
    uint32_t m_lastJiggle;
    uint32_t m_lastUpdate;
    uint32_t m_jiggleCount;
    uint8_t  m_patternIdx;

    ui::FooterHints m_footer;
};

} // namespace adversary
