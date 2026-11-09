/**
 * @file ble_bad_ble_screen.cpp
 * @brief BleBadBleScreen implementation
 */

#include "ble_bad_ble_screen.h"
#include "modules/ble/ble_hid_keyboard.h"
#include "modules/badusb/hid_report.h" // MOUSE_BUTTON_* bitmask constants
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/action_button.h"
#include "ui/components/config_row.h"
#include "ui/components/config_input.h"
#include "modules/storage/settings_manager.h"
#include "modules/ble/ble_scanner.h"
#include "config/config.h"
#include <SD.h>

namespace adversary {

namespace {

// Mouse nudge/scroll magnitudes and gamepad full-deflection value for the demo action menus
// (ADR-0150). Kept modest so a single press is a visible-but-controllable movement on the host.
constexpr int8_t  MOUSE_MOVE_STEP   = 20;  // pixels per directional nudge
constexpr int8_t  MOUSE_WHEEL_STEP  = 1;   // wheel detents per scroll action
constexpr int8_t  GAMEPAD_AXIS_MAX  = 127; // full stick deflection (int8 max)
constexpr int     MOUSE_JIGGLE_CYCLES = 3; // left/right pairs for the anti-idle jiggle

// Gamepad button bitmask (bit 0 = button 1). The report core owns only the count/layout, so the
// named buttons are this screen's domain (hid_report::GAMEPAD_BUTTON_COUNT).
constexpr uint8_t GAMEPAD_BTN_A   = 1 << 0;
constexpr uint8_t GAMEPAD_BTN_B   = 1 << 1;
constexpr uint8_t GAMEPAD_BTN_X   = 1 << 2;
constexpr uint8_t GAMEPAD_BTN_Y   = 1 << 3;
constexpr uint8_t GAMEPAD_BTN_ALL = 0xFF;

// Spacing between a press report and its release so the host registers a discrete event rather
// than coalescing the two into no-op.
constexpr uint32_t HID_ACTION_GAP_MS = 30;

// Mouse and gamepad action labels are OS-agnostic (unlike the keyboard menu), so they are fixed
// tables. Row order must match the switch in injectMouseCommand()/injectGamepadCommand().
const char* const MOUSE_ACTIONS[] = {
    "1. Move Up", "2. Move Down", "3. Move Left", "4. Move Right",
    "5. Left Click", "6. Right Click", "7. Middle Click", "8. Double Click",
    "9. Scroll Up", "10. Scroll Down", "11. Jiggle (anti-idle)"
};
const char* const GAMEPAD_ACTIONS[] = {
    "1. Up", "2. Down", "3. Left", "4. Right",
    "5. Button A", "6. Button B", "7. Button X", "8. Button Y",
    "9. All Buttons"
};

const char* hidClassName(BleHidClass hidClass) {
    switch (hidClass) {
        case BleHidClass::KEYBOARD: return "Keyboard";
        case BleHidClass::MOUSE:    return "Mouse";
        case BleHidClass::GAMEPAD:  return "Gamepad";
    }
    return "Keyboard";
}

} // namespace

BleBadBleScreen::BleBadBleScreen()
    : m_state(State::IDLE)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_targetOs(TargetOS::LINUX)
    , m_customInputBuf{0}
    , m_customInputLen(0)
    , m_bleScriptBuf(nullptr)
    , m_bleScriptName(nullptr)
{
}

BleBadBleScreen::~BleBadBleScreen() {
    freeBleScriptBuf();
}

void BleBadBleScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_state = State::IDLE;
    m_actionList.setSelectedIndex(0);
    m_configList.setItemCount(0);   // For Idle Config: reset selection + scroll to the top

    // Load Defaults for Session
    auto& settings = SettingsManager::getInstance().get();
    m_targetOs = TargetOS::LINUX; // Default
    strncpy(m_sessionName, settings.wireless.bleName, 32);
    m_sessionName[32] = '\0';
    if (m_sessionName[0] == '\0') strcpy(m_sessionName, "Universal Key");
    // The persisted GAP appearance maps to the real class (joystick folds into gamepad, unknown
    // falls back to keyboard); the class is the honest session selector from here on (ADR-0150).
    m_hidClass = hidClassForAppearance(settings.wireless.bleAppearance);
    m_actionList.setItemCount(connectedActionCount()); // Connected menu, sized for the class

    footerHints_.setHints({
        {'\n', "Select", true},
        {'`', "Back", true}
    });
}

void BleBadBleScreen::hide() {
    stopAdvertising();
    m_active = false;
    // Fully release BLE stack so the heap is reclaimed when this screen is deleted.
    // This mirrors what stopAllAttacks() does, but must also happen on normal back-navigation.
    BLESpanner::getInstance().forceRelease();
    BLEScanner::getInstance().deinit();
}

