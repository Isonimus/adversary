/**
 * @file dashboard_assets.cpp
 * @brief Pure request-path resolver for the embedded dashboard shell.
 *
 * This translation unit references no _binary_* symbols, so it compiles and
 * links on the native target for test_dashboard_assets. The real asset table
 * (which does bind the flash symbols) lives in server_manager.cpp, device-only.
 */

#include "dashboard_assets.h"

#include <string.h>

namespace adversary {

const DashboardAsset* resolveDashboardAsset(const DashboardAsset* table,
                                            size_t count,
                                            const char* requestPath) {
    if (table == nullptr || requestPath == nullptr) return nullptr;

    // Root normalizes to the index document.
    const char* path = (requestPath[0] == '/' && requestPath[1] == '\0')
                           ? "/index.html"
                           : requestPath;

    for (size_t i = 0; i < count; ++i) {
        if (table[i].requestPath != nullptr &&
            strcmp(table[i].requestPath, path) == 0) {
            return &table[i];
        }
    }
    return nullptr;
}

} // namespace adversary
