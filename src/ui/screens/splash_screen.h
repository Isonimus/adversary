/**
 * @file splash_screen.h
 * @brief Boot-up splash screen with logo and progress tracking
 */

#pragma once

#include <cstdint>

namespace adversary {

/**
 * @class SplashScreen
 * @brief Handles the display of the boot-up splash screen and progress tracking.
 * 
 * This component is used during the initialization sequence in main.cpp
 * to provide visual feedback during the boot process.
 */
class SplashScreen {
public:
    SplashScreen();
    
    /**
     * @brief Clear screen and draw the initial logo, subtitle, and version
     */
    void show();
    
    /**
     * @brief Update the progress bar and status message
     * @param progress value from 0.0f to 1.0f
     * @param message Text to display above the progress bar
     */
    void updateProgress(float progress, const char* message);
    
    /**
     * @brief Start the timer for minimum splash duration tracking
     */
    void startTimer();
    
    /**
     * @brief Block until the minimum splash duration has elapsed
     * @param minDurationMs override the default duration from config if needed (0 = use config)
     */
    void ensureMinDuration(uint32_t minDurationMs = 0);

private:
    float progress_;
    uint32_t startTime_;
    int phraseIndex_;
};

} // namespace adversary