void BleBadBleScreen::update() {
    if (!m_active) return;

    auto& spanner = BLESpanner::getInstance();

    // Auto-transition to CONNECTED if spanner reports connection
    if (m_state == State::ADVERTISING && spanner.isConnected()) {
        m_state = State::CONNECTED;
        m_needsRedraw = true;
        footerHints_.setHints({
            {'\n', "Inject", true},
            {'`', "Back", true}
        });
    } else if (m_state == State::CONNECTED && !spanner.isConnected()) {
        m_state = State::ADVERTISING;
        m_needsRedraw = true;
        footerHints_.setHints({
            {'\n', "Wait...", false},
            {'`', "Back", true}
        });
    }

    // Abort script if BLE drops mid-run
    if (m_state == State::SCRIPT_RUNNING && !spanner.isConnected()) {
        m_bleInterp.requestAbort();
    }

    // Step the DuckyInterpreter one iteration per frame
    if (m_state == State::SCRIPT_RUNNING) {
        auto result = m_bleInterp.step(BleHidKeyboard::getInstance());
        switch (result) {
            case DuckyInterpreter::StepResult::DONE:
                m_state = State::SCRIPT_DONE;
                m_needsRedraw = true;
                footerHints_.setHints({{'`', "Back", true}});
                freeBleScriptBuf();
                break;
            case DuckyInterpreter::StepResult::ABORTED:
                m_state = State::SCRIPT_SELECT;
                m_needsRedraw = true;
                footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
                freeBleScriptBuf();
                break;
            default:
                if (millis() - m_lastUpdate > 100) {
                    m_lastUpdate = millis();
                    m_needsRedraw = true;
                }
                break;
        }
        return;
    }

    // Dynamic redraw rate based on state
    uint32_t interval = (m_state == State::ADVERTISING) ? 33 : 1000;
    if (millis() - m_lastUpdate > interval) {
        m_lastUpdate = millis();
        m_needsRedraw = true;
    }
}

void BleBadBleScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;
    m_needsRedraw = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (m_state) {
        case State::IDLE:           drawIdle(canvas);         break;
        case State::ADVERTISING:    drawAdvertising(canvas);  break;
        case State::CONNECTED:      drawConnected(canvas);    break;
        case State::INPUT_CUSTOM:   drawInputCustom(canvas);  break;
        case State::SCRIPT_SELECT:  drawScriptSelect(canvas); break;
        case State::SCRIPT_RUNNING: drawScriptRunning(canvas); break;
        case State::SCRIPT_DONE:    drawScriptDone(canvas);   break;
    }
    
    footerHints_.render(canvas);
    m_namePopup.render(canvas); // Render popup on top
}

void BleBadBleScreen::drawIdle(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "CONFIG");
    
    const int16_t listTop = 35;
    const int16_t availableHeight = config::SCREEN_HEIGHT - listTop - ui::FOOTER_HEIGHT - 4;

    const char* labels[] = {
        "Name:",
        // Real device-class selector (ADR-0150): drives the report map, the GAP icon, and the
        // connected action menu, so the device enumerates as exactly this class.
        "HID Class:",
        // Only consulted for the Keyboard class (chooses the OS command set); inert for Mouse
        // and Gamepad, whose actions are OS-agnostic.
        "Target:"
    };

    // The shared component owns the viewport, the selection band and the thumb; this callback
    // paints one config row, keeping this screen's value X and two-tone value colour verbatim
    // (the terminal start row is the shared ActionButton, slice-0089).
    m_configList.setItemCount(IDLE_OPTION_COUNT);
    ScrollableList::render(canvas, m_configList, 0, listTop, canvas.width(), availableHeight,
        theme::LIST_ROW_HEIGHT, [this, &labels](Canvas& c, int idx, int16_t rowY, bool selected) {
            if (idx == IDLE_OPTION_COUNT - 1) {
                ui::ActionButton::render(c, rowY, "START ATTACK", selected);
                return;
            }
            char valueStr[24] = "";
            switch (idx) {
                case 0: // Name
                    strncpy(valueStr, m_sessionName, sizeof(valueStr) - 1);
                    break;
                case 1: // HID class (drives report map + GAP icon + action menu)
                    strncpy(valueStr, hidClassName(m_hidClass), sizeof(valueStr) - 1);
                    break;
                case 2: // Target
                    switch (m_targetOs) {
                        case TargetOS::LINUX:   strncpy(valueStr, "LINUX", sizeof(valueStr) - 1); break;
                        case TargetOS::WINDOWS: strncpy(valueStr, "WINDOWS", sizeof(valueStr) - 1); break;
                        case TargetOS::MACOS:   strncpy(valueStr, "MACOS", sizeof(valueStr) - 1); break;
                    }
                    break;
            }
            ui::ConfigRow::render(c, rowY, labels[idx], valueStr);
        },
        !footerHints_.hasFocus());
}

