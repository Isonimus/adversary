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

private:
    SystemManager();
    ~SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;

    uint16_t crashCount_;
    bool m_isStabilized;
    bool m_canvasPurged;
};

} // namespace adversary
