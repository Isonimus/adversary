/**
 * @file ble_bad_ble_screen.cpp
 * @brief BleBadBleScreen implementation
 */

#include "ble_bad_ble_screen.h"
#include "modules/ble/ble_hid_keyboard.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "modules/storage/settings_manager.h"
#include "modules/ble/ble_scanner.h"
#include "config/config.h"
#include <SD.h>

namespace adversary {

BleBadBleScreen::BleBadBleScreen()
    : m_state(State::IDLE)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_actionSelection(0)
    , m_scrollOffset(0)
    , m_targetOs(TargetOS::LINUX)
    , m_customInputBuf{0}
    , m_customInputLen(0)
    , m_bleScriptSel(0)
    , m_bleScriptOffset(0)
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
    m_actionSelection = 0; // For Connected Menu
    m_menuSelection = 0;   // For Idle Config
    
    // Load Defaults for Session
    auto& settings = SettingsManager::getInstance().get();
    m_targetOs = TargetOS::LINUX; // Default
    strncpy(m_sessionName, settings.wireless.bleName, 32);
    m_sessionName[32] = '\0';
    if (m_sessionName[0] == '\0') strcpy(m_sessionName, "Universal Key");
    m_appearance = settings.wireless.bleAppearance;
    if (m_appearance == 0) m_appearance = 0x03C1;
    
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
    
    int y = 35;
    int lineHeight = 20;
    
    const char* labels[] = {
        "Name:",
        "Type:",
        "Target:",
        "[ START ATTACK ]"
    };
    