// Reusing the "Standard" attack visualization from BleAppleAttackScreen
void BleBadBleScreen::drawAdvertising(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "ADVERTISING");
    
    int centerX = config::SCREEN_WIDTH / 2;
    int centerY = config::SCREEN_HEIGHT / 2 - 10;
    
    // Pulse animation (Standardized)
    uint32_t elapsed = millis() % 1000;
    int radius = 20 + (elapsed / 40); // 20-45px pulse
    canvas.drawCircle(centerX, centerY, radius, theme::ACCENT());
    canvas.fillCircle(centerX, centerY, 15, theme::ACCENT());
    
    // Status text below animation
    int y = centerY + 40;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextDatum(top_center);
    canvas.drawString("Waiting for Pair...", centerX, y);
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    char classLine[32];
    snprintf(classLine, sizeof(classLine), "Class: %s", hidClassName(m_hidClass));
    canvas.drawString(classLine, centerX, y + 15);
    canvas.setTextDatum(top_left); // Reset
}

void BleBadBleScreen::drawConnected(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "CONNECTED [!]");
    
    // Expanded Action Lists - Index 10 is reserved for Dynamic Layout Toggle
    char layoutStr[20];
    KeyboardLayout currentLayout = BLESpanner::getInstance().getLayout();
    snprintf(layoutStr, sizeof(layoutStr), "11. Layout: [%s]", (currentLayout == KeyboardLayout::US) ? "US" : "ES");

    const char* actions_win[] = {
        "1. Send 'GUI+R' (Run)", "2. Type 'cmd'", "3. Type 'powershell'", "4. Rickroll (YouTube)",
        "5. Notepad Payload", "6. Fake Update (F11)", "7. Lock (GUI+L)", "8. Desktop (GUI+D)",
        "9. Ctrl+Alt+Del", "10. Custom Input...", layoutStr, "12. DuckyScript..."
    };
    const char* actions_lin[] = {
        "1. Terminal (Ctrl+Alt+T)", "2. Run (Alt+F2)", "3. Rickroll (YouTube)", "4. Custom (Bash)",
        "5. Type 'htop'", "6. Matrix (cmatrix)", "7. Lock (Ctrl+Alt+L)", "8. Workspace Down",
        "9. Ctrl+Alt+Del", "10. Custom Input...", layoutStr, "12. DuckyScript..."
    };
    const char* actions_mac[] = {
        "1. Spotlight (Cmd+Space)", "2. Type 'Terminal'", "3. Rickroll (YouTube)", "4. Custom (Zsh)",
        "5. Say 'Hello'", "6. Volume Up x10", "7. Open Safari", "8. Lock (Cmd+Ctrl+Q)",
        "9. Ctrl+Alt+Del", "10. Custom Input...", layoutStr, "12. DuckyScript..."
    };

    // The action table is the OS-specific keyboard set, or the OS-agnostic mouse/gamepad set,
    // selected by the active HID class (ADR-0150). The keyboard arrays above are built regardless
    // (cheap) and only referenced on the keyboard branch.
    const char* const* currentActions = nullptr;
    switch (m_hidClass) {
        case BleHidClass::KEYBOARD:
            switch (m_targetOs) {
                case TargetOS::WINDOWS: currentActions = actions_win; break;
                case TargetOS::LINUX:   currentActions = actions_lin; break;
                case TargetOS::MACOS:   currentActions = actions_mac; break;
            }
            break;
        case BleHidClass::MOUSE:   currentActions = MOUSE_ACTIONS;   break;
        case BleHidClass::GAMEPAD: currentActions = GAMEPAD_ACTIONS; break;
    }

    // The shared component paints the selected-row band (dimmed when the footer holds focus) and
    // the thumb, and derives the visible-row count from the rect; this callback draws content only
    // (slice-0063/0066). The list spans from just under the status bar to the footer.
    const int16_t listTop = 22; // status bar is 20px; 2px padding
    const int16_t listH   = (int16_t)(config::SCREEN_HEIGHT - ui::FOOTER_HEIGHT - listTop);
    m_actionList.setItemCount(connectedActionCount());
    ScrollableList::render(canvas, m_actionList, 0, listTop, config::SCREEN_WIDTH, listH,
        theme::LIST_ROW_HEIGHT, [currentActions](Canvas& c, int idx, int16_t rowY, bool /*sel*/) {
            c.setTextColor(theme::TEXT_PRIMARY());
            c.setCursor(15, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(currentActions[idx]);
        },
        !footerHints_.hasFocus());
}

