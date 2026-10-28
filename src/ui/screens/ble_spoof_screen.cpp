/**
 * @file ble_spoof_screen.cpp
 * @brief BleSpoofScreen implementation
 */

#include "ble_spoof_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/screen_manager.h"  // cold launch with no target routes to the BLE scanner (ADR-0092 idiom)
#include "modules/ble/ble_utils.h"

namespace adversary {

BleSpoofScreen::BleSpoofScreen()
    : m_haveTarget(false)
    , m_active(false)
    , m_spoofing(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
{
}

void BleSpoofScreen::setTarget(const BLEDeviceInfo& device) {
    m_target = device;
    m_haveTarget = true;  // the single assignment point: both setParams and direct callers pass here
}

void BleSpoofScreen::setParams(const ScreenParams& params) {
    // Rebuild a minimal clone target from the params. NimBLEAddress(uint8_t[6]) reverse-copies
    // its input, so feed it the reversed bytes to get an address whose getNative() matches the
    // original device's (the order the scanner copied into params.bssid). name must be
    // non-empty: it is what gates and labels the CLONE advertisement (ble_spanner.cpp).
    uint8_t rev[6];
    for (int i = 0; i < 6; ++i) rev[i] = params.bssid[5 - i];
    BLEDeviceInfo device{};
    device.address = NimBLEAddress(rev);
    device.name = params.ssid;
    setTarget(device);
}

void BleSpoofScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_spoofing = false;

    // Without a target (cold launch from the BLE menu) ENTER cannot start a clone, so it offers
    // to go pick one instead: the (select from scanner) navigation idiom (ADR-0092).
    footerHints_.setHints({
        {'\n', m_haveTarget ? "Start Spoof" : "Select Target", true},
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

    // Cold launch: no device was injected, so there is no persona to clone. Show the scanner-target
    // placeholder instead of a fake all-zero persona, and stop here (ENTER routes to the scanner).
    if (!m_haveTarget) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, y);
        canvas.print("Active Persona:");
        y += 15;

        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(15, y);
        canvas.print("(select from scanner)");
        y += 20;

        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(10, y);
        canvas.print("Pick a device in the BLE Scanner");
        y += 12;
        canvas.setCursor(10, y);
        canvas.print("to clone its identity.");
        return;
    }

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
            if (!m_haveTarget) {
                // Nothing to clone. Hand off to the BLE scanner to pick a device, which will route
                // back here with a target via BLE_SPOOF_TARGET_SELECTED. Done through the deferred
                // exit breadcrumb (consumed after this input unwinds in exitActiveScreen()), never a
                // mid-input setActiveScreen(), which would delete this factory-owned screen under us.
                ScreenManager::getInstance().setReturnTarget(ScreenId::BLE_SCANNER, AppState::SCANNING);
                m_shouldExit = true;
                return true;
            }
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
