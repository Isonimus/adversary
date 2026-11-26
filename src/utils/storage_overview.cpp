/**
 * @file storage_overview.cpp
 * @brief Implementation of the pure one-level directory walk for the SD Storage
 *        view (ADR-0166).
 */

#include "storage_overview.h"

namespace adversary {

StorageRow computeStorageRow(IDirectoryLister& lister, const char* path) {
    StorageRow row{false, 0, 0, false};
    if (!lister.open(path)) return row;
    row.ok = true;

    uint16_t scanned = 0;
    DirEntry entry{};
    while (lister.next(entry)) {
        if (scanned >= STORAGE_WALK_CAP) {
            row.truncated = true;
            break;
        }
        ++scanned;
        if (!entry.isDirectory) {
            ++row.fileCount;
            row.totalBytes += entry.size;
        }
    }

    lister.close();
    return row;
}

}  // namespace adversary
