/**
 * @file wardriving_screen.cpp
 * @brief Wardriving screen implementation
 */

#include "wardriving_screen.h"
#include "utils/time_format.h"
#include "modules/system/time_manager.h"
#include "modules/wifi/wifi_scanner.h"
#include "hal/expansion/expansion_cap.h"
#include "modules/storage/settings_manager.h"
#ifdef ARDUINO
#include "modules/ble/ble_scanner.h"   // NimBLE; device-only (not in the native build filter)
#endif
#include <Arduino.h>

namespace adversary {

// How often the live Network Log harvests the scan sources. Both WiFiScanner and BLEScanner
// are polled, not event-driven, so the ring is refreshed on this cadence rather than per frame.
constexpr uint32_t LOG_HARVEST_INTERVAL_MS = 2000;

// ============================================================================
// Constructor / Destructor
// ============================================================================

WardrivingScreen::WardrivingScreen()
    : state_(WardrivingState::IDLE)
    , returnState_(WardrivingState::SCANNING)
    , visible_(false)
    , exitToMenu_(false)
    , lastUpdateMs_(0)
{
    // networkLog_ starts empty (count 0); no per-slot init needed.
}

WardrivingScreen::~WardrivingScreen() {
}

// ============================================================================
// Screen Lifecycle
// ============================================================================

void WardrivingScreen::init() {
    state_ = WardrivingState::IDLE;
    exitToMenu_ = false;
    lastUpdateMs_ = millis();
}

void WardrivingScreen::show() {
    visible_ = true;
    init();
    
    // If GPS not detected, start non-blocking background detection
    // This avoids the 3-second blocking delay that causes UI lag
    auto& gps = GPSManager::getInstance();
    if (!gps.isDetected()) {
        gps.startBackgroundDetection(
            hal::resolvedExpansionCap() != hal::ExpansionCap::MultiRadio);
    }
    
    footerHints_.setHints({}); // Will be set dynamically in render
    footerHints_.setFocus(false);
    
    // Initialize ActionMenu
    actionMenu_.setTitle("Stop Wardriving?");
    actionMenu_.clearItems();
    actionMenu_.addItem('S', "Save & Exit");
    actionMenu_.addItem('D', "Discard & Exit");
    actionMenu_.addItem('C', "Cancel");
    
    actionMenu_.setOnAction([this](char key) {
        auto& manager = wardriving::WardrivingManager::getInstance();
        if (key == 'S') {
            manager.stopSession();
            state_ = WardrivingState::SAVING;
        } else if (key == 'D') {
            manager.discardSession();
            state_ = WardrivingState::IDLE;
            exitToMenu_ = true;
        } else if (key == 'C') {
            state_ = previousState_;
        }
        actionMenu_.hide();
    });
}

void WardrivingScreen::hide() {
    visible_ = false;
    
    // Stop session if active
    auto& manager = wardriving::WardrivingManager::getInstance();
    if (manager.isActive()) {
        manager.stopSession();
    }
    
    // Free WiFiScanner's network results list — it can hold hundreds of APs in heap memory.
    // Without this call, the vector persists until the next scan overwrites it,
    // keeping RAM pinned after the screen is deleted.
    WiFiScanner::getInstance().deinit();
}

// ============================================================================
// Update
// ============================================================================

void WardrivingScreen::update() {
    if (!visible_) {
        return;
    }
    
    uint32_t now = millis();

    // The Network Log is a live overlay on a running session, not a mode that pauses it, so the
    // scan pipeline is driven off the session state underneath the view (returnState_ when the
    // log is open). Otherwise opening the log halts scanning/harvesting and the list freezes
    // until the operator backs out and re-enters; this keeps it refreshing in place like the
    // scanner/sniffer logs.
    const WardrivingState sessionState =
        (state_ == WardrivingState::NETWORK_LOG) ? returnState_ : state_;

    // Only process WiFi scanning when session is active (not during SAVING/IDLE)
    if (sessionState == WardrivingState::SCANNING || sessionState == WardrivingState::PAUSED) {
        // Process wardriving state machine (triggers WiFi scans)
        auto& manager = wardriving::WardrivingManager::getInstance();
        manager.update();

        // Process WiFi scanner (completes async scans and processes results)
        auto& scanner = WiFiScanner::getInstance();
        scanner.update();
    }

    // Harvest both scan sources into the live log (rate limited). The manager owns the CSV
    // ingest; this is the parallel display path, so BLE is read here too or the live list
    // would show only WiFi while the CSV records both (slice-0075).
    if (sessionState == WardrivingState::SCANNING && now - lastUpdateMs_ >= LOG_HARVEST_INTERVAL_MS) {
        const auto& networks = WiFiScanner::getInstance().getNetworks();
        for (const auto& net : networks) {
            networkLog_.append(net.ssid.c_str(), net.bssid, net.rssi, net.channel,
                               static_cast<uint8_t>(net.security),
                               wardriving::ObservationType::WIFI);
        }

#ifdef ARDUINO
        // BLE advertisers: the manager brings the controller up in BLE/BOTH mode and tears it
        // down in WiFi-only (ADR-0060), so an initialized scanner is the signal BLE is live.
        if (BLEScanner::isInitialized()) {
            auto devices = BLEScanner::getInstance().getDevices();
            for (const auto& dev : devices) {
                // NimBLEAddress::getNative() is little-endian; reverse to canonical BD_ADDR
                // order so the on-screen MAC matches the CSV, mirroring processBLEScan.
                const uint8_t* native = dev.address.getNative();
                uint8_t addr[6];
                for (int i = 0; i < 6; i++) {
                    addr[i] = native[5 - i];
                }
                // The scanner stamps nameless advertisers with BLE_NAME_UNKNOWN, so test the
                // advertised-name predicate (not emptiness) and let the row fall back to the MAC.
                networkLog_.append(hasAdvertisedName(dev) ? dev.name.c_str() : nullptr, addr,
                                   static_cast<int8_t>(dev.rssi), 0, 0,
                                   wardriving::ObservationType::BLE);
            }
        }
#endif

        lastUpdateMs_ = now;
    }
    
    // Auto-transition from SAVING to IDLE
    if (state_ == WardrivingState::SAVING) {
        static uint32_t savingStartMs = 0;
        
        if (savingStartMs == 0) {
            savingStartMs = now;
        }
        
        // Wait 2 seconds to show export messages
        if (now - savingStartMs >= 2000) {
            savingStartMs = 0;
            state_ = WardrivingState::IDLE;
            exitToMenu_ = true;  // Return to menu after save
        }
    }
}

// ============================================================================
// Input Handling
// ============================================================================

bool WardrivingScreen::handleInput(char key) {
    if (!visible_) return false;
    
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);
            return true;
        }
    }
    
    auto& manager = wardriving::WardrivingManager::getInstance();
    
    switch (state_) {
        case WardrivingState::IDLE:
            if (key == '\n' || key == '\r') {  // Enter
                // Start session
                if (manager.startSession()) {
                    state_ = WardrivingState::SCANNING;
                }
                return true;
            } else if (key == 'm' || key == 'M') {  // Cycle per-run radio mode
                cycleRadioMode();
                return true;
            } else if (key == '`' || key == 27) {  // Esc (backtick or ESC char)
                exitToMenu_ = true;
                return true;
            }
            break;
            
        case WardrivingState::SCANNING:
            if (key == '\n' || key == '\r') {  // Enter - Pause
                manager.pauseSession();
                state_ = WardrivingState::PAUSED;
                return true;
            } else if (key == 's' || key == 'S' || key == '`' || key == 27) {  // Save or STOP
                // Prompt to save/discard
                previousState_ = WardrivingState::SCANNING;
                state_ = WardrivingState::CONFIRM_EXIT;
                actionMenu_.show();
                return true;
            } else if (key == 'l' || key == 'L') {  // Network log
                returnState_ = WardrivingState::SCANNING;
                state_ = WardrivingState::NETWORK_LOG;
                logList_.setItemCount((int)networkLog_.count());
                logList_.setSelectedIndex(0);
                return true;
            }
            break;
            
        case WardrivingState::PAUSED:
            if (key == '\n' || key == '\r') {  // Enter - Resume
                manager.resumeSession();
                state_ = WardrivingState::SCANNING;
                return true;
            } else if (key == 's' || key == 'S' || key == '`' || key == 27) {  // Save or STOP
                // Prompt to save/discard
                previousState_ = WardrivingState::PAUSED;
                state_ = WardrivingState::CONFIRM_EXIT;
                actionMenu_.show();
                return true;
            } else if (key == 'l' || key == 'L') {  // Network log
                returnState_ = WardrivingState::PAUSED;
                state_ = WardrivingState::NETWORK_LOG;
                logList_.setItemCount((int)networkLog_.count());
                logList_.setSelectedIndex(0);
                return true;
            }
            break;
            
        case WardrivingState::NETWORK_LOG:
            // Navigation — the shared model wraps at the ends and keeps the selection on screen
            // (the renderer feeds it the live row count each frame).
            if (key == '.') {  // Down (Cardputer keyboard)
                logList_.moveDown();
                return true;
            } else if (key == ';') {  // Up (Cardputer keyboard)
                logList_.moveUp();
                return true;
            } else if (key == '`' || key == 27) {  // Back
                state_ = returnState_;
                return true;
            }
            break;
            
        case WardrivingState::SAVING:
            // No input during save
            break;
            
        case WardrivingState::CONFIRM_EXIT:
        {
            if (actionMenu_.handleInput(key)) {
                return true;
            }
            if (key == '`' || key == 27) {
                state_ = previousState_;
                actionMenu_.hide();
                return true;
            }
            return true; // Consume all input while in menu
        }
    }
    
    return false;
}

