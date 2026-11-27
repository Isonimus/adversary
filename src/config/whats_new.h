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
    "DISCOVERABILITY / SETUP",
    "",
    "The dashboard now ships inside",
    "the firmware image. A stock",
    "M5Burner install renders a real",
    "dashboard with no clone, no",
    "zip, no SD copy. Overlay still",
    "wins if you drop files on SD.",
    "",
    "On-device Storage view. Browse",
    "the SD file tree with sizes",
    "from the menu. Finally answers",
    "'where are my handshakes'",
    "without a laptop.",
    "",
    "Dashboard login is a real form,",
    "not a browser Basic-auth popup.",
    "Looks like a web app, behaves",
    "like one.",
    "",
    "Handshake quality is honest.",
    "One crackability helper, stored",
    "equals displayed. Full 4-way is",
    "100, PMKID is 100, lone M2 is a",
    "fragment, not 70.",
    "",
    "Polish. Dashboard reflows on a",
    "phone. Tron theme reaches the",
    "web. Captive-portal RAM wins.",
};

inline constexpr int WHATS_NEW_LINE_COUNT =
    static_cast<int>(sizeof(WHATS_NEW_LINES) / sizeof(WHATS_NEW_LINES[0]));

} // namespace adversary
