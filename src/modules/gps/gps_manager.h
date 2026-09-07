#pragma once

/**
 * @file gps_manager.h
 * @brief GPS module manager - chip-agnostic NMEA GPS detection and streaming
 *
 * Probes all registered (pin-set × baud-rate) combinations at boot.
 * First combination that produces data wins.  New chips/caps are added
 * via the profile/pin-set tables in gps_config.h — no code changes here.
 */

#ifdef ARDUINO
#include <HardwareSerial.h>
#else
#include <cstddef>
#include <cstdint>
// Mock HardwareSerial for native builds
#define SERIAL_8N1 0x800001c
class HardwareSerial {
public:
    HardwareSerial(int uart_num) {}
    void begin(unsigned long baud, uint32_t config = 0, int8_t rxPin = -1, int8_t txPin = -1, bool invert = false, unsigned long timeout_ms = 20000UL) {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t c) { return 0; }
    void flush() {}
    void end() {}
};
#endif
#include "gps_types.h"

namespace adversary {

namespace gps { struct GPSPinSet; }  // defined in gps_config.h; needed by the boot pre-check

namespace gps {
/**
 * @brief Fix-state edge to emit on the EventBus.
 */
enum class FixTransition : uint8_t { None, Acquired, Lost };

/**
 * @brief Pure decision: which fix event (if any) a validity change should emit.
 *
 * The one host-testable seam of the GPS_FIX_ACQUIRED/LOST wiring (mirrors
 * gpsProbePhase). Emits Acquired on false->true, Lost on true->false, and
 * None when the state is unchanged — so no event fires per-sentence, only on
 * the edge. Kept pure so the truth table is pinned without linking the manager.
 */
inline FixTransition fixTransition(bool wasValid, bool isValidNow) {
    if (wasValid == isValidNow) return FixTransition::None;
    return isValidNow ? FixTransition::Acquired : FixTransition::Lost;
}
}  // namespace gps


/**
 * @brief GPS Manager - Singleton for AT6668 GPS module
 * 
 * Manages GPS module lifecycle:
 * - Module detection at boot
 * - UART communication
 * - Status reporting
 */
class GPSManager {
public:
    /**
     * @brief Get singleton instance
     */
    static GPSManager& getInstance();
    
    /**
     * @brief Initialize GPS module and attempt detection
     * 
     * Initializes UART and attempts to detect the AT6668 module
     * by waiting for incoming data within the timeout period.
     * 
     * @param probeCapPort when false, the cap on-board GPS pin set (G13/G15) is
     *        skipped. Under the multi-radio cap those pins are the CC1101
     *        GDO0/CS control lines, not a UART, so a GPS probe must never drive
     *        them (slice-0002). The Grove-port GPS is unaffected.
     * @return true if GPS module detected, false otherwise
     */
    bool init(bool probeCapPort = true);
    
    /**
     * @brief Deinitialize GPS module
     * 
     * Closes UART connection and frees resources.
     */
    void deinit();
    
    /**
     * @brief Update GPS data from UART
     * 
     * Call this from main loop to continuously read and parse GPS data.
     * Reads available NMEA sentences and updates internal GPS state.
     */
    void update();
    
    /**
     * @brief Check if GPS module was detected
     *
     * @return true if GPS module is present and detected
     */
    bool isDetected() const { return detected_; }

    /**
     * @brief Name of the detected chip (e.g. "ATGM336H"), or nullptr if none.
     */
    const char* getDetectedChip() const { return detectedChip_; }

    /**
     * @brief Name of the pin set that worked (e.g. "Cap", "Grove"), or nullptr.
     */
    const char* getDetectedPinSet() const { return detectedPinSet_; }
    
    /**
     * @brief Get current GPS data
     * 
     * @return Current GPS coordinate and velocity data
     */
    const gps::GPSData& getCurrentData() const { return currentData_; }
    
    /**
     * @brief Check if GPS has valid fix
     * 
     * @param maxAgeMs Maximum age of data in milliseconds (default: 5 seconds)
     * @return true if GPS has valid, recent fix
     */
    bool hasValidFix(uint32_t maxAgeMs = 5000) const;
    
    /**
     * @brief Get time since last GPS update
     * 
     * @return Milliseconds since last valid GPS data update
     */
    uint32_t getTimeSinceLastUpdate() const;
    
    /**
     * @brief Attempt to re-detect GPS module if not already detected
     * 
     * Safe to call multiple times. On success, updates detected_ flag
     * and enables GPS updates in main loop.
     * 
     * @return true if GPS module detected (now or previously)
     */
    bool tryRedetect();
    
    /**
     * @brief Start background detection task
     * 
     * Creates a FreeRTOS task that periodically attempts GPS detection
     * if the module wasn't detected at boot. Task self-terminates on success.
     */
    void startBackgroundDetection();
    
    /**
     * @brief Stop background detection task
     */
    void stopBackgroundDetection();

    
#ifdef UNIT_TEST
    /**
     * @brief Set mock GPS data for testing (UNIT_TEST only)
     */
    void setMockData(const gps::GPSData& data) {
        currentData_ = data;
        detected_ = true;
    }
#endif
    
private:
    GPSManager();
    ~GPSManager() = default;
    
    // Prevent copying
    GPSManager(const GPSManager&) = delete;
    GPSManager& operator=(const GPSManager&) = delete;
    
    HardwareSerial* gpsSerial_;      ///< UART serial interface
    bool detected_;                  ///< Module detection status
    bool initialized_;               ///< Initialization status
    bool lastFixState_;              ///< Last published fix state (edge detection for GPS_FIX_* events)
    const char* detectedChip_;       ///< Profile chip name that responded
    const char* detectedPinSet_;     ///< Pin set name that worked
    
    // GPS data
    gps::GPSData currentData_;      ///< Current GPS position and velocity
    
    // NMEA sentence buffering
    char sentenceBuffer_[128];      ///< Buffer for incomplete NMEA sentence
    uint8_t bufferPos_;             ///< Current position in buffer
    
    // Background detection task
    void* taskHandle_;              ///< FreeRTOS task handle for background detection
    static void backgroundDetectionTask(void* param);

#ifdef ARDUINO
    /**
     * @brief Fast carrier pre-check for one pin set (slice-0007).
     *
     * Listens at GPS_PRESENCE_BAUD for any byte within GPS_PRESENCE_WINDOW_MS.
     * Returns true the instant a byte is seen (a device is transmitting), false
     * if the window elapses silent (nothing attached) — so init() can skip the
     * expensive per-profile identification hunt on an empty pin set. Firmware
     * only: it drives a real UART.
     */
    bool probePinSetCarrier(const gps::GPSPinSet& pins);
#endif
};

} // namespace adversary
