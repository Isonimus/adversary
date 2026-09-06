#include "system_manager.h"
#include <esp_system.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "../storage/capture_registry.h"
#include "../wifi/wifi_scanner.h"
#include "../ble/ble_scanner.h"
#include "../ble/ble_spanner.h"
#include "core/heap_policy.h"

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif

// Forward declarations for UI canvas control (defined in main.cpp)
extern void adversary_ui_purge_canvas();
extern void adversary_ui_restore_canvas();

namespace adversary {

// RTC memory survives soft resets but not power cycles
RTC_DATA_ATTR static uint16_t rtcCrashCount = 0;

SystemManager::SystemManager()
    : m_isStabilized(false)
    , m_canvasPurged(false) {
    crashCount_ = rtcCrashCount;
}

void SystemManager::logResetReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    
    // Only log abnormal resets (crashes)
    bool isCrash = false;
    const char* reasonStr = "";
    
    switch (reason) {
        case ESP_RST_PANIC:
            reasonStr = "PANIC (Exception/Crash)";
            isCrash = true;
            break;
        case ESP_RST_INT_WDT:
            reasonStr = "INT_WDT (Interrupt Watchdog)";
            isCrash = true;
            break;
        case ESP_RST_TASK_WDT:
            reasonStr = "TASK_WDT (Task Watchdog)";
            isCrash = true;
            break;
        case ESP_RST_WDT:
            reasonStr = "WDT (Other Watchdog)";
            isCrash = true;
            break;
        case ESP_RST_BROWNOUT:
            reasonStr = "BROWNOUT (Power Issue)";
            isCrash = true;
            break;
        case ESP_RST_SDIO:
            reasonStr = "SDIO (External Reset)";
            isCrash = true;
            break;
        case ESP_RST_POWERON:
            reasonStr = "POWERON (Normal)";
            // Reset crash counter on clean power cycle
            rtcCrashCount = 0;
            crashCount_ = 0;
            break;
        case ESP_RST_SW:
            reasonStr = "SW (Software Reset)";
            break;
        case ESP_RST_DEEPSLEEP:
            reasonStr = "DEEPSLEEP (Wakeup)";
            break;
        default:
            reasonStr = "UNKNOWN";
            break;
    }
    
    Serial.printf("[Boot] Reset reason: %s (crash count: %d)\n", reasonStr, rtcCrashCount);
    
    if (!isCrash) {
        rtcCrashCount = 0;  // Reset on successful boot
        crashCount_ = 0;
        return;
    }
    
    // Increment crash counter
    rtcCrashCount++;
    crashCount_ = rtcCrashCount;
    
    // Bootloop protection: skip SD write if too many consecutive crashes
    if (rtcCrashCount > 3) {
        Serial.printf("[Boot] WARNING: %d consecutive crashes! Skipping SD log to prevent bootloop.\n", rtcCrashCount);
        return;
    }
    
    // Log crash to SD card
    Serial.println("[Boot] CRASH DETECTED - Logging to SD card...");
    
    // Ensure logs directory exists (SD might not be mounted yet in main setup, 
    // but logResetReason is called after SDManager::init in main.cpp)
    if (!SD.exists("/adversary/logs")) {
        SD.mkdir("/adversary/logs");
    }
    
    File logFile = SD.open("/adversary/logs/crash.log", FILE_APPEND);
    if (!logFile) {
        Serial.println("[Boot] Failed to open crash log file!");
        return;
    }
    
