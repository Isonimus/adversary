#include "badusb_screen.h"
#include "ui/theme.h"
#include "config/config.h"
#include "modules/storage/settings_manager.h"
#include <Arduino.h>
#include <SD.h>

namespace adversary {

// ---------------------------------------------------------------------------
// Built-in DuckyScript scripts — always available, seeded to SD on first load
// ---------------------------------------------------------------------------
struct BuiltInScript { const char* name; const char* ducky; };

static const BuiltInScript BUILT_IN_SCRIPTS[] = {
    {
        "Hello World",
        "REM Type into the focused field\n"
        "DELAY 500\n"
        "STRING Hello from The Adversary!\n"
        "ENTER\n"
    },
    {
        "Win: Open Notepad",
        "REM Open Notepad and type a message (Windows)\n"
        "DELAY 500\n"
        "GUI r\n"
        "DELAY 800\n"
        "STRING notepad\n"
        "ENTER\n"
        "DELAY 1200\n"
        "STRING Cardputer was here.\n"
        "ENTER\n"
    },
    {
        "Win: PowerShell",
        "REM Open PowerShell (Windows)\n"
        "DELAY 500\n"
        "GUI r\n"
        "DELAY 800\n"
        "STRING powershell\n"
        "ENTER\n"
        "DELAY 1200\n"
        "STRING whoami\n"
        "ENTER\n"
    },
    {
        "Win: WiFi Dump",
        "REM List saved WiFi profiles (Windows)\n"
        "DELAY 500\n"
        "GUI r\n"
        "DELAY 800\n"
        "STRING cmd\n"
        "ENTER\n"
        "DELAY 1200\n"
        "STRING netsh wlan show profiles\n"
        "ENTER\n"
    },
    {
        "Win: Lock Screen",
        "REM Lock the Windows workstation\n"
        "DELAY 500\n"
        "GUI l\n"
    },
    {
        "Lin: Open Terminal",
        "REM Open GNOME/KDE terminal (Linux)\n"
        "CTRL ALT t\n"
        "DELAY 1200\n"
        "STRING echo Cardputer was here\n"
        "ENTER\n"
    },
    {
        "macOS: Open Terminal",
        "REM Open Terminal via Spotlight (macOS)\n"
        "DELAY 500\n"
        "GUI SPACE\n"
        "DELAY 800\n"
        "STRING terminal\n"
        "ENTER\n"
        "DELAY 1500\n"
        "STRING echo Cardputer was here\n"
        "ENTER\n"
    },
};
static constexpr int NUM_BUILT_IN_SCRIPTS = (int)(sizeof(BUILT_IN_SCRIPTS) / sizeof(BUILT_IN_SCRIPTS[0]));

// ---------------------------------------------------------------------------
// Screen lifecycle
// ---------------------------------------------------------------------------
BadUsbScreen::BadUsbScreen()
    : m_state(State::CONFIRM)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_activatingStart(0)
    , m_scriptBuffer(nullptr)
    , m_scriptName(nullptr)
    , m_lastUpdate(0)
{}

void BadUsbScreen::show() {
    m_active         = true;
    m_shouldExit     = false;
    m_needsRedraw    = true;

    loadScripts();
    m_scriptList.setItemCount((int)m_scripts.size());
    m_scriptList.setSelectedIndex(0);

    m_confirmDialog.hide();
#if defined(TARGET_CARDPUTER)
    if (UsbHidKeyboard::getInstance().isReady()) {
        m_state = State::IDLE;
        m_footer.setHints({{'\n', "Run", true}, {'`', "Back", true}});
    } else {
        m_state = State::CONFIRM;
        m_footer.setHints({{'`', "Back", true}});
        showHidConfirm();  // the Cardputer confirm is the shared ConfirmDialog overlay (ADR-0085)
    }
#else
    m_state = State::CONFIRM;
    m_footer.setHints({{'`', "Back", true}});
#endif

    m_footer.setFocus(false);  // never carry footer focus across visits
}

void BadUsbScreen::hide() {
    m_interp.requestAbort();
    m_active = false;
    freeScriptBuffer();
}

void BadUsbScreen::update() {
    if (!m_active) return;

#if defined(TARGET_CARDPUTER)
    if (m_state == State::ACTIVATING) {
        if (millis() - m_activatingStart >= 1500) {
            m_state = State::IDLE;
            m_footer.setHints({{'\n', "Run", true}, {'`', "Back", true}});
            m_needsRedraw = true;
        } else if (millis() - m_lastUpdate > 100) {
            m_lastUpdate  = millis();
            m_needsRedraw = true;
        }
        return;
    }
#endif

    if (m_state != State::RUNNING) return;

#if defined(TARGET_CARDPUTER)
    auto result = m_interp.step(UsbHidKeyboard::getInstance());
    switch (result) {
        case DuckyInterpreter::StepResult::DONE:
            m_state      = State::DONE;
            m_needsRedraw = true;
            m_footer.setHints({{'`', "Back", true}});
            freeScriptBuffer();
            break;
        case DuckyInterpreter::StepResult::ABORTED:
            m_state      = State::IDLE;
            m_needsRedraw = true;
            m_footer.setHints({{'\n', "Run", true}, {'`', "Back", true}});
            freeScriptBuffer();
            break;
        default:
            if (millis() - m_lastUpdate > 100) {
                m_lastUpdate  = millis();
                m_needsRedraw = true;
            }
            break;
    }
#endif
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void BadUsbScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;
    m_needsRedraw = false;
    canvas.fillScreen(theme::BG_PRIMARY());

    switch (m_state) {
        case State::CONFIRM:    drawConfirm(canvas);    break;
        case State::ACTIVATING: drawActivating(canvas); break;
        case State::IDLE:       drawIdle(canvas);       break;
        case State::RUNNING:    drawRunning(canvas);    break;
        case State::DONE:       drawDone(canvas);       break;
    }
    m_footer.render(canvas);
    m_confirmDialog.render(canvas);  // confirm overlay floats above everything (ADR-0085)
}

void BadUsbScreen::drawConfirm(Canvas& canvas) {
#if defined(TARGET_CARDPUTER)
    // The Cardputer confirm is the shared ConfirmDialog overlay (ADR-0085), rendered after the
    // footer in render(); nothing to draw as the base here.
    (void)canvas;
#else
    ui::StatusBar::render(canvas, "BadUSB", "N/A");
    canvas.setTextDatum(top_center);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("USB HID not available", config::SCREEN_WIDTH/2, 50);
    canvas.drawString("on this device.", config::SCREEN_WIDTH/2, 66);
    canvas.setTextDatum(top_left);
#endif
}

void BadUsbScreen::showHidConfirm() {
#if defined(TARGET_CARDPUTER)
    m_confirmDialog.setQuestion("Enable USB HID mode?");
    m_confirmDialog.setSeverity(ui::ConfirmDialog::Severity::Warning);
    m_confirmDialog.clearDetails();
    m_confirmDialog.addDetail("Serial console off to reboot.");
    m_confirmDialog.addDetail("All other features work.");
    m_confirmDialog.setOnConfirm([this]() { activateHid(); });
    m_confirmDialog.setOnCancel([this]() { m_shouldExit = true; });
    m_confirmDialog.show();
    m_needsRedraw = true;
#endif
}

void BadUsbScreen::drawActivating(Canvas& canvas) {
#if defined(TARGET_CARDPUTER)
    ui::StatusBar::render(canvas, "BadUSB", "WAIT");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("Activating USB HID...", cx, 38);

    uint32_t elapsed = millis() - m_activatingStart;
    if (elapsed > 1500) elapsed = 1500;
    const int barX = 20;
    const int barW = config::SCREEN_WIDTH - 40;
    const int barY = 68;
    canvas.fillRect(barX, barY, barW, 8, theme::BG_TERTIARY());
    canvas.fillRect(barX, barY, (int)(elapsed * barW / 1500), 8, theme::ACCENT());

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("Serial console offline", cx, 86);
    canvas.setTextDatum(top_left);
#endif
}

void BadUsbScreen::drawIdle(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadUSB", "SELECT");

    int total = (int)m_scripts.size();
    if (total == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setTextDatum(top_center);
        canvas.drawString("No scripts found", config::SCREEN_WIDTH/2, 50);
        canvas.setTextDatum(top_left);
        return;
    }

    // The shared component paints the selected-row band and the one proportional thumb (replacing
    // the bespoke copy) and derives the visible-row count from the rect; this callback draws the
    // script name plus the "SD" badge for user scripts (slice-0063/0065).
    const int16_t listY = 22;
    const int16_t listH = (int16_t)(config::SCREEN_HEIGHT - ui::FOOTER_HEIGHT - listY);
    m_scriptList.setItemCount(total);
    ScrollableList::render(canvas, m_scriptList, 0, listY, config::SCREEN_WIDTH, listH,
        theme::LIST_ROW_HEIGHT, [this](Canvas& c, int i, int16_t rowY, bool sel) {
            c.setTextColor(theme::TEXT_PRIMARY());
            c.setCursor(10, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(m_scripts[i].name);

            if (!m_scripts[i].isBuiltIn) {
                c.setTextColor(sel ? theme::TEXT_PRIMARY() : theme::TEXT_SECONDARY());
                c.setCursor(config::SCREEN_WIDTH - 22, rowY + theme::LIST_ROW_TEXT_DY);
                c.print("SD");
            }
        }, !m_footer.hasFocus());  // dim the band while the footer holds focus
}

void BadUsbScreen::drawRunning(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadUSB", "RUNNING");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::TEXT_PRIMARY());
    if (m_scriptName)
        canvas.drawString(m_scriptName, cx, 35);

    canvas.setTextColor(theme::TEXT_SECONDARY());
    char lineBuf[32];
    snprintf(lineBuf, sizeof(lineBuf), "Line %d...", m_interp.currentLine());
    canvas.drawString(lineBuf, cx, 58);

    int dots = (millis() / 400) % 4;
    char anim[8] = "   ";
    for (int i = 0; i < dots; i++) anim[i] = '.';
    canvas.drawString(anim, cx, 78);

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("ESC to abort", cx, 100);
    canvas.setTextDatum(top_left);
}

void BadUsbScreen::drawDone(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadUSB", "DONE");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("Script complete.", cx, 35);

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("Serial offline until", cx, 60);
    canvas.drawString("next reboot.", cx, 76);

    canvas.setTextDatum(top_left);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool BadUsbScreen::handleInput(char key) {
    m_needsRedraw = true;

    if (m_state == State::ACTIVATING) return true;

    // The confirm overlay, when up, owns all input (ADR-0085; Cardputer HID-enable).
    if (m_confirmDialog.isVisible()) {
        return m_confirmDialog.handleInput(key);
    }

    // Footer focus, screen-wide: SPACE toggles focus, ;/. cycle the hints, Enter fires the
    // selected one. Placed before the per-state handling so every interactive state (the CONFIRM
    // decision, the IDLE list, RUNNING/DONE) honors SPACE the same way; the band only exists in
    // IDLE, so elsewhere the focused footer badge is the cue. Transient ACTIVATING is excluded by
    // the guard above. When unfocused, handleInputWithDispatch consumes only SPACE, so every
    // direct key still falls through to its state handler below.
    {
        char footerAction = 0;
        if (m_footer.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // dispatch the focused hint
            return true;  // consumed a focus toggle or hint navigation
        }
    }

#if !defined(TARGET_CARDPUTER)
    // Non-Cardputer: CONFIRM is the "USB HID not available" notice; any key leaves.
    if (m_state == State::CONFIRM) {
        if (key == '\n' || key == '`') m_shouldExit = true;
        return true;
    }
#endif

    if (m_state == State::RUNNING) {
        if (key == '`') m_interp.requestAbort();
        return true;
    }

    if (m_state == State::DONE) {
        if (key == '`') {
            m_state = State::IDLE;
            m_footer.setHints({{'\n', "Run", true}, {'`', "Back", true}});
        }
        return true;
    }

    // IDLE
    switch (key) {
        case ';':  // UP
            m_scriptList.setItemCount((int)m_scripts.size());
            m_scriptList.moveUp();  // wraps to last; model keeps the selection on screen
            return true;
        case '.':  // DOWN
            m_scriptList.setItemCount((int)m_scripts.size());
            m_scriptList.moveDown();  // wraps to first; model keeps the selection on screen
            return true;
        case '\n':
            runScript(m_scriptList.selectedIndex());
            return true;
        case '`':
            m_shouldExit = true;
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void BadUsbScreen::activateHid() {
#if defined(TARGET_CARDPUTER)
    UsbHidKeyboard& kbd = UsbHidKeyboard::getInstance();
    auto& settings = SettingsManager::getInstance().get();
    kbd.setLayout(settings.wireless.keyboardLayout == 1
        ? IHidKeyboard::Layout::ES : IHidKeyboard::Layout::US);
    kbd.begin();
    m_activatingStart = millis();
    m_state      = State::ACTIVATING;
    m_footer.setHints({});
    m_needsRedraw = true;
#endif
}

void BadUsbScreen::runScript(int index) {
    if (index < 0 || index >= (int)m_scripts.size()) return;

    const ScriptEntry& entry = m_scripts[index];
    const char* ducky = nullptr;

    if (entry.isBuiltIn) {
        ducky = entry.duckyPtr;
    } else {
        freeScriptBuffer();
        m_scriptBuffer = new (std::nothrow) char[SCRIPT_BUF_SIZE];
        if (!m_scriptBuffer) return;
        m_scriptBuffer[0] = '\0';

        File f = SD.open(entry.sdPath, FILE_READ);
        if (!f) { freeScriptBuffer(); return; }
        size_t n = f.readBytes(m_scriptBuffer, SCRIPT_BUF_SIZE - 1);
        m_scriptBuffer[n] = '\0';
        f.close();
        ducky = m_scriptBuffer;
    }

    m_scriptName  = entry.name;
    m_interp.load(ducky);
    m_state       = State::RUNNING;
    m_needsRedraw = true;
    m_footer.setHints({{'`', "Abort", true}});
}

void BadUsbScreen::freeScriptBuffer() {
    delete[] m_scriptBuffer;
    m_scriptBuffer = nullptr;
}

void BadUsbScreen::loadScripts() {
    m_scripts.clear();

    // Built-ins are always available
    for (int i = 0; i < NUM_BUILT_IN_SCRIPTS; i++) {
        ScriptEntry e;
        strncpy(e.name, BUILT_IN_SCRIPTS[i].name, sizeof(e.name) - 1);
        e.isBuiltIn = true;
        e.duckyPtr  = BUILT_IN_SCRIPTS[i].ducky;
        m_scripts.push_back(e);
    }

    // SD scripts: enumerate /adversary/badusb/*.txt
#ifdef ESP32
    if (!SD.exists(config::SD_BADUSB_PATH)) {
        SD.mkdir(config::SD_BADUSB_PATH);
        return;  // freshly created — no user scripts yet
    }

    File dir = SD.open(config::SD_BADUSB_PATH);
    if (!dir || !dir.isDirectory()) { dir.close(); return; }

    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* fname = entry.name();
            const char* ext   = strrchr(fname, '.');
            if (ext && strcasecmp(ext, ".txt") == 0) {
                ScriptEntry se;
                se.isBuiltIn = false;
                snprintf(se.sdPath, sizeof(se.sdPath), "%s/%s",
                         config::SD_BADUSB_PATH, fname);
                parseScriptName(se.sdPath, se.name, sizeof(se.name));
                if (se.name[0] == '\0') {
                    // Fall back to filename without extension
                    strncpy(se.name, fname, sizeof(se.name) - 1);
                    char* dot = strrchr(se.name, '.');
                    if (dot) *dot = '\0';
                }
                m_scripts.push_back(se);
            }
        }
        entry.close();
    }
    dir.close();
#endif
}

void BadUsbScreen::parseScriptName(const char* sdPath, char* nameBuf, size_t bufSize) {
    nameBuf[0] = '\0';
#ifdef ESP32
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return;

    char line[128];
    int  pos  = 0;
    while (f.available() && pos < (int)sizeof(line) - 1) {
        char c = (char)f.read();
        if (c == '\n' || c == '\r') break;
        line[pos++] = c;
    }
    f.close();

    line[pos] = '\0';
    // Trim trailing whitespace
    while (pos > 0 && (line[pos-1] == ' ' || line[pos-1] == '\r' || line[pos-1] == '\t'))
        line[--pos] = '\0';

    // Accept "REM name" or "# name" as script name comment
    if (strncasecmp(line, "REM ", 4) == 0 && pos > 4)
        strncpy(nameBuf, line + 4, bufSize - 1);
#endif
}

} // namespace adversary
