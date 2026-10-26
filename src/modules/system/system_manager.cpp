#include "system_manager.h"
#include <esp_system.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ctime>
#include <cstring>
#include "../storage/capture_registry.h"
#include "../storage/settings_manager.h"
#include "../wifi/wifi_scanner.h"
#include "../ble/ble_scanner.h"
#include "../ble/ble_spanner.h"
#include "core/crash_report.h"
#include "core/heap_policy.h"
#include "inactivity.h"
#include "time_manager.h"
#include "utils/log.h"

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

// RTC flight recorder (ADR-0126): the last heap sample the main loop took. NOINIT so
// it survives a soft reset (PANIC/WDT) untouched by the C runtime; a magic sentinel
// tells a genuine sample from the garbage left in RTC RAM after a cold power-on or
// brownout, so logResetReason never reports stale numbers as crash state.
namespace {
constexpr uint32_t HEAP_RECORDER_MAGIC = 0xAD5A11ED;
struct HeapRecorder {
    uint32_t magic;
    adversary::HeapSnapshot snapshot;
    uint32_t epochSec;  ///< Wall clock at the sample, 0 when the clock was unsynced
};

// Translate the IDF reset enum into the firmware's host-testable ResetReason
// (ADR-0126). Thin by design: all classification lives in classifyReset.
adversary::ResetReason mapResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_PANIC:     return adversary::ResetReason::Panic;
        case ESP_RST_INT_WDT:   return adversary::ResetReason::IntWdt;
        case ESP_RST_TASK_WDT:  return adversary::ResetReason::TaskWdt;
        case ESP_RST_WDT:       return adversary::ResetReason::OtherWdt;
        case ESP_RST_BROWNOUT:  return adversary::ResetReason::Brownout;
        case ESP_RST_SDIO:      return adversary::ResetReason::ExternalSdio;
        case ESP_RST_POWERON:   return adversary::ResetReason::PowerOn;
        case ESP_RST_SW:        return adversary::ResetReason::Software;
        case ESP_RST_DEEPSLEEP: return adversary::ResetReason::DeepSleep;
        default:                return adversary::ResetReason::Unknown;
    }
}
} // namespace

RTC_NOINIT_ATTR static HeapRecorder rtcHeapRecorder;

SystemManager::SystemManager()
    : m_isStabilized(false)
    , m_canvasPurged(false)
    , lastActivityMs_(millis())
    , currentStage_(ScreenSleepStage::ACTIVE) {
    crashCount_ = rtcCrashCount;
}

void SystemManager::recordHeapSnapshot() {
    rtcHeapRecorder.snapshot.freeHeap = ESP.getFreeHeap();
    rtcHeapRecorder.snapshot.largestFreeBlock =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    rtcHeapRecorder.snapshot.minFreeHeap = ESP.getMinFreeHeap();
    rtcHeapRecorder.snapshot.uptimeMs = millis();

    TimeManager& time = TimeManager::getInstance();
    rtcHeapRecorder.epochSec =
        time.isSynced() ? static_cast<uint32_t>(time.now()) : 0;

    rtcHeapRecorder.magic = HEAP_RECORDER_MAGIC;
}

