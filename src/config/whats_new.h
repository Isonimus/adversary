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
    "STABILITY PASS",
    "",
    "GPS detection no longer crashes",
    "the device. A probe task that",
    "could overflow its stack and",
    "silently reboot is fixed.",
    "",
    "WiFi scans and attacks cover the",
    "full channel set again (1-14),",
    "so APs on the upper channels are",
    "no longer skipped after a",
    "radio restart.",
    "",
    "Dashboard hardened: device data",
    "(SSIDs, client names) is escaped",
    "before display, closing a",
    "script-injection hole.",
    "",
    "PMKID capture fixed: the hash is",
    "now read from the right fields,",
    "so captured PMKIDs crack instead",
    "of being rejected.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
