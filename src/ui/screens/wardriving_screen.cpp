/**
 * @file wardriving_screen.cpp
 * @brief Wardriving screen implementation
 */

#include "wardriving_screen.h"
#include "modules/system/time_manager.h"
#include "modules/wifi/wifi_scanner.h"
#include <Arduino.h>

namespace adversary {

// ============================================================================
// Constructor / Destructor
// ============================================================================

WardrivingScreen::WardrivingScreen()
    : state_(WardrivingState::IDLE)
    , returnState_(WardrivingState::SCANNING)
    , visible_(false)
    , exitToMenu_(false)
    , lastUpdateMs_(0)
    , logWriteIndex_(0)
    , logCount_(0)
    , logScroll_(0)
    , logSelection_(0)
{
    // Initialize network log
    for (size_t i = 0; i < MAX_LOG_NETWORKS; i++) {
        networkLog_[i].ssid[0] = '\0';
        networkLog_[i].rssi = -100;
        networkLog_[i].channel = 0;
        networkLog_[i].security = 0;
        networkLog_[i].timestamp = 0;
        networkLog_[i].valid = false;
    }
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
        gps.startBackgroundDetection();
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
    
    // Only process WiFi scanning when session is active (not during SAVING/IDLE)
    if (state_ == WardrivingState::SCANNING || state_ == WardrivingState::PAUSED) {
        // Process wardriving state machine (triggers WiFi scans)
        auto& manager = wardriving::WardrivingManager::getInstance();
        manager.update();
        
        // Process WiFi scanner (completes async scans and processes results)
        auto& scanner = WiFiScanner::getInstance();
        scanner.update();
    }
    
    // Add networks to log from WiFi scanner (when scanning)
    if (state_ == WardrivingState::SCANNING) {
        auto& scanner = WiFiScanner::getInstance();
        const auto& networks = scanner.getNetworks();
        
        // Add new networks to circular buffer (rate limited)
        if (!networks.empty() && now - lastUpdateMs_ >= 2000) {
            for (const auto& net : networks) {
                // Check if already in last few entries (avoid immediate duplicates)
                bool isDuplicate = false;
                for (int i = 0; i < 5 && i < (int)logCount_; i++) {
                    int checkIdx = (logWriteIndex_ - 1 - i + MAX_LOG_NETWORKS) % MAX_LOG_NETWORKS;
                    if (networkLog_[checkIdx].valid && 
                        strcmp(networkLog_[checkIdx].ssid, net.ssid.c_str()) == 0) {
                        isDuplicate = true;
                        break;
                    }
                }
                
                if (!isDuplicate) {
                    // Add to circular buffer
                    auto& logEntry = networkLog_[logWriteIndex_];
                    strncpy(logEntry.ssid, net.ssid.c_str(), sizeof(logEntry.ssid) - 1);
                    logEntry.ssid[sizeof(logEntry.ssid) - 1] = '\0';
                    logEntry.rssi = net.rssi;
                    logEntry.channel = net.channel;
                    logEntry.security = static_cast<uint8_t>(net.security);
                    logEntry.timestamp = now;
                    logEntry.valid = true;
                    
                    // Advance write index
                    logWriteIndex_ = (logWriteIndex_ + 1) % MAX_LOG_NETWORKS;
                    if (logCount_ < MAX_LOG_NETWORKS) {
                        logCount_++;
                    }
                }
            }
            
            lastUpdateMs_ = now;
        }
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
                logScroll_ = 0;
                logSelection_ = 0;
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
                logScroll_ = 0;
                logSelection_ = 0;
                return true;
            }
            break;
            
        case WardrivingState::NETWORK_LOG:
            // Navigation
            if (key == ';' || key == '.' || key == ',') {  // Down
                if (logSelection_ < (int)logCount_ - 1) {
                    logSelection_++;
                    // Scroll if needed
                    if (logSelection_ - logScroll_ >= 7) {  // 7 visible items
                        logScroll_++;
                    }
                }
                return true;
            } else if (key == '/' ) {  // Up
                if (logSelection_ > 0) {
                    logSelection_--;
                    // Scroll if needed
                    if (logSelection_ < logScroll_) {
                        logScroll_--;
                    }
                }
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
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", 
             (unsigned long)hours, (unsigned long)minutes, (unsigned long)secs);
    return String(buffer);
}

String WardrivingScreen::formatDistance(float km) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.2f km", km);
    return String(buffer);
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

const char* WardrivingScreen::getSecurityString(uint8_t security) {
    // Map WiFiSecurity enum to short strings
    switch (security) {
        case 0:  return "Open";
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA3";
        case 5:  return "WPA/2";
        case 6:  return "WPA2/3";
        default: return "?";
    }
}

// ============================================================================
// Rendering
// ============================================================================

void WardrivingScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
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
    snprintf(ramBuf, sizeof(ramBuf), "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    ui::StatusBar::render(canvas, "Wardriving", ramBuf);
    
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
    y += 18;
    
    drawGPSStatus(canvas, y);
    
    // Status message above footer
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, canvas.height() - 32);
    canvas.print("Ready to start!");
    