void BleBadBleScreen::drawInputCustom(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "CUSTOM INPUT");
    
    int y = 50;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(10, y);
    canvas.print("Type Command:");
    
    // Input Box
    y += 20;
    canvas.drawRect(10, y, config::SCREEN_WIDTH - 20, 24, theme::TEXT_SECONDARY());
    
    // Text inside box
    canvas.setCursor(14, y + 6);
    canvas.setTextColor(theme::ACCENT());
    canvas.print(m_customInputBuf);
    
    // Cursor
    if ((millis() / 500) % 2 == 0) {
        canvas.print("_");
    }
    
    // Instructions
    y += 35;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.print("Enter: Send  Esc: Cancel");
}

void BleBadBleScreen::startAdvertising() {
    BleSpamConfig cfg;
    cfg.provider = BleSpamProvider::HID;
    cfg.intensity = 10;
    
    // GAP Name handled by BLESpanner via config
    // NimBLEDevice::setDeviceName("Universal Key"); 
    
    cfg.name[0] = '\0';
    if (m_sessionName[0] != '\0') strncpy(cfg.name, m_sessionName, 32);
    // The selected class drives both the report map and the GAP appearance, so the device
    // enumerates as exactly this class (ADR-0150).
    cfg.hidClass = m_hidClass;

    BLESpanner::getInstance().start(cfg);
    m_state = State::ADVERTISING;
    
    footerHints_.setHints({
        {'\n', "Wait...", false},
        {'`', "Back", true}
    });
}

void BleBadBleScreen::stopAdvertising() {
    BLESpanner::getInstance().stop();
    m_state = State::IDLE;
    footerHints_.setHints({
        {'\n', "Start ADV", true},
        {'`', "Back", true}
    });
}

void BleBadBleScreen::injectCommand() {
    // The connected menu is class-specific: keyboard runs OS command macros, mouse/gamepad drive
    // their own HID reports (ADR-0150). Dispatch to the handler for the class in use this session.
    switch (m_hidClass) {
        case BleHidClass::KEYBOARD: injectKeyboardCommand(); break;
        case BleHidClass::MOUSE:    injectMouseCommand();    break;
        case BleHidClass::GAMEPAD:  injectGamepadCommand();  break;
    }
}

