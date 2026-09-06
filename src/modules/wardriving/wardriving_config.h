/**
 * @file wardriving_config.h
 * @brief Wardriving mode configuration constants
 */

#pragma once

#include <cstdint>

namespace adversary {
namespace wardriving {

// ============================================================================
// Network Storage
// ============================================================================

/// Maximum unique networks per session (tracked via BSSID hash set)
constexpr size_t MAX_NETWORKS = 5000;

/// Pending buffer size for GPS updates before writing to SD
/// Small buffer - networks are written to SD as soon as GPS is obtained
constexpr size_t PENDING_BUFFER_SIZE = 20;

/// Flush interval - write pending networks to SD every N milliseconds
constexpr uint32_t FLUSH_INTERVAL_MS = 30000;  // 30 seconds

/// Max pending before forced flush (even without GPS)
constexpr size_t MAX_PENDING_BEFORE_FLUSH = 10;

// ============================================================================
// Update Intervals
// ============================================================================

/// WiFi scan interval (milliseconds)
constexpr uint32_t WIFI_SCAN_INTERVAL_MS = 1000;

/// GPS distance update interval (milliseconds)
constexpr uint32_t GPS_UPDATE_INTERVAL_MS = 1000;

/// UI refresh interval (milliseconds)
constexpr uint32_t UI_REFRESH_INTERVAL_MS = 500;

// ============================================================================
// GPS Thresholds
// ============================================================================

/// Minimum movement to update distance (meters)
constexpr float MIN_DISTANCE_DELTA_M = 5.0f;

/// Maximum HDOP for GPS tagging (lower is better)
constexpr float MAX_HDOP_FOR_TAGGING = 10.0f;

/// Minimum satellites for GPS tagging
constexpr uint8_t MIN_SATELLITES_FOR_TAGGING = 4;

// ============================================================================
// Export Paths
// ============================================================================

/// Base directory for wardriving data
constexpr const char* WARDRIVING_DIR = "/adversary/captures/wardriving";

/// CSV file extension
constexpr const char* CSV_EXTENSION = ".csv";

/// JSON file extension
constexpr const char* JSON_EXTENSION = ".json";

} // namespace wardriving
} // namespace adversary