// ============================================================================
// Helper Functions
// ============================================================================

String WardrivingScreen::formatDuration(uint32_t seconds) {
    // Sized for the uint32_t worst case ("1193046:59:59"); formatting lives in the tested
    // utils helper so hours past two digits never truncate (slice-0057).
    char buffer[16];
    adversary::utils::formatDuration(seconds, buffer, sizeof(buffer));
    return String(buffer);
}

String WardrivingScreen::formatDistance(float km) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.2f km", km);
    return String(buffer);
}

const char* WardrivingScreen::statusHeapText(char* buf, size_t len) const {
    // The heap badge, when enabled, already renders free heap (and optionally the
    // largest free block) in the status bar; only add the centre readout when it is
    // off, so wardriving never shows the heap twice.
    if (SettingsManager::getInstance().get().system.showHeapBadge) return nullptr;
    snprintf(buf, len, "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    return buf;
}

wardriving::RadioMode WardrivingScreen::currentRadioMode() const {
    // Clamp on read: a corrupt stored value renders as WiFi rather than indexing the
    // label array out of bounds (nextRadioMode applies the same recovery on cycle).
    const uint8_t raw = SettingsManager::getInstance().get().wireless.wardrivingRadioMode;
    return raw < wardriving::RADIO_MODE_COUNT ? static_cast<wardriving::RadioMode>(raw)
                                              : wardriving::RadioMode::WIFI_ONLY;
}

const char* WardrivingScreen::radioModeLabel(wardriving::RadioMode mode) {
    // Operator-facing names for the ADR-0060 modes; the Settings editor shows the same
    // three words (settings_screen.cpp formatNumberValue).
    switch (mode) {
        case wardriving::RadioMode::BLE_ONLY: return "BLE";
        case wardriving::RadioMode::BOTH:     return "Both";
        case wardriving::RadioMode::WIFI_ONLY:
        default:                              return "WiFi";
    }
}

void WardrivingScreen::cycleRadioMode() {
    // ADR-0111: write straight through to the canonical field (no screen-local copy) and
    // persist, so the change is live for the next session start and shows identically in
    // the Settings editor. The manager reads this field at startSession, so the pre-start
    // idle screen is the correct place to set it.
    auto& settings = SettingsManager::getInstance().getMutable();
    const wardriving::RadioMode next = wardriving::nextRadioMode(currentRadioMode());
    settings.wireless.wardrivingRadioMode = static_cast<uint8_t>(next);
    SettingsManager::getInstance().save();
}

uint16_t WardrivingScreen::getRSSIColor(int8_t rssi) {
    // Color-code signal strength
    if (rssi > -50) {
        return theme::SUCCESS();   // Green: Excellent
    } else if (rssi > -70) {
        return theme::WARNING();   // Orange: Good
    } else {
        return theme::ERROR();     // Red: Weak
    }
}

// ============================================================================
// Rendering
// ============================================================================

void WardrivingScreen::render(Canvas& canvas) {
    if (!visible_) return;

    canvas.fillScreen(theme::BG_PRIMARY());

    // Only the network-log view shows a right-side position count; clear it every frame so the
    // sticky footer field can't bleed the stale "n/n" into the scanning/paused/idle screens.
    footerHints_.setRightContent(nullptr);

    switch (state_) {
        case WardrivingState::IDLE:
            drawIdle(canvas);
            break;
        case WardrivingState::SCANNING:
            drawScanning(canvas);
            break;
        case WardrivingState::PAUSED:
            drawPaused(canvas);
            break;
        case WardrivingState::SAVING:
            drawSaving(canvas);
            break;
        case WardrivingState::NETWORK_LOG:
            drawNetworkLog(canvas);
            break;
        case WardrivingState::CONFIRM_EXIT:
            // Draw background (scanning or paused) and then action menu
            if (previousState_ == WardrivingState::SCANNING) {
                drawScanning(canvas);
            } else {
                drawPaused(canvas);
            }
            actionMenu_.render(canvas);
            break;
    }
}

void WardrivingScreen::drawIdle(Canvas& canvas) {
    char ramBuf[12];
    ui::StatusBar::render(canvas, "Wardriving", statusHeapText(ramBuf, sizeof(ramBuf)));
    
    int16_t y = 22;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("Wardriving Mode");
    y += 14;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Map WiFi networks with GPS");
    y += 10;
    canvas.setCursor(4, y);
    canvas.print("coordinates for site surveys");
    y += 10;
    canvas.setCursor(4, y);
    canvas.print("and network analysis.");
    y += 16;

    // Per-run radio-mode quick-config (ADR-0111): this reads and writes the canonical
    // WirelessSettings field the manager already consults at session start, so Settings
    // stays the full editor and there is no screen-local copy to drift. Mode is the most
    // run-contextual wardriving knob, chosen from the RF environment right before a walk.
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Radio:");
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(45, y);
    canvas.print(radioModeLabel(currentRadioMode()));
    y += 16;

    drawGPSStatus(canvas, y);

    // Status message above footer
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, canvas.height() - 32);
    canvas.print("Ready to start!");

    footerHints_.setHints({
        {'\r', "Start", true},
        {'m', "Radio", true},
        {'`', "Back", true}
    });
    footerHints_.render(canvas);
}

