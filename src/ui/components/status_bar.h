/**
 * @file status_bar.h
 * @brief Reusable status bar component for all screens
 * 
 * Provides a consistent top bar with screen title, SD card status,
 * and battery level indicator.
 */

#pragma once

#include <cstdint>
#include "../theme.h"
#include "../../config/config.h"
#include "../../hal/storage/sd_manager.h"
#include "../../modules/gps/gps_manager.h"
#include "../../modules/rfid/rfid_manager.h"
#include "../../modules/storage/settings_manager.h"
#include <WiFi.h>

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif

namespace adversary {
namespace ui {

// External module detection flags (defined in main.cpp)
extern bool g_gpsDetected;
extern bool g_rfidDetected;

/**
 * @brief Status bar height in pixels
 */
constexpr int16_t STATUS_BAR_HEIGHT = 20;

/**
 * @brief Vertical center position for text (8px font height)
 */
constexpr int16_t TEXT_Y = (STATUS_BAR_HEIGHT - 8) / 2;

/**
 * @brief Uniform gap between every status indicator (owned by each render function)
 *
 * Convention: when a render function draws something it returns
 * (visual_left_edge - BADGE_GAP); when it draws nothing it returns
 * rightEdge unchanged.  The caller never applies gaps manually.
 */
constexpr int16_t BADGE_GAP = 4;

/**
 * @brief Reusable status bar component
 * 
 * Renders a consistent top bar across all screens with:
 * - Screen title (left aligned)
 * - Custom content area (center, optional)
 * - SD card status badge (right side)
 * - Battery level indicator (far right)
 */
class StatusBar {
public:
    /// Callback type for custom content rendering
    /// Parameters: display reference, x position where custom content ends (right edge of custom area)
    template<typename Display>
    using CustomContentCallback = void(*)(Display&, int16_t);

private:
    /**
     * @brief Render WiFi signal strength indicator
     * @return X position after rendering (for positioning next element)
     */
    template<typename Display>
    static int16_t renderWiFiSignal(Display& display, int16_t rightEdge) {
#ifdef ESP32
        if (!WiFi.isConnected()) {
            return rightEdge;  // Don't show when not connected
        }
        
        int8_t rssi = WiFi.RSSI();
        
        // Calculate signal strength level (0-4 bars)
        int bars = 0;
        if (rssi >= -50) bars = 4;       // Excellent
        else if (rssi >= -60) bars = 3;  // Good
        else if (rssi >= -70) bars = 2;  // Fair
        else if (rssi >= -80) bars = 1;  // Weak
        else bars = 0;                   // Very weak
        
        // Choose color based on signal strength
        uint16_t color;
        if (bars >= 3) {
            color = theme::SUCCESS();  // Green
        } else if (bars >= 2) {
            color = theme::WARNING();  // Yellow
        } else {
            color = theme::ERROR();    // Red
        }
        
        constexpr int16_t barWidth = 2;
        constexpr int16_t barGap = 1;
        constexpr int16_t maxBarHeight = 9;  // Tallest bar height
        constexpr int16_t barCount = 4;
        constexpr int16_t totalWidth = barCount * barWidth + (barCount - 1) * barGap;
        
        int16_t x = rightEdge - totalWidth;
        // Position the base of bars at vertical center + offset for visual balance
        int16_t baseY = (STATUS_BAR_HEIGHT + maxBarHeight) / 2;
        
        for (int i = 0; i < barCount; i++) {
            int16_t barHeight = 3 + (i * 2);  // 3, 5, 7, 9 pixels tall
            int16_t barX = x + i * (barWidth + barGap);
            int16_t barY = baseY - barHeight;  // Grow upward from baseline
            
            if (i < bars) {
                display.fillRect(barX, barY, barWidth, barHeight, color);
            } else {
                display.fillRect(barX, barY, barWidth, barHeight, theme::TEXT_DISABLED());
            }
        }
        
        return x - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

    /**
     * @brief Render battery level indicator
     * @return X position after rendering (for positioning next element)
     */
    template<typename Display>
    static int16_t renderBattery(Display& display, int16_t rightEdge) {
#ifdef ESP32
        int32_t batteryLevel = M5.Power.getBatteryLevel();
        
        constexpr int16_t battWidth = 18;
        constexpr int16_t battHeight = 10;
        constexpr int16_t battTip = 2;
        
        int16_t x = rightEdge - battWidth - battTip;
        int16_t y = (STATUS_BAR_HEIGHT - battHeight) / 2;
        
        uint16_t fillColor;
        if (batteryLevel > 50) {
            fillColor = theme::SUCCESS();
        } else if (batteryLevel > 20) {
            fillColor = theme::WARNING();
        } else {
            fillColor = theme::ERROR();
        }
        
        display.drawRect(x, y, battWidth, battHeight, theme::TEXT_DISABLED());
        display.fillRect(x + battWidth, y + 3, battTip, 4, theme::TEXT_DISABLED());
        
        int16_t fillWidth = ((battWidth - 2) * batteryLevel) / 100;
        if (fillWidth > 0) {
            display.fillRect(x + 1, y + 1, fillWidth, battHeight - 2, fillColor);
        }
        
        return x - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

    /**
     * @brief Render GPS status badge
     * @return X position after rendering (left edge of badge)
     */
    template<typename Display>
    static int16_t renderGPSStatus(Display& display, int16_t rightEdge) {
#ifdef ESP32
        if (!g_gpsDetected) return rightEdge;
        
        const auto& gpsData = GPSManager::getInstance().getCurrentData();
        bool hasFix = GPSManager::getInstance().hasValidFix();
        uint8_t sats = gpsData.coordinate.satellites;
        
        char badge[8];
        snprintf(badge, sizeof(badge), "G%d", (int)sats);
        
        int16_t badgeWidth = (sats >= 10) ? 18 : 14;
        int16_t x = rightEdge - badgeWidth;
        
        display.fillRoundRect(x - 2, 3, badgeWidth + 4, 14, 2, theme::BG_PRIMARY());
        display.setTextColor(hasFix ? theme::SUCCESS() : theme::WARNING());
        display.setTextSize(1);
        display.setCursor(x, 6);
        display.print(badge);
        
        return x - 2 - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

    /**
     * @brief Render RFID status badge
     * @return X position after rendering (left edge of badge)
     */
    template<typename Display>
    static int16_t renderRFIDStatus(Display& display, int16_t rightEdge) {
#ifdef ESP32
        if (!g_rfidDetected) return rightEdge;
        
        const char* badge = "RF";
        int16_t badgeWidth = 14;
        int16_t x = rightEdge - badgeWidth;
        
        display.fillRoundRect(x - 2, 3, badgeWidth + 4, 14, 2, theme::BG_PRIMARY());
        display.setTextColor(theme::SUCCESS());
        display.setTextSize(1);
        display.setCursor(x, 6);
        display.print(badge);
        
        return x - 2 - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

    /**
     * @brief Render free heap memory badge (e.g. "H128")
     * @return X position after rendering (left edge of badge)
     */
    template<typename Display>
    static int16_t renderHeapBadge(Display& display, int16_t rightEdge) {
#ifdef ESP32
        const auto& settings = SettingsManager::getInstance().get();
        if (!settings.system.showHeapBadge) return rightEdge;
        
        uint32_t freeHeap = ESP.getFreeHeap() / 1024;  // KB
        
        char badge[12];
        snprintf(badge, sizeof(badge), "H%u", (unsigned)freeHeap);
        
        int16_t badgeWidth = strlen(badge) * 6;
        int16_t x = rightEdge - badgeWidth;
        
        // Color by health: green >100KB, yellow >50KB, red ≤50KB
        uint16_t color;
        if (freeHeap > 100) color = theme::SUCCESS();
        else if (freeHeap > 50) color = theme::WARNING();
        else color = theme::ERROR();
        
        display.fillRoundRect(x - 2, 3, badgeWidth + 4, 14, 2, theme::BG_PRIMARY());
        display.setTextColor(color);
        display.setTextSize(1);
        display.setCursor(x, 6);
        display.print(badge);
        
        return x - 2 - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

    /**
     * @brief Render SD card status badge
     * @return X position after rendering (left edge of badge)
     */
    template<typename Display>
    static int16_t renderSDStatus(Display& display, int16_t rightEdge) {
#ifdef ESP32
        bool sdReady = SDManager::getInstance().isReady();
        const char* badge = sdReady ? "SD" : "!SD";
        
        int16_t badgeWidth = sdReady ? 14 : 20;
        int16_t x = rightEdge - badgeWidth;
        
        display.fillRoundRect(x - 2, 3, badgeWidth + 4, 14, 2, 
                              sdReady ? theme::BG_PRIMARY() : theme::ERROR());
        
        display.setTextColor(sdReady ? theme::SUCCESS() : theme::BG_PRIMARY());
        display.setTextSize(1);
        display.setCursor(x, 6);
        display.print(badge);
        
        return x - 2 - BADGE_GAP;
#else
        (void)display;
        return rightEdge;
#endif
    }

public:
    /**
     * @brief Render the status bar with optional custom content
     */
    template<typename Display>
    static void render(Display& display, const char* title,
                       const char* centerContent = nullptr,
                       uint16_t bgColor = theme::BG_SECONDARY(),
                       uint16_t textColor = theme::TEXT_PRIMARY()) {
        int16_t screenWidth = display.width();
        
        // Background
        display.fillRect(0, 0, screenWidth, STATUS_BAR_HEIGHT, bgColor);
        
        // Title
        display.setTextColor(textColor);
        display.setTextSize(1);
        display.setCursor(5, TEXT_Y);
        display.print(title);
        
        int16_t xPos = screenWidth - BADGE_GAP;

        // Indicators (each render fn owns its trailing gap)
        xPos = renderWiFiSignal(display, xPos);
        xPos = renderBattery(display, xPos);
        xPos = renderSDStatus(display, xPos);
        xPos = renderGPSStatus(display, xPos);
        xPos = renderRFIDStatus(display, xPos);
        xPos = renderHeapBadge(display, xPos);
        
        // Custom center content
        if (centerContent && centerContent[0] != '\0') {
            int16_t contentX = xPos - 8;
            int16_t contentLen = strlen(centerContent) * 6;
            
            display.setTextColor(theme::TEXT_SECONDARY());
            display.setCursor(contentX - contentLen, TEXT_Y);
            display.print(centerContent);
        }
    }
    
    /**
     * @brief Render with custom accent for active/running states
     */
    template<typename Display>
    static void renderActive(Display& display, const char* title, 
                             const char* centerContent = nullptr,
                             uint16_t accentColor = theme::SUCCESS()) {
        render(display, title, centerContent, accentColor, theme::BG_PRIMARY());
    }
};

} // namespace ui
} // namespace adversary