void BleBadBleScreen::injectKeyboardCommand() {
    auto& spanner = BLESpanner::getInstance();

    // Re-focus helper (Alt+Tab / Cmd+Tab logic could be added here if needed)
    // For now, assume user has focus.

    switch (m_targetOs) {
        case TargetOS::WINDOWS:
            switch (m_actionList.selectedIndex()) {
                case 0: spanner.sendKey(0x15, 0x08); break; // GUI+R
                case 1: spanner.typeString("cmd\n"); break;
                case 2: spanner.typeString("powershell\n"); break;
                case 3: // Rickroll
                    spanner.sendKey(0x15, 0x08); vTaskDelay(pdMS_TO_TICKS(500));
                    spanner.typeString("start https://bit.ly/4k5N\n"); 
                    break;
                case 4: // Notepad
                    spanner.sendKey(0x15, 0x08); vTaskDelay(pdMS_TO_TICKS(500));
                    spanner.typeString("notepad\n"); vTaskDelay(pdMS_TO_TICKS(1000));
                    spanner.typeString("Hello from the cardputer!\n");
                    break;
                case 5: spanner.sendKey(0x44, 0); break; // F11 (Fake Update) - Context dependent
                case 6: spanner.sendKey(0x0F, 0x08); break; // GUI+L (Lock)
                case 7: spanner.sendKey(0x07, 0x08); break; // GUI+D (Desktop)
                case 8: spanner.sendKey(0x4C, 0x05); break; // Ctrl+Alt+Del (0x4C)
                case 9: // Custom
                     m_state = State::INPUT_CUSTOM;
                     m_customInputLen = 0;
                     memset(m_customInputBuf, 0, sizeof(m_customInputBuf));
                     footerHints_.setHints({}); // Clear footer
                     m_needsRedraw = true;
                     break;
                case 10: // Toggle Layout
                     {
                         KeyboardLayout current = BLESpanner::getInstance().getLayout();
                         BLESpanner::getInstance().setLayout((current == KeyboardLayout::US) ? KeyboardLayout::ES : KeyboardLayout::US);
                         m_needsRedraw = true;
                     }
                     break;
                case 11: // DuckyScript
                     enterScriptSelect();
                     break;
            }
            break;

        case TargetOS::LINUX:
            switch (m_actionList.selectedIndex()) {
                case 0: spanner.sendKey(0x17, 0x05); break; // Ctrl+Alt+T
                case 1: spanner.sendKey(0x3B, 0x04); break; // Alt+F2
                case 2: // Rickroll
                    spanner.sendKey(0x3B, 0x04); vTaskDelay(pdMS_TO_TICKS(500));
                    spanner.typeString("xdg-open https://www.youtube.com/watch?v=dQw4w9WgXcQ\n");
                    break;
                case 3: spanner.typeString("echo 'Pwned by Cardputer'\n"); break;
                case 4: spanner.typeString("htop\n"); break;
                case 5: spanner.typeString("cmatrix\n"); break;
                case 6: spanner.sendKey(0x0F, 0x05); break; // Ctrl+Alt+L (Lock) - Distro dependent
                case 7: spanner.sendKey(0x51, 0x05); break; // Ctrl+Alt+Down (Buntus)
                case 8: spanner.sendKey(0x4C, 0x05); break; // Ctrl+Alt+Del
                case 9: // Custom
                     m_state = State::INPUT_CUSTOM;
                     m_customInputLen = 0;
                     memset(m_customInputBuf, 0, sizeof(m_customInputBuf));
                     footerHints_.setHints({});
                     m_needsRedraw = true;
                     break;
                case 10: // Toggle Layout
                     {
                         KeyboardLayout current = BLESpanner::getInstance().getLayout();
                         BLESpanner::getInstance().setLayout((current == KeyboardLayout::US) ? KeyboardLayout::ES : KeyboardLayout::US);
                         m_needsRedraw = true;
                     }
                     break;
                case 11: // DuckyScript
                     enterScriptSelect();
                     break;
            }
            break;

        case TargetOS::MACOS:
            switch (m_actionList.selectedIndex()) {
                case 0: spanner.sendKey(0x2C, 0x08); break; // Cmd+Space
                case 1: spanner.typeString("Terminal\n"); break;
                case 2: // Rickroll
                    spanner.sendKey(0x2C, 0x08); vTaskDelay(pdMS_TO_TICKS(500));
                    spanner.typeString("open https://www.youtube.com/watch?v=dQw4w9WgXcQ\n");
                    break;
                case 3: spanner.typeString("echo 'Zsh Pwned'\n"); break;
                case 4: spanner.typeString("say 'Hello World'\n"); break;
                case 5: 
                    for(int i=0;i<10;i++) { spanner.sendKey(0x80, 0); delay(50); } // Vol Up (0x80)
                    break;
                case 6: 
                     spanner.sendKey(0x2C, 0x08); vTaskDelay(pdMS_TO_TICKS(500));
                     spanner.typeString("open -a Safari\n");
                     break;
                case 7: spanner.sendKey(0x14, 0x09); break; // Cmd+Ctrl+Q (Lock)
                case 8: spanner.sendKey(0x4C, 0x05); break; // Ctrl+Alt+Del (mapped)
                case 9: // Custom
                     m_state = State::INPUT_CUSTOM;
                     m_customInputLen = 0;
                     memset(m_customInputBuf, 0, sizeof(m_customInputBuf));
                     footerHints_.setHints({});
                     m_needsRedraw = true;
                     break;
                case 10: // Toggle Layout
                     {
                         KeyboardLayout current = BLESpanner::getInstance().getLayout();
                         BLESpanner::getInstance().setLayout((current == KeyboardLayout::US) ? KeyboardLayout::ES : KeyboardLayout::US);
                         m_needsRedraw = true;
                     }
                     break;
                case 11: // DuckyScript
                     enterScriptSelect();
                     break;
            }
            break;
    }
}

void BleBadBleScreen::injectMouseCommand() {
    auto& spanner = BLESpanner::getInstance();
    switch (m_actionList.selectedIndex()) {
        case 0: spanner.sendMouseMove(0, -MOUSE_MOVE_STEP); break; // Up
        case 1: spanner.sendMouseMove(0,  MOUSE_MOVE_STEP); break; // Down
        case 2: spanner.sendMouseMove(-MOUSE_MOVE_STEP, 0); break; // Left
        case 3: spanner.sendMouseMove( MOUSE_MOVE_STEP, 0); break; // Right
        case 4: clickMouse(hid_report::MOUSE_BUTTON_LEFT);   break;
        case 5: clickMouse(hid_report::MOUSE_BUTTON_RIGHT);  break;
        case 6: clickMouse(hid_report::MOUSE_BUTTON_MIDDLE); break;
        case 7: // Double click
            clickMouse(hid_report::MOUSE_BUTTON_LEFT);
            clickMouse(hid_report::MOUSE_BUTTON_LEFT);
            break;
        case 8: spanner.sendMouseMove(0, 0, 0,  MOUSE_WHEEL_STEP); break; // Scroll up
        case 9: spanner.sendMouseMove(0, 0, 0, -MOUSE_WHEEL_STEP); break; // Scroll down
        case 10: // Jiggle (anti-idle): small symmetric moves that net to zero displacement
            for (int i = 0; i < MOUSE_JIGGLE_CYCLES; i++) {
                spanner.sendMouseMove(MOUSE_MOVE_STEP, 0);
                vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
                spanner.sendMouseMove(-MOUSE_MOVE_STEP, 0);
                vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
            }
            break;
    }
}

