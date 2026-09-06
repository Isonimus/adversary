#pragma once

// Path-safety helpers for serving files from a fixed base directory over HTTP.
// Pure and dependency-free so it is unit-testable on the native target.

#include <string.h>

namespace adversary {
namespace path_security {

/**
 * @brief Validate a URL path before joining it to a fixed server base dir.
 *
 * Intended for the dashboard file server, which builds
 * "/adversary/dashboard" + <decoded URL path>. Pass the URL-DECODED path so
 * percent-encoded traversal (e.g. "%2e%2e%2f") is caught after decoding.
 *
 * Rejects:
 *  - empty / non-absolute paths (must start with '/')
 *  - parent-directory traversal — any "/.." segment
 *  - backslashes (Windows-style separators) and control characters
 *
 * "/.." (rather than a bare "..") is used so ordinary names that merely
 * contain dots — e.g. "/a..b.html" — are still allowed.
 *
 * @return true if the path is safe to append to a trusted base directory.
 */
inline bool isSafeWebPath(const char* path) {
    if (!path || path[0] != '/') return false;

    for (const char* p = path; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == 0x7f) return false;  // control chars / NUL region
        if (c == '\\') return false;              // backslash separator
    }

    if (strstr(path, "/..") != nullptr) return false;  // parent traversal

    return true;
}

} // namespace path_security
} // namespace adversary