    for (int i = 0; i < 4; i++) {
        if (i == m_menuSelection) {
            canvas.fillRect(0, y - 2, config::SCREEN_WIDTH, lineHeight, theme::BG_SECONDARY());
            canvas.setTextColor(theme::ACCENT());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        canvas.setCursor(10, y + 2);
        
        if (i == 3) { // Start Button
            canvas.setTextDatum(top_center);
            canvas.drawString(labels[i], config::SCREEN_WIDTH / 2, y + 2);
            canvas.setTextDatum(top_left);
        } else {
            canvas.print(labels[i]);
            
            // Draw Value
            canvas.setCursor(80, y + 2);
            if (i == m_menuSelection) canvas.setTextColor(theme::TEXT_PRIMARY());
            else canvas.setTextColor(theme::TEXT_SECONDARY());
            
            switch(i) {
                case 0: // Name
                    canvas.print(m_sessionName);
                    break;
                case 1: // Type
                    switch(m_appearance) {
                        case 0x03C1: canvas.print("< Keyboard >"); break;
                        case 0x03C2: canvas.print("< Mouse >"); break;
                        case 0x03C3: canvas.print("< Joystick >"); break;
                        case 0x03C4: canvas.print("< Gamepad >"); break;
                        default: canvas.print("< Unknown >"); break;
                    }
                    break;
                case 2: // Target
                    switch(m_targetOs) {
                        case TargetOS::LINUX:   canvas.print("< LINUX >"); break;
                        case TargetOS::WINDOWS: canvas.print("< WINDOWS >"); break;
                        case TargetOS::MACOS:   canvas.print("< MACOS >"); break;
                    }
                    break;
            }
        }
        y += lineHeight;
    }
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
    canvas.drawString("Target: Linux / Universal", centerX, y + 15);
    canvas.setTextDatum(top_left); // Reset
}

void BleBadBleScreen::drawConnected(Canvas& canvas) {
    ui::StatusBar::render(canvas, "BadBLE", "CONNECTED [!]");
    
    // Header Info - REMOVED (Redundant with StatusBar)
    // Compact Layout - List starts immediately after StatusBar (20px + 2px padding for rect)
    // Text y = 22 (rect top) + 5 (padding) = 27
    int y = 27;
    
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

    const char** currentActions = nullptr;
    int totalItems = 12;
    
    switch (m_targetOs) {
        case TargetOS::WINDOWS: currentActions = actions_win; break;
        case TargetOS::LINUX:   currentActions = actions_lin; break;
        case TargetOS::MACOS:   currentActions = actions_mac; break;
    }
    
    // Scrolling Logic
    const int VISIBLE_ROWS = 5; // Standard Menu items (18px) fit 5 rows (90px)
    const int ROW_HEIGHT = 18;  // Match Menu standard
    
    int startIdx = m_scrollOffset;
    int endIdx = startIdx + VISIBLE_ROWS;
    if (endIdx > totalItems) endIdx = totalItems;
    
    // Render List
    for (int i = startIdx; i < endIdx; i++) {
        bool selected = (m_actionSelection == i);
        if (selected) {
            // Check if footer has focus - if so, dim the selection
            if (footerHints_.hasFocus()) {
                 canvas.fillRect(0, y - 5, config::SCREEN_WIDTH, ROW_HEIGHT, theme::BG_SECONDARY());
                 canvas.setTextColor(theme::TEXT_PRIMARY());
            } else {
                 canvas.fillRect(0, y - 5, config::SCREEN_WIDTH, ROW_HEIGHT, theme::BG_SELECTED());
                 canvas.setTextColor(theme::TEXT_INVERSE());
            }
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        canvas.setCursor(15, y);
        canvas.print(currentActions[i]);
        y += ROW_HEIGHT;
    }
    
    // Draw Scrollbar (from CapturesScreen pattern)
    if (totalItems > VISIBLE_ROWS) {
        int16_t barX = config::SCREEN_WIDTH - 5;
        int16_t barY = 22; // Matches list rect start (27 - 5)
        int16_t barHeight = VISIBLE_ROWS * ROW_HEIGHT; // 90px
        
        // Track
        canvas.fillRect(barX, barY, 4, barHeight, theme::BG_TERTIARY());
        
        // Thumb
        int16_t thumbHeight = (VISIBLE_ROWS * barHeight) / totalItems;
        if (thumbHeight < 6) thumbHeight = 6;
        
        int maxScroll = totalItems - VISIBLE_ROWS;
        int16_t thumbY = barY + (m_scrollOffset * (barHeight - thumbHeight)) / maxScroll;
        
        canvas.fillRect(barX, thumbY, 4, thumbHeight, theme::ACCENT());
    }
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
    cfg.provider = BleSpamProvider::HID_KEYBOARD;
    cfg.intensity = 10;
    
    // GAP Name handled by BLESpanner via config
    // NimBLEDevice::setDeviceName("Universal Key"); 
    
    cfg.name[0] = '\0';
    if (m_sessionName[0] != '\0') strncpy(cfg.name, m_sessionName, 32);
    cfg.appearance = m_appearance;
    
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
    auto& spanner = BLESpanner::getInstance();
    
    // Re-focus helper (Alt+Tab / Cmd+Tab logic could be added here if needed)
    // For now, assume user has focus.
    
    switch (m_targetOs) {
        case TargetOS::WINDOWS:
            switch (m_actionSelection) {
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
                     loadBleScripts();
                     m_bleScriptSel = 0;
                     m_bleScriptOffset = 0;
                     m_state = State::SCRIPT_SELECT;
                     footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
                     m_needsRedraw = true;
                     break;
            }
            break;

        case TargetOS::LINUX:
            switch (m_actionSelection) {
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
                     loadBleScripts();
                     m_bleScriptSel = 0;
                     m_bleScriptOffset = 0;
                     m_state = State::SCRIPT_SELECT;
                     footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
                     m_needsRedraw = true;
                     break;
            }
            break;

        case TargetOS::MACOS:
            switch (m_actionSelection) {
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
                     loadBleScripts();
                     m_bleScriptSel = 0;
                     m_bleScriptOffset = 0;
                     m_state = State::SCRIPT_SELECT;
                     footerHints_.setHints({{'\n', "Run", true}, {'`', "Back", true}});
                     m_needsRedraw = true;
                     break;
            }
            break;
    }
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

    // IDLE MENU HANDLING
    if (m_state == State::IDLE) {
        switch (key) {
            case ';': // UP
                if (m_menuSelection > 0) m_menuSelection--;
                return true;
            case '.': // DOWN
                if (m_menuSelection < 3) m_menuSelection++;
                return true;
            case ',': // LEFT
                if (m_menuSelection == 1) { // Appearance
                    if (m_appearance == 0x03C1) m_appearance = 0x03C4; // Key -> Gamepad
                    else if (m_appearance == 0x03C4) m_appearance = 0x03C3;
                    else if (m_appearance == 0x03C3) m_appearance = 0x03C2;
                    else if (m_appearance == 0x03C2) m_appearance = 0x03C1;
                } else if (m_menuSelection == 2) { // OS
                    int os = (int)m_targetOs - 1;
                    if (os < 0) os = 2;
                    m_targetOs = (TargetOS)os;
                }
                return true;
            case '/': // RIGHT
                if (m_menuSelection == 1) { // Appearance
                    if (m_appearance == 0x03C1) m_appearance = 0x03C2; // Key -> Mouse
                    else if (m_appearance == 0x03C2) m_appearance = 0x03C3;
                    else if (m_appearance == 0x03C3) m_appearance = 0x03C4;
                    else if (m_appearance == 0x03C4) m_appearance = 0x03C1;
                } else if (m_menuSelection == 2) { // OS
                    int os = (int)m_targetOs + 1;
                    if (os > 2) os = 0;
                    m_targetOs = (TargetOS)os;
                }
                return true;
            case '\n': // ENTER
                if (m_menuSelection == 0) { // Name
                    m_namePopup.show("Session Name", m_sessionName, false, 31);
                    m_namePopup.setOnSubmit([this](const char* text) {
                        strncpy(m_sessionName, text, 32);
                        m_sessionName[32] = '\0';
                        m_needsRedraw = true;
                    });
                    m_namePopup.setOnCancel([this]() {
                        m_needsRedraw = true; 
                    });
                } else if (m_menuSelection == 3) { // Start
                    startAdvertising();
                }
                return true;
            case '`': // BACK
                m_shouldExit = true;
                return true;
        }
        return false;
    }

    // SCRIPT_SELECT — browse SD scripts
    if (m_state == State::SCRIPT_SELECT) {
        int total = (int)m_bleScripts.size();
        const int VISIBLE = 5;
        switch (key) {
            case ';':
                if (total > 0 && m_bleScriptSel > 0) {
                    m_bleScriptSel--;
                    if (m_bleScriptSel < m_bleScriptOffset)
                        m_bleScriptOffset = m_bleScriptSel;
                }
                return true;
            case '.':
                if (total > 0 && m_bleScriptSel < total - 1) {
                    m_bleScriptSel++;
                    if (m_bleScriptSel >= m_bleScriptOffset + VISIBLE)
                        m_bleScriptOffset = m_bleScriptSel - VISIBLE + 1;
                }
                return true;
            case '\n':
                if (total > 0) runBleScript(m_bleScriptSel);
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
            if (m_state == State::CONNECTED && m_actionSelection > 0) {
                m_actionSelection--;
                // Scroll Up Logic
                if (m_actionSelection < m_scrollOffset) {
                    m_scrollOffset = m_actionSelection;
                }
            }
            return true;
        case '.': // DOWN
        case 'j':
             // Max items = 12 (indices 0-11)
            if (m_state == State::CONNECTED && m_actionSelection < 11) {
                m_actionSelection++;
                // Scroll Down Logic
                const int VISIBLE_ROWS = 5; // Must match drawConnected
                if (m_actionSelection >= m_scrollOffset + VISIBLE_ROWS) {
                    m_scrollOffset = m_actionSelection - VISIBLE_ROWS + 1;
                }
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

    const int VISIBLE = 5;
    const int ROW_H   = 18;
    int total = (int)m_bleScripts.size();
    int y     = 22;

    int start = m_bleScriptOffset;
    int end   = start + VISIBLE;
    if (end > total) end = total;

    for (int i = start; i < end; i++) {
        bool sel = (i == m_bleScriptSel);
        if (sel) {
            canvas.fillRect(0, y, config::SCREEN_WIDTH, ROW_H, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_INVERSE());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        canvas.setCursor(10, y + 3);
        canvas.print(m_bleScripts[i].name);
        y += ROW_H;
    }

    if (total > VISIBLE) {
        int barH   = VISIBLE * ROW_H;
        int barX   = config::SCREEN_WIDTH - 5;
        int barY   = 22;
        canvas.fillRect(barX, barY, 4, barH, theme::BG_TERTIARY());
        int thumbH = (VISIBLE * barH) / total;
        if (thumbH < 6) thumbH = 6;
        int maxScr = total - VISIBLE;
        int thumbY = barY + (m_bleScriptOffset * (barH - thumbH)) / maxScr;
        canvas.fillRect(barX, thumbY, 4, thumbH, theme::ACCENT());
    }
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
    footerHints_.setHints({{'`', "Abort", true}});
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