void BleBadBleScreen::injectGamepadCommand() {
    switch (m_actionList.selectedIndex()) {
        case 0: pulseGamepad(0, -GAMEPAD_AXIS_MAX, 0); break; // Up
        case 1: pulseGamepad(0,  GAMEPAD_AXIS_MAX, 0); break; // Down
        case 2: pulseGamepad(-GAMEPAD_AXIS_MAX, 0, 0); break; // Left
        case 3: pulseGamepad( GAMEPAD_AXIS_MAX, 0, 0); break; // Right
        case 4: pulseGamepad(0, 0, GAMEPAD_BTN_A); break;
        case 5: pulseGamepad(0, 0, GAMEPAD_BTN_B); break;
        case 6: pulseGamepad(0, 0, GAMEPAD_BTN_X); break;
        case 7: pulseGamepad(0, 0, GAMEPAD_BTN_Y); break;
        case 8: pulseGamepad(0, 0, GAMEPAD_BTN_ALL); break;
    }
}

void BleBadBleScreen::clickMouse(uint8_t button) {
    auto& spanner = BLESpanner::getInstance();
    spanner.sendMouseMove(0, 0, button, 0);           // press
    vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
    spanner.sendMouseMove(0, 0, 0, 0);                // release (buttons cleared)
    vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
}

void BleBadBleScreen::pulseGamepad(int8_t x, int8_t y, uint8_t buttons) {
    auto& spanner = BLESpanner::getInstance();
    spanner.sendGamepad(x, y, buttons);               // deflect / press
    vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
    spanner.sendGamepad(0, 0, 0);                      // return to neutral / release
    vTaskDelay(pdMS_TO_TICKS(HID_ACTION_GAP_MS));
}

int BleBadBleScreen::connectedActionCount() const {
    // Row counts must match the action tables / injectXxxCommand() switches above.
    static_assert(sizeof(MOUSE_ACTIONS)   / sizeof(MOUSE_ACTIONS[0])   == MOUSE_ACTION_COUNT,
                  "MOUSE_ACTIONS size must match MOUSE_ACTION_COUNT");
    static_assert(sizeof(GAMEPAD_ACTIONS) / sizeof(GAMEPAD_ACTIONS[0]) == GAMEPAD_ACTION_COUNT,
                  "GAMEPAD_ACTIONS size must match GAMEPAD_ACTION_COUNT");
    switch (m_hidClass) {
        case BleHidClass::KEYBOARD: return KEYBOARD_ACTION_COUNT;
        case BleHidClass::MOUSE:    return MOUSE_ACTION_COUNT;
        case BleHidClass::GAMEPAD:  return GAMEPAD_ACTION_COUNT;
    }
    return KEYBOARD_ACTION_COUNT;
}