void WardrivingScreen::drawScanning(Canvas& canvas) {
    char ramBuf[12];
    ui::StatusBar::render(canvas, "Wardriving", statusHeapText(ramBuf, sizeof(ramBuf)));
    
    auto& manager = wardriving::WardrivingManager::getInstance();
    const auto& session = manager.getSession();
    
    int16_t y = 25;
    
    uint32_t duration = session.getDurationSeconds();
    String durationStr = formatDuration(duration);
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.printf("Session: %s", durationStr.c_str());
    
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(canvas.width() - 50, y);
    canvas.print("ACTIVE");
    y += 16;
    
    drawSessionStats(canvas, y);
    y += 30;
    
    drawGPSStatus(canvas, y);
    
    // Draw time if synced
    drawTime(canvas, canvas.height() - 32);
    
    footerHints_.setHints({
        {'\r', "Pause", true},
        {'l', "Log", true},
        {'`', "Stop", true}
    });
    footerHints_.render(canvas);
}

void WardrivingScreen::drawPaused(Canvas& canvas) {
    char ramBuf[12];
    ui::StatusBar::render(canvas, "Wardriving", statusHeapText(ramBuf, sizeof(ramBuf)));
    
    auto& manager = wardriving::WardrivingManager::getInstance();
    const auto& session = manager.getSession();
    
    int16_t y = 25;
    
    uint32_t duration = session.getDurationSeconds();
    String durationStr = formatDuration(duration);
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.printf("Session: %s", durationStr.c_str());
    
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(canvas.width() - 50, y);
    canvas.print("PAUSED");
    y += 16;
    
    drawSessionStats(canvas, y);
    y += 32;
    
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(4, y);
    canvas.print("Session paused");
    canvas.print("Data collection stopped");
    
    // Draw time if synced
    drawTime(canvas, canvas.height() - 32);
    
    footerHints_.setHints({
        {'\r', "Resume", true},
        {'l', "Log", true},
        {'`', "Stop", true}
    });
    footerHints_.render(canvas);
}

