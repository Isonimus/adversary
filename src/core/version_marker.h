#pragma once

/**
 * @file version_marker.h
 * @brief Tracks the last firmware version the operator has seen, in NVS (ADR-0121).
 *
 * The marker lives in flash (NVS via Preferences), NOT in the SD-backed settings, so it
 * survives reboot on a cardless device: the OTA/M5Burner alpha operator is exactly who the
 * what's-new auto-show exists for, and that operator often runs with no SD. NVS is scoped
 * by ADR-0121 to device-level markers that must survive without a card; operator settings
 * stay in SD JSON.
 */

#include <cstring>

namespace adversary {

/// Pure comparison behind currentVersionIsNew(): the only branching logic, isolated so it is
/// host-testable without the NVS plumbing. An empty `seen` (first-ever boot, no stored value)
/// reads as "differs", so a fresh flash shows the notes.
inline bool versionDiffers(const char* seen, const char* current) {
    return std::strcmp(seen, current) != 0;
}

class VersionMarker {
public:
    /// True when the running config::VERSION differs from the version stored in NVS (or none
    /// is stored yet). Read-only: does not advance the marker. Device-only (NVS); the native
    /// build stubs this to false, see version_marker.cpp.
    static bool currentVersionIsNew();

    /// Record config::VERSION as the last-seen version in NVS, so currentVersionIsNew() is
    /// false until the next bump. Device-only; a native no-op.
    static void markCurrentVersionSeen();
};

} // namespace adversary