bool BleBadBleScreen::handleInput(char key) {
    m_needsRedraw = true;

    if (m_namePopup.isVisible()) {
        return m_namePopup.handleInput(key);
    }

    if (m_state == State::INPUT_CUSTOM) {
        if (key == '\n' || key == '\r') {
            // Send Command
            if (m_customInputLen > 0) {
                BLESpanner::getInstance().typeString(m_customInputBuf);
                BLESpanner::getInstance().typeString("\n");
            }
            // Return to connected
            m_state = State::CONNECTED;
            footerHints_.setHints({
                {'\n', "Inject", true},
                {'x', "Stop", true},
                {'`', "Back", true}
            });
        } else if (key == '`' || key == 0x1B) {
            // Cancel
            m_state = State::CONNECTED;
            footerHints_.setHints({
                {'\n', "Inject", true},
                {'x', "Stop", true},
                {'`', "Back", true}
            });
        } else if (key == 0x08 || key == 0x7F) { // Backspace
            if (m_customInputLen > 0) {
                m_customInputBuf[--m_customInputLen] = 0;
            }
        } else if (key >= 32 && key <= 126 && m_customInputLen < 63) {
            m_customInputBuf[m_customInputLen++] = key;
            m_customInputBuf[m_customInputLen] = 0;
        }
        return true;
    }

    // IDLE MENU HANDLING. Canonical config-form input from the shared classifier (ADR-0107):
    // row 0 (Name) is the popup row (ENTER opens the name popup), the last row is the terminal
    // START button, rows 1-2 (Type, Target) are value rows. This also gains the '<'/'>' edit
    // aliases the canonical model defines, which this screen lacked before (additive, ADR-0092).
    if (m_state == State::IDLE) {
        const int sel = m_configList.selectedIndex();
        const ui::ConfigRowKind kind =
            (sel == 0)                      ? ui::ConfigRowKind::Popup :
            (sel == IDLE_OPTION_COUNT - 1)  ? ui::ConfigRowKind::Terminal :
                                              ui::ConfigRowKind::Value;
        switch (ui::classifyConfigKey(key, kind)) {
            case ui::ConfigKeyAction::NavUp:
                m_configList.setItemCount(IDLE_OPTION_COUNT);
                m_configList.moveUp();  // wraps to last; model keeps the selection on screen
                return true;
            case ui::ConfigKeyAction::NavDown:
                m_configList.setItemCount(IDLE_OPTION_COUNT);
                m_configList.moveDown();  // wraps to first; model keeps the selection on screen
                return true;
            case ui::ConfigKeyAction::EditPrev:
                if (sel == 1) { // HID class
                    m_hidClass = prevHidClass(m_hidClass);
                } else if (sel == 2) { // OS
                    int os = (int)m_targetOs - 1;
                    if (os < 0) os = 2;
                    m_targetOs = (TargetOS)os;
                }
                return true;
            case ui::ConfigKeyAction::EditNext:  // also ENTER on a value row (never inert, ADR-0092)
                if (sel == 1) { // HID class
                    m_hidClass = nextHidClass(m_hidClass);
                } else if (sel == 2) { // OS
                    int os = (int)m_targetOs + 1;
                    if (os > 2) os = 0;
                    m_targetOs = (TargetOS)os;
                }
                return true;
            case ui::ConfigKeyAction::Activate:
                startAdvertising();  // ENTER on the terminal START row
                return true;
            case ui::ConfigKeyAction::Popup:  // Name row: open the session-name popup
                m_namePopup.show("Session Name", m_sessionName, false, 31);
                m_namePopup.setOnSubmit([this](const char* text) {
                    strncpy(m_sessionName, text, 32);
                    m_sessionName[32] = '\0';
                    m_needsRedraw = true;
                });
                m_namePopup.setOnCancel([this]() {
                    m_needsRedraw = true;
                });
                return true;
            case ui::ConfigKeyAction::Navigate:
            case ui::ConfigKeyAction::None:
                break;  // no navigation row; Back ('`') handled just below
        }
        if (key == '`') { // BACK
            m_shouldExit = true;
            return true;
        }
        return false;
    }

    // SCRIPT_SELECT — browse SD scripts
    if (m_state == State::SCRIPT_SELECT) {
        int total = (int)m_bleScripts.size();
        switch (key) {
            case ';':
                if (total > 0) { m_bleScriptList.setItemCount(total); m_bleScriptList.moveUp(); }
                return true;
            case '.':
                if (total > 0) { m_bleScriptList.setItemCount(total); m_bleScriptList.moveDown(); }
                return true;
            case '\n':
                if (total > 0) runBleScript(m_bleScriptList.selectedIndex());
                return true;
            case '`':
                m_state = State::CONNECTED;
                m_needsRedraw = true;
                footerHints_.setHints({{'\n', "Inject", true}, {'`', "Back", true}});
                return true;
        }
        return true;
    }

    // SCRIPT_RUNNING — abort only
    if (m_state == State::SCRIPT_RUNNING) {
        if (key == '`') m_bleInterp.requestAbort();
        return true;
    }

    // SCRIPT_DONE — back to script list
    if (m_state == State::SCRIPT_DONE) {
        if (key == '`') {
            m_state = State::SCRIPT_SELECT;
            footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
            m_needsRedraw = true;
        }
        return true;
    }

    // CONNECTED MENU HANDLING
    // Footer Focus Dispatch
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction) return handleInput(footerAction);
        m_needsRedraw = true;
        return true;
    }

    switch (key) {
        case ';': // UP
        case 'k':
            if (m_state == State::CONNECTED) {
                m_actionList.setItemCount(connectedActionCount());
                m_actionList.moveUp();
            }
            return true;
        case '.': // DOWN
        case 'j':
            if (m_state == State::CONNECTED) {
                m_actionList.setItemCount(connectedActionCount());
                m_actionList.moveDown();
            }
            return true;
        case '\n':
            if (m_state == State::CONNECTED) injectCommand();
            return true;
        case '`':
            stopAdvertising(); // Go back to config instead of exit
            m_state = State::IDLE;
            footerHints_.setHints({
                {'\n', "Select", true},
                {'`', "Back", true}
            });
            m_needsRedraw = true;
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// DuckyScript over BLE — script select / running / done screens
// ---------------------------------------------------------------------------

void BleBadBleScreen::drawScriptSelect(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "DUCKY SCRIPT");

    if (m_bleScripts.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setTextDatum(top_center);
        int cx = config::SCREEN_WIDTH / 2;
        canvas.drawString("No scripts on SD.", cx, 42);
        canvas.drawString("Copy .txt files to:", cx, 60);
        canvas.drawString(config::SD_BADUSB_PATH, cx, 78);
        canvas.setTextDatum(top_left);
        return;
    }

    // SCRIPT_SELECT does not wire footer-focus input (ledgered gap, see slice-0066), so the band
    // stays always-focused; the component paints band + thumb and the callback draws content only
    // (slice-0063/0066).
    const int16_t listTop = 22;
    const int16_t listH   = (int16_t)(config::SCREEN_HEIGHT - ui::FOOTER_HEIGHT - listTop);
    m_bleScriptList.setItemCount((int)m_bleScripts.size());
    ScrollableList::render(canvas, m_bleScriptList, 0, listTop, config::SCREEN_WIDTH, listH,
        theme::LIST_ROW_HEIGHT, [this](Canvas& c, int idx, int16_t rowY, bool /*sel*/) {
            c.setTextColor(theme::TEXT_PRIMARY());
            c.setCursor(10, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(m_bleScripts[idx].name);
        },
        true);
}

void BleBadBleScreen::drawScriptRunning(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "RUNNING");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::TEXT_PRIMARY());
    if (m_bleScriptName)
        canvas.drawString(m_bleScriptName, cx, 32);

    canvas.setTextColor(theme::TEXT_SECONDARY());
    char lineBuf[32];
    snprintf(lineBuf, sizeof(lineBuf), "Line %d...", m_bleInterp.currentLine());
    canvas.drawString(lineBuf, cx, 55);

    int dots = (millis() / 400) % 4;
    char anim[8] = "   ";
    for (int i = 0; i < dots; i++) anim[i] = '.';
    canvas.drawString(anim, cx, 73);

    canvas.drawString("ESC to abort", cx, 100);
    canvas.setTextDatum(top_left);
}

