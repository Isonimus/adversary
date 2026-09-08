/**
 * @file module_detection.h
 * @brief One re-runnable peripheral-detection pass (slice-0018).
 *
 * Boot probes the expansion cap, GPS, and RFID exactly once and caches each
 * verdict; a peripheral attached later is invisible until reboot. This declares
 * the single sequence that re-runs all three probes in the arbitration-correct
 * order (cap -> GPS -> RFID, because the GPS cap-pin skip depends on the resolved
 * cap and RFID gating depends on the resolved GPS source) and refreshes the
 * detection globals. setup() calls it at boot; the Modules dashboard's Re-scan
 * calls the same function so the two paths never drift.
 *
 * The definition lives in main.cpp, not a core/*.cpp: the native build compiles
 * all of core/ but excludes hal/expansion/expansion_cap.cpp, so a core TU calling
 * detectExpansionCap() would fail to link natively. main.cpp is native-excluded
 * and already owns the detection globals and the SD/GPS/RFID singletons — the
 * correct composition-root home.
 */

#pragma once

namespace adversary {

/**
 * @brief Re-probe cap + GPS + RFID and refresh ui::g_expansionCap /
 *        g_gpsDetected / g_rfidDetected.
 *
 * Synchronous and firmware-only. Borrows and re-mounts the SD SPI bus for the cap
 * probe, so the caller must guarantee the bus is idle (no capture/replay/scan in
 * flight) — the operator contract is insert-while-idle, then Re-scan.
 */
void redetectModules();

} // namespace adversary
