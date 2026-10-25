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
 * same commit (see CLAUDE.md). The heading shows config::VERSION dynamically, so do not
 * repeat the version number in the body. Keep it to roughly a screenful (~34 chars/line).
 */

namespace adversary {

// Blank strings render as vertical spacing; "---" draws a divider (WhatsNewScreen mirrors
// the AboutScreen line conventions).
inline constexpr const char* const WHATS_NEW_LINES[] = {
    "FIELD USABILITY",
    "",
    "Screen sleep saves battery.",
    "The display dims after a while,",
    "then turns off; any key wakes it.",
    "Set the delays in Settings >",
    "Display (Dim After / Screen Off).",
    "",
    "Local time on the wardriving",
    "clock. Set your offset in",
    "Settings > Time Zone. Saved logs",
    "and exports stay in UTC.",
    "",
    "Clearer SD state. Actions that",
    "need a card (like Rebuild Index)",
    "now grey out when none is in,",
    "instead of failing silently.",
    "",
    "This screen. It shows once after",
    "an update; reopen it any time",
    "from Settings > What's New.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
