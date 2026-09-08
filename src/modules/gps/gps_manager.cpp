/**
 * @file gps_manager.cpp
 * @brief GPS module manager implementation
 */

#include "gps_manager.h"
#include "gps_config.h"
#include "gps_probe.h"
#include "nmea_parser.h"
#include "modules/system/time_manager.h"
#include "core/event_bus.h"
#include "core/event_data.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../../../test/common/arduino_mocks.h"
#endif

namespace adversary {

static GPSManager* s_instance = nullptr;

GPSManager& GPSManager::getInstance() {
    if (!s_instance) s_instance = new GPSManager();
    return *s_instance;
}

GPSManager::GPSManager()
    : gpsSerial_(nullptr)
    , detected_(false)
    , initialized_(false)
    , lastFixState_(false)
    , detectedChip_(nullptr)
    , detectedPinSet_(nullptr)
    , bufferPos_(0)
    , taskHandle_(nullptr) {
}

bool GPSManager::init(bool probeCapPort) {
    if (initialized_) {
        return detected_;
    }

#ifdef ARDUINO
    gpsSerial_ = new HardwareSerial(gps::GPS_UART_NUM);

    // Probe every (pin-set × baud-rate profile) combination.
    // All NMEA GPS chips speak the same protocol — the only chip-specific
    // parameters are the default baud rate and startup time.
    for (size_t p = 0; p < gps::GPS_PIN_SET_COUNT && !detected_; ++p) {
        const gps::GPSPinSet& pins = gps::GPS_PIN_SETS[p];
        if (pins.rxPin < 0 || pins.txPin < 0) continue;

        // Under the multi-radio cap, the cap-GPS pins are the CC1101 control
        // lines — never drive a UART probe onto them (slice-0002).
        if (!probeCapPort && pins.rxPin == adversary::pins::CAP_GPS_RX) continue;

        // Fast carrier pre-check (slice-0007): the per-profile hunt below costs the
        // sum of every detection timeout, paid in full precisely when nothing is
        // attached. Skip it on a silent line — the common case — and only spend the
        // timeouts once we know a device is actually transmitting on this pin set.
        if (!probePinSetCarrier(pins)) {
            Serial.printf("[GPS] %s: no carrier, skipping\n", pins.name);
            continue;
        }

        for (size_t c = 0; c < gps::GPS_PROFILE_COUNT && !detected_; ++c) {
            const gps::GPSProfile& profile = gps::GPS_PROFILES[c];

            Serial.printf("[GPS] Probing %s on %s at %u baud...\n",
                          profile.chipName, pins.name, profile.baudRate);

            gpsSerial_->begin(profile.baudRate, SERIAL_8N1, pins.rxPin, pins.txPin);

            // Flush any stale / framing-error bytes before listening
            while (gpsSerial_->available()) gpsSerial_->read();

            // Require a real NMEA '$' — not just any noise byte
            uint32_t t0 = millis();
            while (millis() - t0 < profile.detectionTimeoutMs) {
                while (gpsSerial_->available()) {
                    char c = (char)gpsSerial_->read();
                    if (c == '$') {
                        detected_       = true;
                        detectedChip_   = profile.chipName;
                        detectedPinSet_ = pins.name;
                        break;
                    }
                }
                if (detected_) break;
                delay(10);
            }

            if (!detected_) {
                gpsSerial_->end();
            }
        }
    }

    initialized_ = true;

    if (detected_) {
        Serial.printf("[GPS] Detected %s on %s\n", detectedChip_, detectedPinSet_);
    } else {
        Serial.println("[GPS] No GPS module found");
        delete gpsSerial_;
        gpsSerial_ = nullptr;
    }

    return detected_;
#else
    detected_ = false;
    initialized_ = true;
    return false;
#endif
}

#ifdef ARDUINO
bool GPSManager::probePinSetCarrier(const gps::GPSPinSet& pins) {
    gpsSerial_->begin(gps::GPS_PRESENCE_BAUD, SERIAL_8N1, pins.rxPin, pins.txPin);

    // Drop any stale / framing-error bytes so only fresh line activity counts.
    while (gpsSerial_->available()) gpsSerial_->read();

    const uint32_t t0 = millis();
    gps::ProbePhase phase = gps::ProbePhase::Waiting;
    while (phase == gps::ProbePhase::Waiting) {
        phase = gps::gpsProbePhase(gpsSerial_->available() > 0,
                                   millis() - t0,
                                   gps::GPS_PRESENCE_WINDOW_MS);
        if (phase == gps::ProbePhase::Waiting) delay(10);
    }

    // The identification hunt re-opens the UART at each profile's baud, so release
    // this listen session regardless of the verdict.
    gpsSerial_->end();
    return phase == gps::ProbePhase::Present;
}
#endif

void GPSManager::deinit() {
    if (!initialized_) {
        return;
    }
    
#ifdef ARDUINO
    if (gpsSerial_) {
        gpsSerial_->end();
        delete gpsSerial_;
        gpsSerial_ = nullptr;
    }
#endif
    
    detected_       = false;
    initialized_    = false;
    detectedChip_   = nullptr;
    detectedPinSet_ = nullptr;
}

void GPSManager::update() {
    if (!detected_ || !gpsSerial_) {
        return;
    }
    
#ifdef ARDUINO
    // Read available bytes from UART
    while (gpsSerial_->available()) {
        char c = gpsSerial_->read();
        
        // Start of sentence
        if (c == '$') {
            bufferPos_ = 0;
            sentenceBuffer_[bufferPos_++] = c;
        }
        // End of sentence
        else if (c == '\n' && bufferPos_ > 0) {
            sentenceBuffer_[bufferPos_] = '\0';
            
            // Parse complete NMEA sentence
            gps::GPSCoordinate coord;
            gps::GPSVelocity vel;
            
            // Try GGA (Global Positioning System Fix Data)
            if (gps::NMEAParser::parseGGA(sentenceBuffer_, &coord)) {
                // Always update satellite count and HDOP (for progress feedback in UI)
                currentData_.coordinate.satellites = coord.satellites;
                currentData_.coordinate.hdop = coord.hdop;
                currentData_.coordinate.fixQuality = coord.fixQuality;
                
                // Only update full position when fix is valid
                if (coord.valid) {
                    currentData_.coordinate = coord;
                    currentData_.lastUpdateMs = millis();
                }
                
                // Debug: Log GGA parse status
                static uint32_t lastGGALog = 0;
                if (millis() - lastGGALog > 5000) {
                    if (coord.valid) {
                        Serial.printf("[GPS] GGA: lat=%.6f lon=%.6f alt=%.1f sats=%d fix=%d hdop=%.1f\n",
                                      coord.latitude, coord.longitude, coord.altitude,
                                      coord.satellites, coord.fixQuality, coord.hdop);
                    } else {
                        Serial.printf("[GPS] GGA: Acquiring satellites (%d sats, hdop=%.1f)\n",
                                      coord.satellites, coord.hdop);
                    }
                    lastGGALog = millis();
                }
            }
            // Try RMC — match on message type only, any talker ID
            else if (bufferPos_ > 6 && strncmp(sentenceBuffer_ + 3, "RMC,", 4) == 0) {
                     
                bool rmcValid = gps::NMEAParser::parseRMC(sentenceBuffer_, &coord, &vel);
                
                // Always update velocity data
                currentData_.velocity = vel;
                
                // Update coordinate if RMC status is valid
                if (rmcValid) {
                    currentData_.coordinate = coord;
                    currentData_.lastUpdateMs = millis();
                    
                    // Trigger time sync
                    TimeManager::getInstance().syncFromGPS(
                        coord.hour, coord.minute, coord.second,
                        coord.day, coord.month, coord.year
                    );
                } else {
                    // Even if invalid, update satellites/quality indicators from RMC
                    // This helps track progress toward fix
                    static uint32_t lastRMCLog = 0;
                    if (millis() - lastRMCLog > 10000) {
                        Serial.printf("[GPS] RMC: Waiting for fix (status=V)\n");
                        lastRMCLog = millis();
                    }
                }
            }
            // Try VTG (Track Made Good - velocity only)
            else if (gps::NMEAParser::parseVTG(sentenceBuffer_, &vel)) {
                currentData_.velocity = vel;
                // Don't update lastUpdateMs for velocity-only
            }
            
            // Reset buffer for next sentence
            bufferPos_ = 0;
        }
        // Add character to buffer
        else if (bufferPos_ < sizeof(sentenceBuffer_) - 1) {
            sentenceBuffer_[bufferPos_++] = c;
        }
        // Buffer overflow - reset
        else {
            bufferPos_ = 0;
        }
    }
#endif

    // Emit fix-state edges on the bus so any module can react without polling
    // the singleton. The decision is the pure gps::fixTransition seam (pinned by
    // the regression test); this glue just maps the edge to an event + payload.
    const bool fixNow = hasValidFix();
    const gps::FixTransition edge = gps::fixTransition(lastFixState_, fixNow);
    if (edge != gps::FixTransition::None) {
        EventData evt(edge == gps::FixTransition::Acquired
                          ? EventType::GPS_FIX_ACQUIRED
                          : EventType::GPS_FIX_LOST);
        evt.payload.gps.latitude   = currentData_.coordinate.latitude;
        evt.payload.gps.longitude  = currentData_.coordinate.longitude;
        evt.payload.gps.altitude   = currentData_.coordinate.altitude;
        evt.payload.gps.speed      = currentData_.velocity.speedKmh;
        evt.payload.gps.satellites = currentData_.coordinate.satellites;
        evt.payload.gps.hdop       = currentData_.coordinate.hdop;
        EventBus::getInstance().publish(evt);
    }
    lastFixState_ = fixNow;
}

bool GPSManager::hasValidFix(uint32_t maxAgeMs) const {
    if (!detected_) {
        return false;
    }
    
    return currentData_.isValid(maxAgeMs);
}

uint32_t GPSManager::getTimeSinceLastUpdate() const {
    if (currentData_.lastUpdateMs == 0) {
        return 0xFFFFFFFF;  // Never updated
    }
    
    return millis() - currentData_.lastUpdateMs;
}

bool GPSManager::tryRedetect(bool probeCapPort) {
    // Already detected - nothing to do
    if (detected_) {
        return true;
    }

#ifdef ARDUINO
    // If not initialized, try full init
    if (!initialized_) {
        return init(probeCapPort);
    }
    
    // Already initialized but not detected - try again
    // Clean up any existing serial
    if (gpsSerial_) {
        gpsSerial_->end();
        delete gpsSerial_;
        gpsSerial_ = nullptr;
    }
    
    // Reset state and try again
    initialized_ = false;
    bool result = init(probeCapPort);

    if (result) {
        Serial.println("[GPS] Module re-detected!");
    }
    
    return result;
#else
    return false;
#endif
}

void GPSManager::startBackgroundDetection() {
#ifdef ARDUINO
    // Don't start if already detected or task already running
    if (detected_ || taskHandle_ != nullptr) {
        return;
    }
    
    Serial.println("[GPS] Starting background detection task");
    
    xTaskCreatePinnedToCore(
        backgroundDetectionTask,
        "gps_detect",
        2048,           // Stack size
        this,           // Parameter
        1,              // Priority (low)
        (TaskHandle_t*)&taskHandle_,
        0               // Core 0 (background)
    );
#endif
}

void GPSManager::stopBackgroundDetection() {
#ifdef ARDUINO
    if (taskHandle_ != nullptr) {
        vTaskDelete((TaskHandle_t)taskHandle_);
        taskHandle_ = nullptr;
        Serial.println("[GPS] Background detection task stopped");
    }
#endif
}

void GPSManager::backgroundDetectionTask(void* param) {
#ifdef ARDUINO
    GPSManager* self = static_cast<GPSManager*>(param);
    
    // Check every 30 seconds
    constexpr uint32_t CHECK_INTERVAL_MS = 30000;
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
        
        // Try to detect GPS
        if (self->tryRedetect()) {
            Serial.println("[GPS] Background detection succeeded - stopping task");
            self->taskHandle_ = nullptr;
            vTaskDelete(nullptr);  // Delete self
            return;
        }
        
        Serial.println("[GPS] Background detection: GPS not found, retrying in 30s...");
    }
#else
    (void)param;
#endif
}

} // namespace adversary
