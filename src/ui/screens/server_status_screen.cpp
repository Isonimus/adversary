/**
 * @file server_status_screen.cpp
 * @brief Server Status Screen implementation
 */

#include "server_status_screen.h"
#include "modules/server/server_manager.h"
#include "modules/storage/settings_manager.h"
#include "../theme.h"
#include <cstdio>

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

ServerStatusScreen::ServerStatusScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , lastUpdateMillis_(0)
{
}

ServerStatusScreen::~ServerStatusScreen() {
}

void ServerStatusScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    lastUpdateMillis_ = 0;
    
    footerHints_.setHints({
        {'`', "Back", true}
    });
}

void ServerStatusScreen::hide() {
    visible_ = false;
}

void ServerStatusScreen::update() {
    if (!visible_) return;
    
    // Refresh stats every 1 second
    if (millis() - lastUpdateMillis_ >= 1000) {
        lastUpdateMillis_ = millis();
        needsRedraw_ = true;
    }
}

void ServerStatusScreen::render(Canvas& canvas) {
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    ui::StatusBar::render(canvas, "SERVER STATUS", nullptr);
    drawStats(canvas);
    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void ServerStatusScreen::drawStats(Canvas& canvas) {
#ifdef ESP32
    auto& server = ServerManager::getInstance();
    auto& settings = SettingsManager::getInstance().get();
    bool running = server.isRunning();
    
    int16_t y = START_Y + 4;
    int16_t xLabel = 8;
    int16_t xValue = 85;
    
    canvas.setTextSize(1);
    
    // 1. Status
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("STATUS:");
    canvas.setCursor(xValue, y);
    if (running) {
        canvas.setTextColor(theme::SUCCESS());
        canvas.print("RUNNING");
    } else {
        canvas.setTextColor(theme::ERROR());
        canvas.print("STOPPED");
    }
    y += ROW_HEIGHT;
    
    // 2. IP Address
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("IP ADDR:");
    canvas.setCursor(xValue, y);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(server.getIPAddress());
    y += ROW_HEIGHT;
    
    // 3. mDNS Host
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("MDNS:");
    canvas.setCursor(xValue, y);
    canvas.setTextColor(theme::ACCENT());
    canvas.printf("%s.local", settings.system.deviceName);
    y += ROW_HEIGHT;
    
    // 4. Clients
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("CLIENTS:");
    canvas.setCursor(xValue, y);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.printf("%d connected", server.getConnectedStations());
    y += ROW_HEIGHT;
    
    // 5. Requests
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("REQUESTS:");
    canvas.setCursor(xValue, y);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.printf("%u hits", server.getRequestCount());
    y += ROW_HEIGHT;
    
    // 6. Uptime
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(xLabel, y);
    canvas.print("UPTIME:");
    canvas.setCursor(xValue, y);
    canvas.setTextColor(theme::ACCENT());
    
    uint32_t totalSecs = server.getUptime();
    uint32_t hours = totalSecs / 3600;
    uint32_t minutes = (totalSecs % 3600) / 60;
    uint32_t seconds = totalSecs % 60;
    canvas.printf("%02u:%02u:%02u", hours, minutes, seconds);
    
#else
    (void)canvas;
#endif
}

void ServerStatusScreen::drawFooter(Canvas& canvas) {
    footerHints_.render(canvas);
}

bool ServerStatusScreen::handleInput(char key) {
    if (key == '`' || key == 27) { // Back or Escape
        shouldExit_ = true;
        return true;
    }
    return false;
}

} // namespace adversary