    // Write crash entry
    logFile.printf("========================================\n");
    logFile.printf("CRASH DETECTED (count: %d)\n", rtcCrashCount);
    logFile.printf("Reason: %s\n", reasonStr);
    logFile.printf("Boot Time: %lu ms since start\n", millis());
    logFile.printf("Free Heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    logFile.printf("Min Free Heap: %lu bytes\n", (unsigned long)ESP.getMinFreeHeap());
    logFile.printf("Heap Size: %lu bytes\n", (unsigned long)ESP.getHeapSize());
    logFile.printf("Free PSRAM: %lu bytes\n", (unsigned long)ESP.getFreePsram());
    logFile.printf("========================================\n\n");
    
    logFile.close();
    
    Serial.println("[Boot] Crash logged to /adversary/logs/crash.log");
}

void SystemManager::prepareForMemoryIntensiveTask(bool purgeCanvas, bool keepWifiUp) {
    // Adaptive purge: a caller may keep the canvas resident (purgeCanvas=false)
    // for live UI, but if the largest contiguous block is already too small for
    // a TLS handshake, free the 64800-byte canvas anyway — otherwise mbedTLS
    // fails with -32512 ("SSL - Memory allocation failed"). This is the post-
    // Karma fragmentation case: ~80KB free but the biggest block is only ~19KB.
    if (!purgeCanvas && !m_canvasPurged) {
        size_t largest =
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (largest < heap_policy::TLS_CONTIG_PURGE_FLOOR) {
            Serial.printf("[System] Largest block %u < %u — forcing canvas purge for TLS\n",
                          (unsigned int)largest,
                          (unsigned int)heap_policy::TLS_CONTIG_PURGE_FLOOR);
            purgeCanvas = true;
        }
    }

    if (m_isStabilized && (!purgeCanvas || m_canvasPurged)) {
        return; // Already stabilized and (no-purge-requested or already-purged)
    }

    Serial.println("[System] Memory stabilization starting...");
    Serial.printf("[System] Heap before: %u\n", (unsigned int)ESP.getFreeHeap());

    // 1. Release BLE memory (critically large)
    BLESpanner::getInstance().forceRelease();
    BLEScanner::getInstance().deinit();

    // DELAY: Ensure radio is fully released (fix AUTH_EXPIRE/ASSOC_EXPIRE)
    delay(200);

    // 2. Turn OFF WiFi completely to reclaim ~30KB of internal RAM driver memory.
    //    Skipped when keepWifiUp: a bulk sync keeps one association across many
    //    per-file purge/restore cycles instead of toggling WIFI_OFF<->STA each
    //    file (that churn crashes the WiFi stack after ~20 cycles).
    if (!keepWifiUp && WiFi.getMode() != WIFI_OFF) {
        WiFi.mode(WIFI_OFF);
        delay(200);
    }
    
    // 3. Clear transient caches
    CaptureRegistry::getInstance().invalidateCache(nullptr);
    
    // 4. THE NUCLEAR OPTION
    if (purgeCanvas && !m_canvasPurged) {
        adversary_ui_purge_canvas();
        m_canvasPurged = true;
    }
    
    m_isStabilized = true;
    Serial.printf("[System] Stabilization complete. Heap: %u\n", (unsigned int)ESP.getFreeHeap());
}

void SystemManager::restoreFromMemoryIntensiveTask() {
    if (!m_isStabilized) return;

    Serial.println("[System] Restoring system state...");
    
    // 1. Release BLE memory (just in case)
    BLEScanner::getInstance().deinit();
    delay(100);
    
    // 2. Restore global canvas if it was purged
    if (m_canvasPurged) {
        adversary_ui_restore_canvas();
        m_canvasPurged = false;
    }
    
    // 3. Restore WiFi to STATION mode (default)
    // CRITICAL MEMORY OPTIMIZATION:
    // Do NOT auto-restore WiFi here. Turning WiFi on consumes ~30-40KB Heap.
    // If the next screen (e.g. Menu/Carousel) doesn't need it, we waste memory and crash.
    // Screens that need WiFi (Scanner, Web) must explicitly enable it in their init()/show().
    // WiFi.mode(WIFI_STA); 
    // delay(100);
    
    m_isStabilized = false;
    Serial.printf("[System] System state restored. Heap: %u\n", (unsigned int)ESP.getFreeHeap());
}

void SystemManager::logHeapStatus(const char* tag) {
    size_t free = ESP.getFreeHeap();
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t minFree = ESP.getMinFreeHeap();
    
    Serial.printf("[DIAG] %-15s | Free: %6zu | MaxBlock: %6zu | MinFree: %6zu\n", 
                  tag, free, largest, minFree);
}

void SystemManager::setDisplayBrightness(uint8_t percentage) {
#ifdef ESP32
    if (percentage > 100) percentage = 100;
    
    // Apply brightness to display - range 0-255, our setting is 0-100%
    uint8_t brightness = (percentage * 255) / 100;
    M5.Display.setBrightness(brightness);
    Serial.printf("[System] Brightness set to %d%% (%d/255)\n", (int)percentage, brightness);
#endif
}

} // namespace adversary
