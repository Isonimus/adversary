/**
 * @file splash_screen.cpp
 * @brief SplashScreen implementation
 */

#include "splash_screen.h"
#include "config/config.h"
#include "ui/theme.h"
#include <Arduino.h>

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif

namespace adversary {

SplashScreen::SplashScreen() : progress_(0.0f), startTime_(0), phraseIndex_(-1) {}

void SplashScreen::show() {
    M5.Display.fillScreen(theme::BG_PRIMARY());
    
    // Draw logo/title
    M5.Display.setTextColor(theme::ACCENT());
    M5.Display.setTextSize(2);
    
    // Center the title
    const char* title = "THE ADVERSARY";
    int16_t titleWidth = strlen(title) * 12;  // Approximate width
    int16_t titleX = (config::SCREEN_WIDTH - titleWidth) / 2;
    M5.Display.setCursor(titleX, 30);
    M5.Display.print(title);
    
    // Random Subtitle Selection
    static const char* phrases[] = {
        "Red Team Wireless Tool",
        "Takes one to know one",
        "Hack the world!",
        "Be the enemy",
        "Knock, knock",
        "Follow the white rabbit",
        "War never changes",
        "Code is poetry",
        "Zero Trust needed",
        "Default passwords kill",
        "Scanning for weakness",
        "Enter the matrix",
        "Privacy is a myth",
        "Root is the goal",
        "Stay under the radar",
        "Packet by packet",
        "The system is a playground",
        "Security is an illusion",
        "Decrypting reality",
        "Own the airwaves"
    };
    
    if (phraseIndex_ == -1) {
        phraseIndex_ = random(0, 20);
    }
    const char* subtitle = phrases[phraseIndex_];
    
    M5.Display.setTextColor(theme::TEXT_SECONDARY());
    M5.Display.setTextSize(1);
    int16_t subWidth = strlen(subtitle) * 6;
    int16_t subX = (config::SCREEN_WIDTH - subWidth) / 2;
    M5.Display.setCursor(subX, 55);
    M5.Display.print(subtitle);
    
    // Version
    M5.Display.setTextColor(theme::TEXT_DISABLED());
    char versionStr[32];
    snprintf(versionStr, sizeof(versionStr), "v%s", config::VERSION);
    int16_t verWidth = strlen(versionStr) * 6;
    int16_t verX = (config::SCREEN_WIDTH - verWidth) / 2;
    M5.Display.setCursor(verX, 70);
    M5.Display.print(versionStr);
    
    // Initial progress bar
    updateProgress(0.0f, "Starting...");
}

void SplashScreen::updateProgress(float progress, const char* message) {
    progress_ = progress;
    
    // Progress bar dimensions
    const int16_t barX = 20;
    const int16_t barY = 100;
    const uint16_t barWidth = config::SCREEN_WIDTH - 40;
    const uint16_t barHeight = 10;
    
    // Clear progress area
    M5.Display.fillRect(barX, barY - 15, barWidth, barHeight + 20, theme::BG_PRIMARY());
    
    // Draw message
    M5.Display.setTextColor(theme::TEXT_SECONDARY());
    M5.Display.setTextSize(1);
    M5.Display.setCursor(barX, barY - 12);
    M5.Display.print(message);
    
    // Draw progress bar background
    M5.Display.fillRoundRect(barX, barY, barWidth, barHeight, 3, theme::BG_SECONDARY());
    
    // Draw progress bar border
    M5.Display.drawRoundRect(barX, barY, barWidth, barHeight, 3, theme::TEXT_SECONDARY());
    
    // Draw progress bar fill
    uint16_t fillWidth = static_cast<uint16_t>((barWidth - 4) * progress);
    if (fillWidth > 0) {
        M5.Display.fillRoundRect(barX + 2, barY + 2, fillWidth, barHeight - 4, 2, theme::ACCENT());
    }
}

void SplashScreen::startTimer() {
    startTime_ = millis();
}

void SplashScreen::ensureMinDuration(uint32_t minDurationMs) {
    uint32_t duration = (minDurationMs > 0) ? minDurationMs : config::SPLASH_MIN_DURATION_MS;
    uint32_t elapsed = millis() - startTime_;
    if (elapsed < duration) {
        delay(duration - elapsed);
    }
}

} // namespace adversary
