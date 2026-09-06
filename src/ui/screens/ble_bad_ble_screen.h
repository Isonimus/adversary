/**
 * @file ble_bad_ble_screen.h
 * @brief UI for executing BadBLE attacks (HID Keyboard Injection)
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "modules/ble/ble_spanner.h"
#include "modules/badusb/ducky_interpreter.h"
#include "modules/badusb/script_entry.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "ui/components/text_input_popup.h"
#include <vector>

namespace adversary {

class BleBadBleScreen : public IScreen {
public:
    BleBadBleScreen();
    virtual ~BleBadBleScreen();

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    
    const char* getName() const override { return "BadBLE"; }
    ScreenId getId() const override { return ScreenId::BLE_BAD_BLE; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    enum class State {
        IDLE,
        ADVERTISING,
        CONNECTED,
        INPUT_CUSTOM,
        SCRIPT_SELECT,   // browsing SD DuckyScript files
        SCRIPT_RUNNING,  // DuckyInterpreter executing over BLE
        SCRIPT_DONE      // script finished
    };

    enum class TargetOS {
        WINDOWS,
        LINUX,
        MACOS
    };

    void drawIdle(Canvas& canvas);
    void drawAdvertising(Canvas& canvas);
    void drawConnected(Canvas& canvas);
    void drawInputCustom(Canvas& canvas);
    void drawScriptSelect(Canvas& canvas);
    void drawScriptRunning(Canvas& canvas);
    void drawScriptDone(Canvas& canvas);

    void startAdvertising();
    void stopAdvertising();
    void injectCommand();

    void loadBleScripts();
    void runBleScript(int index);
    void freeBleScriptBuf();
    void parseBleScriptName(const char* sdPath, char* nameBuf, size_t bufSize);

    State m_state;
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    
    ui::FooterHints footerHints_;
    uint32_t m_lastUpdate;
    int m_actionSelection;
    int m_menuSelection; // For IDLE menu
    int m_scrollOffset;
    TargetOS m_targetOs;
    
    char m_sessionName[33];
    uint16_t m_appearance;
    TextInputPopup m_namePopup;
    
    char m_customInputBuf[64];
    int m_customInputLen;

    // DuckyScript over BLE — all existing hardcoded actions live in injectCommand();
    // these members support the additional "12. DuckyScript..." entry (item index 11).
    DuckyInterpreter         m_bleInterp;
    std::vector<ScriptEntry> m_bleScripts;
    int                      m_bleScriptSel;
    int                      m_bleScriptOffset;
    char*                    m_bleScriptBuf;
    const char*              m_bleScriptName;
};

} // namespace adversary