void WardrivingScreen::drawSaving(Canvas& canvas) {
    char ramBuf[12];
    ui::StatusBar::render(canvas, "Wardriving", statusHeapText(ramBuf, sizeof(ramBuf)));
    
    auto& manager = wardriving::WardrivingManager::getInstance();
    const auto& session = manager.getSession();
    
    int16_t y = 50;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("Saving session...");
    y += 20;
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(4, y);
    canvas.printf("%s.csv", session.sessionId);
    y += 12;
    canvas.setCursor(4, y);
    canvas.printf("%s.json", session.sessionId);
    y += 20;
    
    canvas.setTextColor(theme::SUCCESS());
    canvas.printf("%lu networks exported", (unsigned long)session.networksFound);
    
    footerHints_.setHints({});
    footerHints_.render(canvas);
}

void WardrivingScreen::drawGPSStatus(Canvas& canvas, int16_t y) {
    using namespace gps;
    
    auto& gps = GPSManager::getInstance();
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("GPS:");
    
    if (!gps.isDetected()) {
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(35, y);
        canvas.print("Not detected");
        return;
    }
    
    const auto& gpsData = gps.getCurrentData();
    const auto& coord = gpsData.coordinate;
    
    if (!gps.hasValidFix()) {
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(35, y);
        canvas.printf("Waiting for fix... (%d sats)", coord.satellites);
        return;
    }
    
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(35, y);
    canvas.printf("%d sats, %.1f HDOP", coord.satellites, coord.hdop);
    y += 12;
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(4, y);
    canvas.printf("%.6f,%.6f Alt%dm", coord.latitude, coord.longitude, (int)coord.altitude);
}