void BleBadBleScreen::drawScriptDone(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "DONE");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);
    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("Script complete.", cx, 45);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("Press Back", cx, 70);
    canvas.setTextDatum(top_left);
}

void BleBadBleScreen::enterScriptSelect() {
    loadBleScripts();
    m_bleScriptList.setItemCount((int)m_bleScripts.size());
    m_bleScriptList.setSelectedIndex(0);
    m_state = State::SCRIPT_SELECT;
    footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
    m_needsRedraw = true;
}

void BleBadBleScreen::loadBleScripts() {
    m_bleScripts.clear();
#ifdef ESP32
    if (!SD.exists(config::SD_BADUSB_PATH)) {
        SD.mkdir(config::SD_BADUSB_PATH);
        return;
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
                parseBleScriptName(se.sdPath, se.name, sizeof(se.name));
                if (se.name[0] == '\0') {
                    strncpy(se.name, fname, sizeof(se.name) - 1);
                    char* dot = strrchr(se.name, '.');
                    if (dot) *dot = '\0';
                }
                m_bleScripts.push_back(se);
            }
        }
        entry.close();
    }
    dir.close();
#endif
}

void BleBadBleScreen::runBleScript(int index) {
    if (index < 0 || index >= (int)m_bleScripts.size()) return;

    freeBleScriptBuf();
    m_bleScriptBuf = new (std::nothrow) char[8192];
    if (!m_bleScriptBuf) return;
    m_bleScriptBuf[0] = '\0';

    File f = SD.open(m_bleScripts[index].sdPath, FILE_READ);
    if (!f) { freeBleScriptBuf(); return; }
    size_t n = f.readBytes(m_bleScriptBuf, 8191);
    m_bleScriptBuf[n] = '\0';
    f.close();

    m_bleScriptName = m_bleScripts[index].name;
    m_bleInterp.load(m_bleScriptBuf);
    m_state = State::SCRIPT_RUNNING;
    m_needsRedraw = true;
    footerHints_.setHints({{'`', "Stop", true}});
}

void BleBadBleScreen::freeBleScriptBuf() {
    delete[] m_bleScriptBuf;
    m_bleScriptBuf = nullptr;
}

void BleBadBleScreen::parseBleScriptName(const char* sdPath, char* nameBuf, size_t bufSize) {
    nameBuf[0] = '\0';
#ifdef ESP32
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return;

    char line[128];
    int  pos = 0;
    while (f.available() && pos < (int)sizeof(line) - 1) {
        char c = (char)f.read();
        if (c == '\n' || c == '\r') break;
        line[pos++] = c;
    }
    f.close();

    line[pos] = '\0';
    while (pos > 0 && (line[pos-1] == ' ' || line[pos-1] == '\r' || line[pos-1] == '\t'))
        line[--pos] = '\0';

    if (strncasecmp(line, "REM ", 4) == 0 && pos > 4)
        strncpy(nameBuf, line + 4, bufSize - 1);
#endif
}

} // namespace adversary
