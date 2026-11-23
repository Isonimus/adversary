#pragma once

/**
 * @file whats_new.h
 * @brief Bundled "what's new" notes for the running release (ADR-0121, slice-0122).
 *
 * Hand-curated, operator-pitched summary of what changed in the version now running. This
 * is NOT generated from CHANGELOG.md: the on-device copy is deliberately terser and more
 * operator-facing, and generating it would couple the build to changelog markup for no gain.
 *
 * RELEASE CHECKLIST: when VERSION is bumped, rewrite these lines for the new release in the
 * same commit (see CLAUDE.md). The title line is composed as "v<config::VERSION> - <line 0>",
 * so line 0 is the release CODENAME only (e.g. "FIELD USABILITY"): do not put the version
 * number anywhere in the body. Keep it to roughly a screenful (~34 chars/line).
 */

namespace adversary {

// Blank strings render as vertical spacing; "---" draws a divider (WhatsNewScreen mirrors
// the AboutScreen line conventions).
inline constexpr const char* const WHATS_NEW_LINES[] = {
    "TARGETED HUNT",
    "",
    "Auto Hunt picks targets per",
    "cycle on a live score: RSSI,",
    "observed clients, and prior",
    "success. Productive APs rise",
    "to the top of the queue.",
    "",
    "Dead targets back off. A run",
    "of empty strikes earns an",
    "exponential cooldown, so the",
    "radio stops hammering an AP",
    "that is not yielding.",
    "",
    "The engine now says what it",
    "is doing. A rolling status",
    "line surfaces target rotation,",
    "quiet nudges, strikes and",
    "captures while they happen.",
    "",
    "Quieter serial. The deauth",
    "start path no longer prints",
    "the ESP_ERR_WIFI_NOT_INIT",
    "noise before the radio is up.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
