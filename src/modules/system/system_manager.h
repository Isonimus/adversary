/**
 * @file system_manager.h
 * @brief System-level management (power, reset reasons, crash logging)
 */

#pragma once

#include <cstdint>
#include <Arduino.h>

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
     * @brief Report an input event for inactivity screen sleep (ADR-0114).
     *
     * Resets the inactivity timer. If the screen is currently dimmed for inactivity, this
     * restores the backlight to the configured brightness and returns true, signalling the
     * caller to swallow the triggering input so a wake tap on a dark screen does not also
     * fire an action. Returns false otherwise (the input routes normally).
     */
    bool noteActivityAndWake();

    /**
     * @brief Dim the backlight if the inactivity timeout has elapsed (ADR-0114).
     *
     * Called once per main loop. Reads DisplaySettings::screenTimeoutSec live (0 = never)
     * and, on the transition into idle, sets the backlight to 0. Backlight only: the loop
     * keeps running so an active attack/scan/wardriving session continues in the dark.
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

    // Inactivity screen sleep (ADR-0114). lastActivityMs_ is stamped on every input;
    // displayDimmed_ gates the one-shot dim transition and the wake.
    uint32_t lastActivityMs_;
    bool displayDimmed_;
};

} // namespace adversary
