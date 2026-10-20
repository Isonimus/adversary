#pragma once
#include "ui/screens/screen_interface.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "ui/components/list_scroll_model.h"
#include "ui/components/scrollable_list.h"
#include "ui/components/confirm_dialog.h"
#include "modules/badusb/ducky_interpreter.h"
#include "modules/badusb/script_entry.h"
#include <vector>

#if defined(TARGET_CARDPUTER)
#include "modules/badusb/usb_hid_keyboard.h"
#endif

namespace adversary {

class BadUsbScreen : public IScreen {
public:
    BadUsbScreen();
    virtual ~BadUsbScreen() { freeScriptBuffer(); }

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;

    const char* getName() const override { return "BadUSB"; }
    ScreenId getId() const override { return ScreenId::USB_BADUSB; }
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    static constexpr size_t SCRIPT_BUF_SIZE  = 8192; // max SD script size

    enum class State {
        CONFIRM,     // warn user serial console will drop
        ACTIVATING,  // USB.begin() called; waiting ~1.5 s for host enumeration
        IDLE,        // script selection list
        RUNNING,     // executing a script
        DONE         // script finished, waiting for ESC
    };

    void drawConfirm(Canvas& canvas);
    void showHidConfirm();  // configure + raise the shared confirm overlay (ADR-0085; Cardputer)
    void drawActivating(Canvas& canvas);
    void drawIdle(Canvas& canvas);
    void drawRunning(Canvas& canvas);
    void drawDone(Canvas& canvas);

    void activateHid();
    void runScript(int index);
    void loadScripts();
    void parseScriptName(const char* sdPath, char* nameBuf, size_t bufSize);
    void freeScriptBuffer();

    State    m_state;
    bool     m_active;
    bool     m_shouldExit;
    bool     m_needsRedraw;
    // The shared model owns selection + scroll window + wrap; drawIdle feeds it the live viewport
    // each frame so the two can't drift (slice-0062/0065).
    ListScrollModel m_scriptList;
    uint32_t m_activatingStart;
    char*    m_scriptBuffer;   // heap buffer for active SD script; nullptr when unused
    const char* m_scriptName; // display name of the running script

    std::vector<ScriptEntry> m_scripts;
    DuckyInterpreter m_interp;
    ui::FooterHints  m_footer;
    ui::ConfirmDialog m_confirmDialog;  // shared HID-enable confirm overlay (ADR-0085)
    uint32_t         m_lastUpdate;
};

} // namespace adversary
