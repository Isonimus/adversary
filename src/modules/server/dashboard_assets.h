/**
 * @file dashboard_assets.h
 * @brief Embedded dashboard shell assets and their request-path resolver.
 *
 * The dashboard shell (index.html, dashboard.js, index.css, login.html) is
 * embedded into the firmware image via board_build.embed_files and served from
 * flash only (ADR-0168, slice-0169). The byte spans are bounded by the
 * linker-emitted _binary_*_start/_end symbols; the resolver below maps an HTTP
 * request path to its asset with a pure exact-match, which is native-testable.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace adversary {

/**
 * @brief One embedded dashboard shell file.
 *
 * @c start / @c end bound the bytes in flash .rodata (the paired
 * _binary_*_start/_end symbols board_build.embed_files emits). @c requestPath is
 * the exact HTTP path the browser asks for (e.g. "/index.html").
 */
struct DashboardAsset {
    const char* requestPath;
    const uint8_t* start;
    const uint8_t* end;
    const char* contentType;
};

/// Byte length of an embedded asset (end - start).
inline size_t dashboardAssetLen(const DashboardAsset& asset) {
    return static_cast<size_t>(asset.end - asset.start);
}

/**
 * @brief Resolve an HTTP request path to its embedded asset.
 *
 * Pure exact-match against @p table. "/" normalizes to "/index.html" first.
 * Because only known paths match, a traversal path (e.g.
 * "/../config/settings.json") simply fails to match and returns nullptr: there
 * is no filesystem path to escape, so this replaces the old path-join +
 * traversal guard rather than re-implementing it.
 *
 * @return the matching asset, or nullptr for an unknown path (a 404).
 */
const DashboardAsset* resolveDashboardAsset(const DashboardAsset* table,
                                            size_t count,
                                            const char* requestPath);

} // namespace adversary
