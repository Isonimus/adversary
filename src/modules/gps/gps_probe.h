#pragma once

/**
 * @file gps_probe.h
 * @brief Pure carrier-presence policy for the GPS boot pre-check (slice-0007).
 *
 * Detection splits two questions: "is a device present?" and "which chip/baud is
 * it?". The second needs the multi-second per-profile timeouts; the first does
 * not — a transmitting UART line yields bytes almost immediately at any listen
 * baud, a dead line yields none. This is the pure decision for question one, so
 * the boot path can skip the expensive identification hunt on an empty pin set.
 *
 * Header-only and hardware-free so it is proven natively; the UART I/O that feeds
 * it lives in gps_manager.cpp behind the firmware guard.
 */

#include <cstdint>
#include <cstring>

#include "modules/gps/gps_config.h"

namespace adversary {
namespace gps {

/// Verdict of the fast presence pre-check on one pin set.
enum class ProbePhase {
    Waiting,  ///< Window still open, no byte yet — keep listening.
    Present,  ///< A byte arrived — a device is transmitting on this line.
    Absent,   ///< Window elapsed with no byte — nothing attached.
};

/**
 * @brief Decide the presence phase from what has been observed so far.
 *
 * A single received byte proves a device is transmitting (valid at its own baud,
 * framing garbage at any other), so we stop early and commit to identification.
 * Absence is declared only once the whole window has elapsed silent — never
 * before — so a GPS merely between its ~1 Hz sentence bursts is not mistaken for
 * absent (which would wrongly hand the shared Grove GPIO to RFID).
 *
 * @param anyByteSeen true once at least one byte has been received on the line
 * @param elapsedMs   milliseconds since the listen window opened
 * @param windowMs    silence budget before declaring the pin set empty
 */
constexpr ProbePhase gpsProbePhase(bool anyByteSeen, uint32_t elapsedMs, uint32_t windowMs) {
    if (anyByteSeen) return ProbePhase::Present;
    if (elapsedMs >= windowMs) return ProbePhase::Absent;
    return ProbePhase::Waiting;
}

/**
 * @brief Whether a detected GPS forces the RFID reader offline.
 *
 * The MFRC522 reader speaks I2C on the Grove pins (G1/G2); a Grove-port GPS
 * drives those same pins for UART, so the two cannot coexist and RFID must be
 * skipped. A cap GPS lives on G13/G15 and shares nothing with the reader, so it
 * must NOT block RFID — the two run together (this is what keeps RFID available
 * while wardriving off the GNSS/LoRa cap). No GPS at all blocks nothing.
 *
 * Fail-safe on an unknown source: a GPS was detected but its pin set is not
 * reported (shouldn't happen — GPSManager sets it whenever init() succeeds — so
 * this is drift insurance). Rather than risk bringing up RFID onto G1/G2 and
 * stomping a possibly-active Grove GPS, treat unknown as a conflict and skip.
 *
 * @param gpsDetected    whether boot detection found a GPS
 * @param detectedSource GPSManager::getDetectedPinSet() — GPS_SOURCE_CAP,
 *                       GPS_SOURCE_GROVE, or nullptr
 */
inline bool gpsBlocksRfid(bool gpsDetected, const char* detectedSource) {
    if (!gpsDetected) return false;
    if (detectedSource == nullptr) return true;  // unknown → fail safe, skip RFID
    return std::strcmp(detectedSource, GPS_SOURCE_GROVE) == 0;
}

} // namespace gps
} // namespace adversary
