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
    "SIGNAL HUNTER",
    "",
    "Hunt by signal. Pick Track on",
    "the BLE scanner, or Locate on",
    "the WiFi scanner, and walk down",
    "a beacon or AP with a live",
    "signal meter and a closer /",
    "farther arrow. Signal, not",
    "distance.",
    "",
    "Act on Evil Twin clients. ENTER",
    "a client to deauth it once or",
    "block it for the session.",
    "",
    "BLE Identity Spoof is fixed.",
    "Pick Spoof on a scanned device",
    "to clone its name and MAC; a",
    "cold launch routes you to the",
    "scanner to pick a target.",
    "",
    "Smoother log scrolling. Probe,",
    "credential and other read-only",
    "views show a scrollbar thumb.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
