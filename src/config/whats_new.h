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
    "PMF-AWARE HUNT",
    "",
    "Auto Hunt now reads each AP's",
    "PMF capability and picks its",
    "weapon per target: deauth where",
    "it still works, 802.11v BTM",
    "steering where it does not.",
    "Mixed sessions mix.",
    "",
    "BTM steering reaches clientless",
    "APs too. A short solicitation",
    "nudges the AP into announcing",
    "its stations; a confirmed",
    "client then takes the steer.",
    "",
    "Quiet clients get surfaced.",
    "A QoS-Null nudge pulls idle",
    "stations into view before the",
    "strike, so a silent client no",
    "longer stays invisible.",
    "",
    "The BTM tag now reads BTM and",
    "BTM-CL. The (unverified)",
    "suffix is gone: the mechanism",
    "is proven.",
    "",
    "Framework libs moved onto our",
    "own signed build. Same bytes,",
    "our pipeline, SHA-pinned.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
