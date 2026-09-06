/**
 * @file ble_scanner_screen.cpp
 * @brief BleScannerScreen implementation
 */

#include "ble_scanner_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "modules/ble/ble_utils.h"
#include "modules/system/system_manager.h"

#include "ui/screens/ble_apple_attack_screen.h"
#include "ui/screens/ble_spoof_screen.h"
#include "ui/screen_manager.h"
#include <algorithm>

namespace adversary {

BleScannerScreen::BleScannerScreen()
    : m_scanner(BLEScanner::getInstance())
    , m_selectedIndex(0)
    , m_scrollOffset(0)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_isInitializing(false)
    , m_pendingInit(0)
    , m_lastUpdate(0) {
}

BleScannerScreen::~BleScannerScreen() {
}

void BleScannerScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_selectedIndex = 0;
    m_scrollOffset = 0;
    
    // Initialize footer hints
    footerHints_.setHints({
        {'\n', "Target", true},
        {'c', "Clear", true},
        {'`', "Back", true}
    });
    footerHints_.setFocus(false);
    
    // Aggressively free memory for BLE session ONLY if we aren't already utilizing it
    // This avoids a destructive deinit/init cycle when returning from another BLE screen
    if (!BLEScanner::isInitialized()) {
        SystemManager::getInstance().prepareForMemoryIntensiveTask(false);
    }
    
    // Defer initialization to update() to flatten call stack and avoid WindowOverflow
    m_isInitializing = true;
    m_pendingInit = 2; // Countdown: 2 -> 1 (render) -> 0 (init)
    
    Serial.println("[UI] BleScannerScreen visible, pending init...");
}

void BleScannerScreen::hide() {
    m_active = false;
    m_scanner.stopScan();

    // Release the BLEScanner singleton's device list (each entry holds two heap
    // std::strings). clearDevices() clear()+shrink_to_fit()s WITHOUT tearing down
    // NimBLE, so it frees the scan results but keeps the BLE session intact —
    // respecting the "don't do a destructive deinit/init cycle between BLE
    // screens" optimization in show(). The screen's own m_deviceList copy is
    // freed by the destructor (factory-owned screen).
    m_scanner.clearDevices();

    // Lazy restore system state (WiFi, etc.) after BLE session
    SystemManager::getInstance().restoreFromMemoryIntensiveTask();

    Serial.println("[UI] BleScannerScreen hidden");
}

void BleScannerScreen::update() {
    if (!m_active) return;

    // Handle deferred initialization
    if (m_isInitializing && m_pendingInit > 0) {
        if (--m_pendingInit == 0) {
            m_scanner.init();
            m_isInitializing = false;
            m_scanner.startScan(0); // Start scan after init
        }
        m_needsRedraw = true;
        return; // Don't do other updates until init is done
    }

    // Periodically refresh list from scanner
    if (millis() - m_lastUpdate > 1000) {
        m_deviceList = m_scanner.getDevices();
        
        // Sort by RSSI
        std::sort(m_deviceList.begin(), m_deviceList.end(), [](const BLEDeviceInfo& a, const BLEDeviceInfo& b) {
            return a.rssi > b.rssi;
        });
        
        m_lastUpdate = millis();
        m_needsRedraw = true;
    }
}

void BleScannerScreen::render(Canvas& canvas) {
    if (!m_active || !m_needsRedraw) return;

    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Header
    ui::StatusBar::render(canvas, "BLE SCANNER", m_scanner.isScanning() ? "SCANNING..." : nullptr);
    
    if (m_isInitializing) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setTextSize(1);
        canvas.setCursor(config::SCREEN_WIDTH / 2 - 50, config::SCREEN_HEIGHT / 2);
        canvas.print("Initializing BLE...");
    } else {
        drawList(canvas);
        
        // Right content for footer
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%d DEVS", (int)m_deviceList.size());
        footerHints_.setRightContent(countBuf);
        footerHints_.render(canvas);
    }

    if (actionMenu_.isVisible()) {
        actionMenu_.render(canvas);
    }

    m_needsRedraw = false;
}

void BleScannerScreen::drawHeader(Canvas& canvas) {
    canvas.fillRect(0, 0, config::SCREEN_WIDTH, config::STATUS_BAR_HEIGHT, theme::BG_SECONDARY());
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.print("BLE SCANNER");
    
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "DEVS: %d", (int)m_deviceList.size());
    canvas.setCursor(config::SCREEN_WIDTH - 60, 5);
    canvas.print(countBuf);
    
    if (m_scanner.isScanning()) {
        canvas.setTextColor(theme::SUCCESS());
        canvas.setCursor(config::SCREEN_WIDTH - 110, 5);
        canvas.print("SCANNING...");
    }
}

