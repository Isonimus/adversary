/**
 * @file ble_spoof_screen.cpp
 * @brief BleSpoofScreen implementation
 */

#include "ble_spoof_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "modules/ble/ble_utils.h"

namespace adversary {

BleSpoofScreen::BleSpoofScreen()
    : m_active(false)
    , m_spoofing(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
{
}

void BleSpoofScreen::setTarget(const BLEDeviceInfo& device) {
    m_target = device;
}

void BleSpoofScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_spoofing = false;
    
    footerHints_.setHints({
        {'\n', "Start Spoof", true},
        {'`', "Back", true}
    });
}

void BleSpoofScreen::hide() {
    if (m_spoofing) BLESpanner::getInstance().stop();
    m_active = false;
    BLESpanner::getInstance().forceRelease();
    BLEScanner::getInstance().deinit();
}

void BleSpoofScreen::update() {
    if (!m_active) return;
    
    if (m_spoofing && millis() - m_lastUpdate > 1000) {
        m_lastUpdate = millis();
        m_needsRedraw = true;
    }
}

void BleSpoofScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;
    m_needsRedraw = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "BLE SPOOF", m_spoofing ? "RUNNING" : "READY");
    
    drawStatus(canvas);
    footerHints_.render(canvas);
}

void BleSpoofScreen::drawStatus(Canvas& canvas) {
    int y = 35;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.print("Active Persona:");
    y += 15;
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(15, y);
    canvas.print(m_target.name.empty() ? "Unlabeled Device" : m_target.name.c_str());
    y += 12;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(15, y);
    canvas.print(ble::addressToString(m_target.address).c_str());
    y += 15;
    
    if (m_spoofing) {
        canvas.setTextColor(theme::SUCCESS());
        canvas.setCursor(10, y);
        canvas.print("Transmission active...");
        
        int barW = (millis() / 50) % 100;
        canvas.fillRect(10, y + 10, barW, 2, theme::SUCCESS());
        
        y += 20;
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, y);
        canvas.printf("Pkts: %u", (unsigned int)BLESpanner::getInstance().getPacketCount());
    } else {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(10, y);
        canvas.print("Press ENTER to clone identity.");
    }
}

bool BleSpoofScreen::handleInput(char key) {
    // Footer Focus Dispatch
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction) return handleInput(footerAction);
        m_needsRedraw = true;
        return true;
    }

    switch (key) {
        case '\n':
            if (!m_spoofing) {
                m_spoofing = true;
                
                BleSpamConfig cfg;
                cfg.provider = BleSpamProvider::CLONE;
                cfg.prompt = BleSpamPrompt::PAIRING;
                cfg.intensity = 10;
                
                auto& spanner = BLESpanner::getInstance();
                spanner.setSpoofTarget(m_target);
                spanner.start(cfg);
                
                footerHints_.setHints({
                    {'\n', "Stop Spoof", true},
                    {'`', "Back", true}
                });
            } else {
                m_spoofing = false;
                BLESpanner::getInstance().stop();
                footerHints_.setHints({
                    {'\n', "Start Spoof", true},
                    {'`', "Back", true}
                });
            }
            m_needsRedraw = true;
            return true;
        case '`':
            m_shouldExit = true;
            return true;
    }
    return false;
}

} // namespace adversary
