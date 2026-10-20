/**
 * @file ble_spam_screen.cpp
 * @brief BleSpamScreen implementation
 */

#include "ui/screens/ble_spam_screen.h"
#include "ui/theme.h"
#include "config/config.h"
#include "ui/components/toast_manager.h"
#include "ui/components/action_button.h"
#include "modules/system/system_manager.h"
#include "modules/ble/ble_scanner.h"
#include "modules/wifi/wifi_scanner.h"
#include "assets/generated_icons.h"
#include "utils/bitmap_remapper.h"
#include "utils/log.h"

namespace adversary {

BleSpamScreen::BleSpamScreen()
    : m_state(State::CONFIG)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_pulseStartTime(0) {
}

void BleSpamScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_state = State::CONFIG;
    m_configList.setItemCount(0);  // Reset selection + scroll to the top
    m_config = BleSpamConfig(); // Reset to defaults
    
    footerHints_.setHints({
        {';', "Nav"},
        {'.', "Nav"},
        {',', "Edit"},
        {'\r', "Go"}
    });
}

void BleSpamScreen::hide() {
    stopAttack();
    m_active = false;
    BLESpanner::getInstance().forceRelease();
    BLEScanner::getInstance().deinit();
}

void BleSpamScreen::update() {
    if (m_state == State::ATTACK) {
        BLESpanner::getInstance().update();
        m_needsRedraw = true; // Refresh telemetry in real-time
    }
}

void BleSpamScreen::render(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    
    if (m_state == State::CONFIG) {
        m_statusBar.render(canvas, "BLE Spam Config");
        drawConfig(canvas);
    } else {
        m_statusBar.render(canvas, "BLE SPAM", "ATTACK", theme::BG_SECONDARY(), theme::ACCENT());
        drawAttack(canvas);
    }

    footerHints_.render(canvas);
}

