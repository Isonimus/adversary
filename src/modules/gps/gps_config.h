#pragma once

/**
 * @file gps_config.h
 * @brief GPS module configuration and chip profile registry
 *
 * All GPS chips supported by Adversary speak standard NMEA over UART, so the
 * only chip-specific knowledge needed is the default baud rate and how long to
 * wait for the first sentence.  New chips can be added by appending one entry
 * to GPS_PROFILES — no code changes required elsewhere.
 *
 * Detection tries every (pin-set × profile) combination at boot and uses the
 * first that produces data.  GPSManager stores which pair succeeded so it can
 * be reported in the UI / logs.
 */

#include <stdint.h>
#include <stddef.h>
#include "config/pins.h"

namespace adversary {
namespace gps {

// ---------------------------------------------------------------------------
// Per-chip profile — add new GPS chips here
// ---------------------------------------------------------------------------
struct GPSProfile {
    const char*  chipName;            ///< Human-readable chip identifier
    uint32_t     baudRate;            ///< Default UART baud rate
    uint32_t     detectionTimeoutMs;  ///< How long to wait for first byte
};

/// Chips probed in order; first to respond wins.
constexpr GPSProfile GPS_PROFILES[] = {
    { "AT6668",   115200, 3000 },   ///< AT6668 / AT6558 / ATGM336H @ 115200 (M5Stack Cap default)
    { "ATGM336H", 9600,   2000 },   ///< ATGM336H factory default
    { "Generic",  9600,   1500 },   ///< Fallback: any 9600-baud NMEA module
    { "Generic",  38400,  1500 },   ///< Fallback: some u-blox devices
};

constexpr size_t GPS_PROFILE_COUNT = sizeof(GPS_PROFILES) / sizeof(GPS_PROFILES[0]);

// ---------------------------------------------------------------------------
// Per-connection pin set — add new caps / modules here
// ---------------------------------------------------------------------------
struct GPSPinSet {
    const char* name;   ///< Human-readable source (e.g. "Cap", "Grove")
    int8_t      rxPin;  ///< ESP32 RX pin (receives data from GPS TX)
    int8_t      txPin;  ///< ESP32 TX pin (sends data to GPS RX)
};

/// Pin sets probed in order; cap takes priority over Grove.
constexpr GPSPinSet GPS_PIN_SETS[] = {
    { "Cap",   pins::CAP_GPS_RX, pins::CAP_GPS_TX },  ///< Cap LoRa 1262 on-board GPS
    { "Grove", pins::GPS_RX,     pins::GPS_TX     },  ///< Grove port GPS
};

constexpr size_t GPS_PIN_SET_COUNT = sizeof(GPS_PIN_SETS) / sizeof(GPS_PIN_SETS[0]);

// ---------------------------------------------------------------------------
// UART and buffer constants
// ---------------------------------------------------------------------------
constexpr uint8_t GPS_UART_NUM = 2;          ///< HardwareSerial UART index

constexpr size_t GPS_BUFFER_SIZE        = 128;  ///< UART receive buffer
constexpr size_t GPS_SENTENCE_MAX_LEN   = 82;   ///< NMEA maximum sentence length

// Fast presence pre-check (slice-0007): before paying the per-profile detection
// timeouts, listen briefly for ANY byte on a pin set. A transmitting NMEA device
// puts bytes on the line at any listen baud (valid at its own rate, framing garbage
// at others), so one baud tells "device present" from "nothing attached" — and the
// absent case (the common one) used to cost the full sum of every profile timeout.
constexpr uint32_t GPS_PRESENCE_BAUD      = 115200; ///< Listen baud for the carrier check (cap GPS default)
constexpr uint32_t GPS_PRESENCE_WINDOW_MS = 1200;   ///< Silence budget before a pin set is empty — one full 1 Hz sentence period + margin

// GPS quality thresholds
constexpr uint8_t GPS_MIN_SATELLITES = 4;
constexpr float   GPS_MAX_HDOP       = 10.0f;
constexpr uint32_t GPS_FIX_TIMEOUT_MS = 5000;

} // namespace gps
} // namespace adversary