void WardrivingScreen::drawTime(Canvas& canvas, int16_t y) {
    auto& timeManager = TimeManager::getInstance();
    if (!timeManager.isSynced()) return;
    
    // Render the operator's local time (display-only offset, ADR-0118); the canonical clock
    // and every persisted/exported stamp stay UTC.
    time_t now = timeManager.now();
    struct tm timeinfo;
    const int16_t tzOffsetMinutes = SettingsManager::getInstance().get().system.tzOffsetMinutes;
    if (!utils::localTimeFields(now, tzOffsetMinutes, &timeinfo)) return;  // unparseable: nothing to draw

    // Show time source so user knows reliability:
    // NTP/GPS = accurate, SD = stale (off-time is lost without RTC)
    const char* srcTag;
    switch (timeManager.getSource()) {
        case TimeSource::NTP: srcTag = "NTP"; break;
        case TimeSource::GPS: srcTag = "GPS"; break;
        default:              srcTag = "~SD"; break;  // ~ = approximate
    }

    // Fields are reduced modulo their width (no-ops on valid gmtime output) to give the
    // compiler the width proof, and the 3-char reliability tag is bounded with %.3s, so the
    // fixed buffer cannot be overrun (slice-0057).
    char timeStr[36];
    snprintf(timeStr, sizeof(timeStr), "%04u-%02u-%02u %02u:%02u:%02u [%.3s]",
             static_cast<unsigned>(timeinfo.tm_year + 1900) % 10000u,
             static_cast<unsigned>(timeinfo.tm_mon + 1) % 100u,
             static_cast<unsigned>(timeinfo.tm_mday) % 100u,
             static_cast<unsigned>(timeinfo.tm_hour) % 100u,
             static_cast<unsigned>(timeinfo.tm_min) % 100u,
             static_cast<unsigned>(timeinfo.tm_sec) % 100u,
             srcTag);
             
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print(timeStr);
}

