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

/**
 * @brief (Re)build the root carousel and list menus from current detection + SD
 *        state.
 *
 * The single menu-definition site. setup() calls it once at boot; the Settings →
 * Retry SD Mount handler calls it again after a successful mount so SD-blocked
 * entries (Captures, BadBLE, SERVER) un-grey without a reboot (slice-0021). Both
 * menu layers gate at build time — the list items are boot-frozen structs and the
 * SERVER tile's gate needs SD I/O that must not run per-frame — so a rebuild, not
 * a live per-frame check, is how SD state changes reach the menu. Idempotent;
 * resets the menu cursor to root.
 *
 * Defined in main.cpp for the same composition-root reason as redetectModules().
 */
void initializeMenu();

} // namespace adversary