void BleScannerScreen::drawList(Canvas& canvas) {
    int startY = config::STATUS_BAR_HEIGHT + 5;
    int itemHeight = 22;
    
    if (m_deviceList.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(20, startY + 40);
        canvas.print("Scanning for devices...");
        return;
    }

    for (int i = 0; i < ITEMS_PER_PAGE && (i + m_scrollOffset) < m_deviceList.size(); ++i) {
        int idx = i + m_scrollOffset;
        const auto& dev = m_deviceList[idx];
        int y = startY + (i * itemHeight);
        
        // Highlight selection - only if footer doesn't have focus
        bool showAsSelected = (idx == m_selectedIndex) && !footerHints_.hasFocus();
        
        if (showAsSelected) {
            canvas.fillRect(0, y, config::SCREEN_WIDTH, itemHeight, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_INVERSE());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        // Signal strength bar (simple)
        int rssiX = 5;
        int barW = map(constrain(dev.rssi, -100, -30), -100, -30, 2, 10);
        canvas.fillRect(rssiX, y + 5, barW, 10, idx == m_selectedIndex ? theme::TEXT_INVERSE() : theme::ACCENT());
        
        // Name and MAC
        canvas.setCursor(20, y + 2);
        canvas.print(dev.name.c_str());
        
        // Fast Pair Badge
        if (dev.isFastPair) {
            int nameW = strlen(dev.name.c_str()) * 6;
            int badgeX = 20 + nameW + 8;
            canvas.fillRoundRect(badgeX, y + 2, 20, 9, 2, theme::SUCCESS());
            canvas.setTextColor(theme::BG_PRIMARY());
            canvas.setCursor(badgeX + 2, y + 3);
            canvas.print("FP");
        }
        
        canvas.setTextSize(1);
        canvas.setTextColor(idx == m_selectedIndex ? theme::TEXT_INVERSE() : theme::TEXT_SECONDARY());
        canvas.setCursor(20, y + 12);
        canvas.print(ble::addressToString(dev.address).c_str());
        
        // RSSI value
        char rssiBuf[8];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d", dev.rssi);
        canvas.setCursor(config::SCREEN_WIDTH - 50, y + 2);
        canvas.print(rssiBuf);
        
        // Special tags
        if (dev.isFastPair) {
            canvas.setTextColor(theme::SUCCESS());
            canvas.setCursor(config::SCREEN_WIDTH - 30, y + 12);
            canvas.print("[FP]");
        }
    }
}

void BleScannerScreen::drawFooter(Canvas& canvas) {
    int footerY = config::SCREEN_HEIGHT - config::ACTION_BAR_HEIGHT;
    canvas.fillRect(0, footerY, config::SCREEN_WIDTH, config::ACTION_BAR_HEIGHT, theme::BG_SECONDARY());
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(5, footerY + 5);
    canvas.print("OK:Target  CLR:Clear  ESC:Back");
}

bool BleScannerScreen::handleInput(char key) {
    if (actionMenu_.isVisible()) {
        if (actionMenu_.handleInput(key)) {
            m_needsRedraw = true;
            return true;
        }
    }

    // Handle footer hints first
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    switch (key) {
        case ';': // UP (wraps to last)
        case 'k': {
            int n = (int)m_deviceList.size();
            if (n > 0) {
                m_selectedIndex = (m_selectedIndex - 1 + n) % n;
                if (m_selectedIndex < m_scrollOffset) m_scrollOffset = m_selectedIndex;
                else if (m_selectedIndex >= m_scrollOffset + ITEMS_PER_PAGE)
                    m_scrollOffset = m_selectedIndex - ITEMS_PER_PAGE + 1;
                m_needsRedraw = true;
            }
            return true;
        }

        case '.': // DOWN (wraps to first)
        case 'j': {
            int n = (int)m_deviceList.size();
            if (n > 0) {
                m_selectedIndex = (m_selectedIndex + 1) % n;
                if (m_selectedIndex >= m_scrollOffset + ITEMS_PER_PAGE)
                    m_scrollOffset = m_selectedIndex - ITEMS_PER_PAGE + 1;
                else if (m_selectedIndex < m_scrollOffset) m_scrollOffset = m_selectedIndex;
                m_needsRedraw = true;
            }
            return true;
        }
            
        case '\n': // ENTER - Open Action Menu
            if (!m_deviceList.empty() && m_selectedIndex < (int)m_deviceList.size()) {
                m_contextDevice = m_deviceList[m_selectedIndex];
                setupActionMenu(m_contextDevice);
                actionMenu_.show();
                m_needsRedraw = true;
            }
            return true;
            
        case 'c': // Clear
            m_scanner.clearDevices();
            m_deviceList.clear();
            m_selectedIndex = 0;
            m_scrollOffset = 0;
            m_needsRedraw = true;
            return true;
            
        case '`': // ESC
            m_shouldExit = true;
            return true;
    }
    
    return false;
}

void BleScannerScreen::setupActionMenu(const BLEDeviceInfo& device) {
    actionMenu_.clearItems();
    actionMenu_.setTitle((device.name.empty() || device.name == "Unknown" ? ble::addressToString(device.address) : device.name).c_str());
    
    actionMenu_.addItem('S', "Spoof Identity", true);
    
    // Add vendor-specific attacks
    if (device.manufacturerId == 0x004C) { // Apple
        actionMenu_.addItem('A', "Apple Attack", true);
    }
    
    actionMenu_.setOnAction([this](char action) {
        handleAction(action);
    });
}

void BleScannerScreen::handleAction(char action) {
    switch (action) {
        case 'S':
        case 's': {
            auto* spoofScreen = static_cast<BleSpoofScreen*>(ScreenManager::getInstance().getScreen(ScreenId::BLE_SPOOF));
            if (spoofScreen) {
                spoofScreen->setTarget(m_contextDevice);
                ScreenManager::getInstance().setActiveScreen(ScreenId::BLE_SPOOF);
            }
            break;
        }
        case 'A':
        case 'a': {
            // Future: Pass target to Apple attack (if we want targeted loop)
            ScreenManager::getInstance().setActiveScreen(ScreenId::BLE_APPLE_ATTACK);
            break;
        }
    }
    m_needsRedraw = true;
}

} // namespace adversary