    footerHints_.setHints({
        {'\r', "Start", true},
        {'`', "Exit", true}
    });
    footerHints_.render(canvas);
}

void WardrivingScreen::drawScanning(Canvas& canvas) {
    char ramBuf[12];
    snprintf(ramBuf, sizeof(ramBuf), "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    ui::StatusBar::render(canvas, "Wardriving", ramBuf);
    
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
    snprintf(ramBuf, sizeof(ramBuf), "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    ui::StatusBar::render(canvas, "Wardriving", ramBuf);
    
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
    snprintf(ramBuf, sizeof(ramBuf), "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    ui::StatusBar::render(canvas, "Wardriving", ramBuf);
    
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
    
    time_t now = timeManager.now();
    struct tm* timeinfo = gmtime(&now);
    
    // Show time source so user knows reliability:
    // NTP/GPS = accurate, SD = stale (off-time is lost without RTC)
    const char* srcTag;
    switch (timeManager.getSource()) {
        case TimeSource::NTP: srcTag = "NTP"; break;
        case TimeSource::GPS: srcTag = "GPS"; break;
        default:              srcTag = "~SD"; break;  // ~ = approximate
    }
    
    char timeStr[36];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d [%s]",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
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
    snprintf(ramBuf, sizeof(ramBuf), "%uK", (unsigned int)(ESP.getFreeHeap() / 1024));
    ui::StatusBar::render(canvas, "Network Log", ramBuf);
    
    const int16_t listY = 25;
    const int16_t listHeight = canvas.height() - 45;
    const int16_t itemHeight = 14;
    const int16_t visibleItems = listHeight / itemHeight;
    
    if (logCount_ == 0) {
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
    
    for (int i = 0; i < visibleItems && (logScroll_ + i) < (int)logCount_; i++) {
        int idx = logScroll_ + i;
        
        int bufferIdx = (logWriteIndex_ - 1 - idx + MAX_LOG_NETWORKS) % MAX_LOG_NETWORKS;
        const auto& net = networkLog_[bufferIdx];
        
        if (!net.valid) continue;
        
        int16_t y = listY + (i * itemHeight);
        bool isSelected = (idx == logSelection_);
        
        if (isSelected) {
            canvas.fillRect(0, y - 1, canvas.width(), itemHeight, theme::ACCENT());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        
        canvas.setCursor(4, y + 2);
        char ssid[17];
        strncpy(ssid, net.ssid, 16);
        ssid[16] = '\0';
        canvas.print(ssid);
        
        canvas.setCursor(canvas.width() - 90, y + 2);
        canvas.printf("Ch%d", net.channel);
        
        canvas.setCursor(canvas.width() - 60, y + 2);
        canvas.print(getSecurityString(net.security));
        
        uint16_t rssiColor = getRSSIColor(net.rssi);
        canvas.setTextColor(rssiColor);
        canvas.setCursor(canvas.width() - 30, y + 2);
        canvas.printf("%d", net.rssi);
    }
    
    if ((int)logCount_ > visibleItems) {
        int16_t scrollBarHeight = (visibleItems * listHeight) / logCount_;
        int16_t scrollBarY = listY + (logScroll_ * listHeight) / logCount_;
        canvas.fillRect(canvas.width() - 3, scrollBarY, 3, scrollBarHeight, theme::ACCENT());
    }
    
    char footerLeft[32];
    snprintf(footerLeft, sizeof(footerLeft), "%d/%d", logScroll_ + 1, (int)logCount_);
    footerHints_.setRightContent(footerLeft);
    
    footerHints_.setHints({
        {'`', "Back", true}
    });
    footerHints_.render(canvas);
}

} // namespace adversary
