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
    "CLIENTLESS PMKID",
    "",
    "Capture a PMKID straight from",
    "an AP with no clients connected.",
    "The new Clientless PMKID mode",
    "solicits the hash itself.",
    "",
    "Auto Hunt now picks its strategy:",
    "Deauth, PMKID, or Both.",
    "",
    "The .22000 cracking path is fixed",
    "end to end. Hash type tags were",
    "swapped, so captures never",
    "cracked. New captures do.",
    "",
    "4-way captures are paired by",
    "client and replay counter, so",
    "busy-air grabs are no longer",
    "rejected as invalid.",
    "",
    "Bulk sync ends on a recap: what",
    "sent, what failed, and why.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
