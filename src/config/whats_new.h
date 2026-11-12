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
    "HID + SPAM POLISH",
    "",
    "BadBLE is more than a keyboard.",
    "Pick Keyboard, Mouse or Gamepad",
    "and the host pairs with what",
    "you chose. Scripts emit the",
    "right HID reports.",
    "",
    "BLE spam is honest about what",
    "it does: unverified payloads",
    "dropped, per-channel pacing,",
    "Fast Pair pool pruned to the",
    "entries that still pop.",
    "",
    "Field panics now land as",
    "decodable backtraces. The",
    "coredump is rescued to SD on",
    "the next boot. BOTH-mode",
    "Int-WDT is instrumented.",
    "",
    "4-way captures with all four",
    "messages still need to pair",
    "by client and replay counter",
    "to count as complete.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
