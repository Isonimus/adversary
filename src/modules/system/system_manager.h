/**
 * @file system_manager.h
 * @brief System-level management (power, reset reasons, crash logging)
 */

#pragma once

#include <cstdint>
#include <Arduino.h>
#include "inactivity.h"

namespace adversary {

/**
 * @class SystemManager
 * @brief Singleton class to handle system-level maintenance and crash logging.
 */
class SystemManager {
public:
    static SystemManager& getInstance() {
        static SystemManager instance;
        return instance;
    }

    /**
     * @brief Check and log the reset reason, handling crash counters
     */
    void logResetReason();

    /**
     * @brief Get the cumulative crash count (resets on POWERON)
     */
    uint16_t getCrashCount() const { return crashCount_; }

    /**
     * @brief Check if the system is in a potential bootloop
     */
    bool isBootloopRisk() const { return crashCount_ > 3; }

    /**
     * @brief Clear caches and non-essential buffers to free contiguous memory
     * Useful before starting memory-intensive tasks like SSL or BLE.
     * @param purgeCanvas If true, the 64KB UI Canvas sprite is deleted to free RAM.
     */
    // keepWifiUp: skip the WiFi.mode(WIFI_OFF) step so an already-associated
    // station stays connected across the call. Used by bulk sync, which connects
    // once and keeps the link up across many per-file canvas purge/restore cycles
    // (per-file WiFi off->STA toggling crashes the stack after ~20 cycles).
    void prepareForMemoryIntensiveTask(bool purgeCanvas = true, bool keepWifiUp = false);

    /**
     * @brief Restore state after memory-intensive task completes
     */
    void restoreFromMemoryIntensiveTask();

    /**
     * @brief Log detailed heap diagnostics (Free vs Largest Block)
     */
    void logHeapStatus(const char* tag);

    /**
     * @brief Apply screen brightness (0-100%)
     */
    void setDisplayBrightness(uint8_t percentage);

    /**
     * @brief Report an input event for inactivity screen sleep (ADR-0116).
     *
     * Resets the inactivity timer and restores full brightness if the screen was dimmed or
     * off. Returns true only when the screen was fully OFF (blind), signalling the caller to
     * swallow the triggering input so a wake tap on a dark screen does not also fire an
     * action. At the readable DIMMED stage it returns false, so that tap restores brightness
     * and still routes normally.
     */
    bool noteActivityAndWake();

    /**
     * @brief Advance the inactivity stage if the configured timeouts have elapsed (ADR-0116).
     *
     * Called once per main loop. Reads DisplaySettings::screenDimAfterSec / screenOffAfterSec
     * live (dim-after 0 = feature off) and applies the two-stage backlight dim. Backlight
     * only: the loop keeps running so an active attack/scan/wardriving session continues.
     */
    void updateScreenSleep();

private:
    SystemManager();
    ~SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;

    uint16_t crashCount_;
    bool m_isStabilized;
    bool m_canvasPurged;

    // Inactivity screen sleep (ADR-0116). lastActivityMs_ is stamped on every input;
    // currentStage_ is the applied backlight stage, so transitions fire once.
    uint32_t lastActivityMs_;
    ScreenSleepStage currentStage_;
};

} // namespace adversary