void BleSpamScreen::drawConfig(Canvas& canvas) {
    const int16_t footerHeight = ui::FOOTER_HEIGHT;
    const int16_t availableHeight = config::SCREEN_HEIGHT - FIRST_LINE_Y - footerHeight - 4;

    const char* optionLabels[] = {"Provider:", "Prompt:", "Intensity:"};
    const char* providers[] = {"Apple", "Android", "Windows", "Samsung", "Carousel"};
    const char* prompts[] = {"Pairing", "Action", "Battery", "Link"};
    char intStr[8];
    sprintf(intStr, "%dx", m_config.intensity);

    // The shared component derives the visible-row count from the list rect, keeps the selection
    // on screen, and draws the unified thumb in place of the old ^/v markers; this callback paints
    // one config row.
    m_configList.setItemCount(CONFIG_OPTION_COUNT);
    ScrollableList::render(canvas, m_configList, 0, FIRST_LINE_Y, canvas.width(), availableHeight,
        theme::LIST_ROW_HEIGHT, [this, &optionLabels, &providers, &prompts, &intStr](Canvas& c, int idx, int16_t rowY, bool selected) {
            if (idx == CONFIG_OPTION_COUNT - 1) { // Terminal start row: shared green ActionButton (slice-0089)
                ui::ActionButton::render(c, rowY, "START SPAM", selected);
                return;
            }
            // Config field: selected field reddens to ACCENT (the edit cue); band drawn by the
            // component (BG_SELECTED, dimmed on footer focus via the flag below) (slice-0063).
            c.setTextColor(selected ? theme::ACCENT() : theme::TEXT_PRIMARY());
            c.setCursor(MARGIN_LEFT, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(optionLabels[idx]);

            const char* val = nullptr;
            if (idx == 0) val = providers[(int)m_config.provider];
            else if (idx == 1) val = prompts[(int)m_config.prompt];
            else if (idx == 2) val = intStr;
            if (val) {
                c.setCursor(80, rowY + theme::LIST_ROW_TEXT_DY);
                c.print(val);
            }
        },
        !footerHints_.hasFocus());
}

void BleSpamScreen::drawAttack(Canvas& canvas) {
    int16_t screenWidth = canvas.width();
    int16_t y = FIRST_LINE_Y;
    
    // 1. Target Section (Autohunt style)
    const char* providers[] = {"Apple", "Android", "Windows", "Samsung", "Carousel"};
    const char* prompts[] = {"Pairing", "Action", "Battery", "Link"};
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Target: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(providers[(int)m_config.provider]);
    canvas.printf(" (%s)", prompts[(int)m_config.prompt]);
    
    // 2. Central Activity Pulse with BLE Icon (Re-centered)
    // Vertically centered between target line (38) and telemetry line (110)
    int centerX = screenWidth / 2;
    int centerY = 74; 
    
    // Pulsing rings (Handshake autohunt style)
    uint32_t elapsed = millis() - m_pulseStartTime;
    float pulse = (float)(elapsed % 1200) / 1200.0f;
    
    // Draw two expanding rings
    for (int i = 0; i < 2; i++) {
        float p = pulse + (i * 0.5f);
        if (p > 1.0f) p -= 1.0f;
        int radius = 18 + (int)(p * 15.0f);
        uint16_t color = theme::ACCENT();
        canvas.drawCircle(centerX, centerY, radius, color);
    }
    
    // Draw the BLE icon in the center
    int iconSize = 28; // approx scale 0.6
    utils::BitmapRemapper::drawThemeIcon(canvas, adversary::assets::ICON_BLE, centerX - iconSize/2, centerY - iconSize/2, 48, 48, 0.6f);
    
    // 3. Telemetry Line (Single line at the bottom, no background)
    int telY = 110;
    auto& spanner = BLESpanner::getInstance();
    
    // Pps
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, telY);
    canvas.print("Pps: ");
    canvas.setTextColor(theme::ACCENT());
    canvas.print(spanner.getPPS(), 1);

    // Intr
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(85, telY);
    canvas.print("Intr: ");
    canvas.setTextColor(theme::WARNING());
    canvas.print(spanner.getInteractionCount());

    // Total
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(155, telY);
    canvas.print("Total: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(spanner.getPacketCount());

    // Activity indicator dot
    if ((millis() / 500) % 2 == 0) {
        canvas.fillCircle(225, telY + 4, 3, theme::ACCENT());
    }
}

bool BleSpamScreen::handleInput(char key) {
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    if (m_state == State::CONFIG) {
        switch (key) {
            case ';': // Nav UP
                m_configList.setItemCount(CONFIG_OPTION_COUNT);
                m_configList.moveUp();  // wraps to last; model keeps the selection on screen
                m_needsRedraw = true;
                return true;
            case '.': // Nav DOWN
                m_configList.setItemCount(CONFIG_OPTION_COUNT);
                m_configList.moveDown();  // wraps to first; model keeps the selection on screen
                m_needsRedraw = true;
                return true;
            case ',': // Toggle/Edit PREV
            case '<':
                if (m_configList.selectedIndex() == 0) {
                    m_config.provider = (BleSpamProvider)((int)m_config.provider == 0 ? 4 : (int)m_config.provider - 1);
                } else if (m_configList.selectedIndex() == 1) {
                    m_config.prompt = (BleSpamPrompt)((int)m_config.prompt == 0 ? 3 : (int)m_config.prompt - 1);
                } else if (m_configList.selectedIndex() == 2) {
                    if (m_config.intensity > 1) m_config.intensity--;
                }
                m_needsRedraw = true;
                return true;
            case '/': // Toggle/Edit NEXT
            case '>':
                if (m_configList.selectedIndex() == 0) {
                    m_config.provider = (BleSpamProvider)(((int)m_config.provider + 1) % 5);
                } else if (m_configList.selectedIndex() == 1) {
                    m_config.prompt = (BleSpamPrompt)(((int)m_config.prompt + 1) % 4);
                } else if (m_configList.selectedIndex() == 2) {
                    if (m_config.intensity < 10) m_config.intensity++;
                }
                m_needsRedraw = true;
                return true;
            case '\n':
            case '\r':
                if (m_configList.selectedIndex() == CONFIG_OPTION_COUNT - 1) {
                    startAttack();
                } else {
                    // Logic to toggle next if applicable
                    return handleInput('/'); 
                }
                return true;
            case '`': // ESC
            case 27:
                m_shouldExit = true;
                return true;
        }
    } else {
        // Attack state
        if (key == '`' || key == 27) { // ESC/Stop
            stopAttack();
            return true;
        }
    }
    return false;
}

void BleSpamScreen::startAttack() {
    LOG_D("BLESPAM", "startAttack() called");
    // 1. Prepare memory
    SystemManager::getInstance().prepareForMemoryIntensiveTask(false); // Don't purge canvas
    
    m_state = State::ATTACK;
    m_pulseStartTime = millis();
    LOG_I("BLESPAM", "Starting BLESpanner...");
    BLESpanner::getInstance().start(m_config);
    
    footerHints_.setHints({
        {'`', "Stop", true}
    });
    
    m_needsRedraw = true;
    ToastManager::getInstance().show("Attack Started", ToastType::INFO);
}

void BleSpamScreen::stopAttack() {
    BLESpanner::getInstance().stop();
    m_state = State::CONFIG;
    
    // Restore memory/WiFi
    SystemManager::getInstance().restoreFromMemoryIntensiveTask();
    
    footerHints_.setHints({
        {';', "Prev", true},
        {'.', "Next", true},
        {'\n', "Toggle", true},
        {'d', "Start", true}
    });
    
    m_needsRedraw = true;
    ToastManager::getInstance().show("Attack Stopped", ToastType::WARNING);
}

} // namespace adversary
