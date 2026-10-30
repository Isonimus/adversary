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
#include "ui/screen_manager.h"
#include "core/event_bus.h"
#include "core/event_types.h"
#include "core/event_data.h"
#include "utils/log.h"
#include <algorithm>
#include <cstring>

namespace adversary {

BleScannerScreen::BleScannerScreen()
    : m_scanner(BLEScanner::getInstance())
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
    m_list.setItemCount(0);  // Reset selection + scroll to the top

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
    
    LOG_D("BLESCAN", "visible, pending init...");
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

    LOG_D("BLESCAN", "hidden");
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

void BleScannerScreen::drawList(Canvas& canvas) {
    int startY = config::STATUS_BAR_HEIGHT + 5;
    const int16_t itemHeight = 22;

    if (m_deviceList.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(20, startY + 40);
        canvas.print("Scanning for devices...");
        return;
    }

    // The shared component derives the visible-row count from the list rect, keeps the selection
    // on screen, and draws the unified thumb; this callback paints one device row. The render rect
    // is sized to ITEMS_PER_PAGE so the on-screen row count is unchanged.
    m_list.setItemCount((int)m_deviceList.size());
    ScrollableList::render(canvas, m_list, 0, startY, config::SCREEN_WIDTH, ITEMS_PER_PAGE * itemHeight,
        itemHeight, [this](Canvas& c, int idx, int16_t y, bool /*selected*/) {
            const auto& dev = m_deviceList[idx];

            // Band drawn by the component (BG_SELECTED, dimmed on footer focus via the flag
            // below); row content is content-coloured, not selection-inverted (slice-0063).
            c.setTextColor(theme::TEXT_PRIMARY());

            // Signal strength bar (simple)
            int rssiX = 5;
            int barW = map(constrain(dev.rssi, -100, -30), -100, -30, 2, 10);
            c.fillRect(rssiX, y + 5, barW, 10, theme::ACCENT());

            // Name and MAC
            c.setCursor(20, y + 2);
            c.print(dev.name.c_str());

            // Fast Pair Badge
            if (dev.isFastPair) {
                int nameW = strlen(dev.name.c_str()) * theme::FONT_CELL_WIDTH;
                int badgeX = 20 + nameW + 8;
                c.fillRoundRect(badgeX, y + 2, 20, 9, 2, theme::SUCCESS());
                c.setTextColor(theme::BG_PRIMARY());
                c.setCursor(badgeX + 2, y + 3);
                c.print("FP");
            }

            c.setTextSize(1);
            c.setTextColor(theme::TEXT_SECONDARY());
            c.setCursor(20, y + 12);
            c.print(ble::addressToString(dev.address).c_str());

            // RSSI value
            char rssiBuf[8];
            snprintf(rssiBuf, sizeof(rssiBuf), "%d", dev.rssi);
            c.setCursor(config::SCREEN_WIDTH - 50, y + 2);
            c.print(rssiBuf);

            // Special tags
            if (dev.isFastPair) {
                c.setTextColor(theme::SUCCESS());
                c.setCursor(config::SCREEN_WIDTH - 30, y + 12);
                c.print("[FP]");
            }
        },
        !footerHints_.hasFocus());
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
        case 'k':
            m_list.setItemCount((int)m_deviceList.size());
            m_list.moveUp();  // wraps to last; model keeps the selection on screen
            m_needsRedraw = true;
            return true;

        case '.': // DOWN (wraps to first)
        case 'j':
            m_list.setItemCount((int)m_deviceList.size());
            m_list.moveDown();  // wraps to first; model keeps the selection on screen
            m_needsRedraw = true;
            return true;

        case '\n': // ENTER - Open Action Menu
            if (!m_deviceList.empty() && m_list.selectedIndex() < (int)m_deviceList.size()) {
                m_contextDevice = m_deviceList[m_list.selectedIndex()];
                setupActionMenu(m_contextDevice);
                actionMenu_.show();
                m_needsRedraw = true;
            }
            return true;

        case 'c': // Clear
            m_scanner.clearDevices();
            m_deviceList.clear();
            m_list.setItemCount(0);
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
    actionMenu_.setTitle((hasAdvertisedName(device) ? device.name : ble::addressToString(device.address)).c_str());
    
    actionMenu_.addItem('T', "Track device", true);
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
        case 'T':
        case 't': {
            // Hand the locked device to the proximity tracker. Queued (not published):
            // the main loop drains it after this input unwinds, so navigating away cannot
            // delete this screen mid-call. The 6-byte getNative() address rides bssid, the
            // display name rides ssid (ADR-0128, slice-0129).
            EventData evt(EventType::BLE_TRACK_TARGET_SELECTED);
            evt.payload.attackTarget.targetScreen = static_cast<int16_t>(ScreenId::BLE_TRACKER);
            memcpy(evt.payload.attackTarget.bssid, m_contextDevice.address.getNative(), 6);
            // By value: the ternary yields a temporary (one branch returns std::string by
            // value), so a reference would bind to it. A value copy is unambiguously safe.
            const std::string name = hasAdvertisedName(m_contextDevice)
                                         ? m_contextDevice.name
                                         : ble::addressToString(m_contextDevice.address);
            strncpy(evt.payload.attackTarget.ssid, name.c_str(), 32);
            evt.payload.attackTarget.ssid[32] = '\0';
            EventBus::getInstance().queue(evt);
            break;
        }
        case 'S':
        case 's': {
            // Hand the locked device to the identity spoofer. Queued, like the tracker above:
            // getScreen() only resolves statically-registered screens, but BLE_SPOOF is
            // factory-registered, so the old direct getScreen()->setTarget() path returned
            // nullptr and did nothing. The main loop drains this after input unwinds, so
            // navigating away cannot delete this screen mid-call (ADR-0128 launch pattern).
            EventData evt(EventType::BLE_SPOOF_TARGET_SELECTED);
            evt.payload.attackTarget.targetScreen = static_cast<int16_t>(ScreenId::BLE_SPOOF);
            memcpy(evt.payload.attackTarget.bssid, m_contextDevice.address.getNative(), 6);
            // By value: see the Track case above (ternary yields a temporary).
            const std::string name = hasAdvertisedName(m_contextDevice)
                                         ? m_contextDevice.name
                                         : ble::addressToString(m_contextDevice.address);
            strncpy(evt.payload.attackTarget.ssid, name.c_str(), 32);
            evt.payload.attackTarget.ssid[32] = '\0';
            EventBus::getInstance().queue(evt);
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