void WardrivingScreen::drawSessionStats(Canvas& canvas, int16_t y) {
    auto& manager = wardriving::WardrivingManager::getInstance();
    const auto& session = manager.getSession();
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("Networks:");
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(70, y);
    canvas.printf("%lu  (%lu w/ GPS)", (unsigned long)session.networksFound, (unsigned long)session.networksWithGPS);
    y += 12;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Distance:");
    
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(70, y);
    String distStr = formatDistance(session.distanceTraveledKm);
    canvas.print(distStr);
}

void WardrivingScreen::drawNetworkLog(Canvas& canvas) {
    char ramBuf[12];
    ui::StatusBar::render(canvas, "Network Log", statusHeapText(ramBuf, sizeof(ramBuf)));
    
    const int16_t itemHeight = theme::LIST_ROW_HEIGHT;
    const int16_t listY = ui::STATUS_BAR_HEIGHT + 1;                         // sit just under the bar
    const int16_t listHeight = canvas.height() - listY - ui::FOOTER_HEIGHT;  // ...down to the footer

    logList_.setItemCount((int)networkLog_.count());

    if (networkLog_.count() == 0) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setTextSize(1);
        canvas.setCursor(4, listY + listHeight / 2);
        canvas.print("No networks logged yet");
        footerHints_.setHints({
            {'`', "Back", true}
        });
        footerHints_.render(canvas);
        return;
    }

    canvas.setTextSize(1);

    // The shared component derives the visible-row count from listHeight/itemHeight, keeps the
    // selection on screen, and draws the unified scrollbar; this callback only paints one row.
    ScrollableList::render(canvas, logList_, 0, listY, canvas.width(), listHeight, itemHeight,
        [this](Canvas& c, int idx, int16_t y, bool /*isSelected*/) {
            // The model clamps idx to [0, count), so newest-first access is always in range.
            const auto& net = networkLog_.newestFirst((size_t)idx);

            const bool isBle = net.type == wardriving::ObservationType::BLE;

            // The component paints the selection band; row text is content-coloured, not
            // selection-coloured (slice-0063 band-only policy for data lists).
            c.setTextColor(theme::TEXT_PRIMARY());

            // Label: SSID / device name, or the first three BD_ADDR octets when a BLE
            // advertiser is unnamed, so distinct unnamed devices stay distinguishable.
            char label[17];
            if (isBle && net.ssid[0] == '\0') {
                snprintf(label, sizeof(label), "%02X:%02X:%02X",
                         net.bssid[0], net.bssid[1], net.bssid[2]);
            } else {
                strncpy(label, net.ssid, sizeof(label) - 1);
                label[sizeof(label) - 1] = '\0';
            }
            c.setCursor(4, y + theme::LIST_ROW_TEXT_DY);
            c.print(label);

            // Type/metadata column: a "BLE" tag marks an advertiser (channel 0 and WiFi
            // security do not apply); a WiFi row keeps its channel and security.
            c.setCursor(c.width() - 90, y + theme::LIST_ROW_TEXT_DY);
            if (isBle) {
                c.setTextColor(theme::ACCENT());
                c.print("BLE");
            } else {
                c.printf("Ch%d", net.channel);
                c.setCursor(c.width() - 60, y + theme::LIST_ROW_TEXT_DY);
                c.print(wifiSecurityToString(static_cast<WiFiSecurity>(net.security)));
            }

            uint16_t rssiColor = getRSSIColor(net.rssi);
            c.setTextColor(rssiColor);
            c.setCursor(c.width() - 30, y + theme::LIST_ROW_TEXT_DY);
            c.printf("%d", net.rssi);
        });

    char footerLeft[32];
    snprintf(footerLeft, sizeof(footerLeft), "%d/%d", logList_.selectedIndex() + 1, (int)networkLog_.count());
    footerHints_.setRightContent(footerLeft);

    footerHints_.setHints({
        {'`', "Back", true}
    });
    footerHints_.render(canvas);
}

} // namespace adversary