void SystemManager::logResetReason() {
    const ResetClassification cls = classifyReset(mapResetReason(esp_reset_reason()));

    // Consume the flight recorder: capture whether this boot's RTC survived, then
    // invalidate it so a crash early in *this* session (before it has sampled) reports
    // "unavailable" rather than the previous session's stale snapshot. The struct's
    // fields stay readable below; only the validity magic is cleared.
    const bool snapshotValid = rtcHeapRecorder.magic == HEAP_RECORDER_MAGIC;
    rtcHeapRecorder.magic = 0;

    LOG_I("BOOT", "Reset reason: %s (crash count: %d)", cls.name, rtcCrashCount);

    if (!cls.isCrash) {
        rtcCrashCount = 0;  // Reset streak on a clean boot
        crashCount_ = 0;
        return;
    }

    rtcCrashCount++;
    crashCount_ = rtcCrashCount;

    // Bootloop protection: skip SD write if too many consecutive crashes
    if (rtcCrashCount > 3) {
        LOG_W("BOOT", "%d consecutive crashes! Skipping SD log to prevent bootloop.", rtcCrashCount);
        return;
    }

    LOG_W("BOOT", "CRASH DETECTED - logging to SD card...");

    // Format the stored epoch into a readable stamp so entries correlate to a session.
    char timestamp[32];
    if (snapshotValid && rtcHeapRecorder.epochSec != 0) {
        const time_t when = static_cast<time_t>(rtcHeapRecorder.epochSec);
        struct tm utc;
        gmtime_r(&when, &utc);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
    } else {
        strncpy(timestamp, "unsynced", sizeof(timestamp));
        timestamp[sizeof(timestamp) - 1] = '\0';
    }

    char entry[512];
    formatCrashEntry(entry, sizeof(entry), cls, rtcCrashCount,
                     snapshotValid, rtcHeapRecorder.snapshot, timestamp);

    // Ensure logs directory exists (SD might not be mounted yet in main setup,
    // but logResetReason is called after SDManager::init in main.cpp)
    if (!SD.exists("/adversary/logs")) {
        SD.mkdir("/adversary/logs");
    }

    File logFile = SD.open("/adversary/logs/crash.log", FILE_APPEND);
    if (!logFile) {
        LOG_E("BOOT", "Failed to open crash log file!");
        return;
    }
    logFile.print(entry);
    logFile.close();

    LOG_I("BOOT", "Crash logged to /adversary/logs/crash.log");
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
            LOG_W("SYSTEM", "Largest block %u < %u - forcing canvas purge for TLS",
                  (unsigned int)largest,
                  (unsigned int)heap_policy::TLS_CONTIG_PURGE_FLOOR);
            purgeCanvas = true;
        }
    }

    if (m_isStabilized && (!purgeCanvas || m_canvasPurged)) {
        return; // Already stabilized and (no-purge-requested or already-purged)
    }

    LOG_I("SYSTEM", "Memory stabilization starting...");
    LOG_D("SYSTEM", "Heap before: %u", (unsigned int)ESP.getFreeHeap());

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
    LOG_I("SYSTEM", "Stabilization complete. Heap: %u", (unsigned int)ESP.getFreeHeap());
}

void SystemManager::restoreFromMemoryIntensiveTask() {
    if (!m_isStabilized) return;

    LOG_I("SYSTEM", "Restoring system state...");
    
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
    LOG_I("SYSTEM", "System state restored. Heap: %u", (unsigned int)ESP.getFreeHeap());
}

void SystemManager::logHeapStatus(const char* tag) {
    // Inlined so the whole telemetry statement (ESP heap calls included) vanishes
    // when this DEBUG-level log compiles out at the INFO default.
    LOG_D("DIAG", "%-15s | Free: %6zu | MaxBlock: %6zu | MinFree: %6zu",
          tag, ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
          ESP.getMinFreeHeap());
    (void)tag;  // only consumed by the debug log above
}

void SystemManager::setDisplayBrightness(uint8_t percentage) {
#ifdef ESP32
    if (percentage > 100) percentage = 100;

    // Apply brightness to display - range 0-255, our setting is 0-100%
    uint8_t brightness = (percentage * 255) / 100;
    M5.Display.setBrightness(brightness);
    LOG_D("SYSTEM", "Brightness set to %d%% (%d/255)", (int)percentage, brightness);
#endif
}

bool SystemManager::noteActivityAndWake() {
    lastActivityMs_ = millis();
    if (currentStage_ == ScreenSleepStage::ACTIVE) {
        return false;
    }
    // Any input restores full brightness. Swallow the input only when the screen was fully
    // OFF (blind), so a wake tap there cannot also fire an action; at the readable DIMMED
    // stage the operator can see the screen, so the tap wakes it and still routes (ADR-0116).
    const bool wasOff = (currentStage_ == ScreenSleepStage::OFF);
    setDisplayBrightness(SettingsManager::getInstance().get().display.brightness);
    currentStage_ = ScreenSleepStage::ACTIVE;
    LOG_D("SYSTEM", "Screen woken from inactivity (was %s)", wasOff ? "off" : "dimmed");
    return wasOff;
}

void SystemManager::updateScreenSleep() {
    const auto& disp = SettingsManager::getInstance().get().display;
    const ScreenSleepStage target = inactivityStage(
        millis(), lastActivityMs_, disp.screenDimAfterSec, disp.screenOffAfterSec);
    if (target == currentStage_) {
        return;
    }

    // Backlight only (ADR-0116): loop() keeps running, so any active session continues.
    switch (target) {
        case ScreenSleepStage::ACTIVE: setDisplayBrightness(disp.brightness); break;
        case ScreenSleepStage::DIMMED: setDisplayBrightness(SCREEN_DIM_BRIGHTNESS_PCT); break;
        case ScreenSleepStage::OFF:    setDisplayBrightness(0); break;
    }
    currentStage_ = target;
    LOG_D("SYSTEM", "Screen sleep stage -> %d", (int)target);
}

} // namespace adversary
