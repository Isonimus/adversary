/**
 * @file storage_overview.h
 * @brief Pure one-level directory walk for the SD Storage view (ADR-0166).
 *
 * Returns per-folder `{fileCount, totalBytes}` via a mockable `IDirectoryLister`
 * so the walk is host-testable without the SD hardware. The caller feeds either
 * the arduino-esp32 SD `File`-backed adapter on device, or a stub in the native
 * test rig.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace adversary {

/// Hard cap on entries scanned per folder. A dir with more entries returns
/// `truncated = true` and the partial sum, guarding against a pathological card.
constexpr uint16_t STORAGE_WALK_CAP = 500;

struct DirEntry {
    const char* name;
    uint32_t size;
    bool isDirectory;
};

/// Minimal directory iterator — one path open at a time, pull-model next().
class IDirectoryLister {
public:
    virtual ~IDirectoryLister() = default;
    virtual bool open(const char* path) = 0;
    virtual bool next(DirEntry& out) = 0;
    virtual void close() = 0;
};

struct StorageRow {
    bool ok;             // false iff open() failed (missing or unreadable dir)
    uint16_t fileCount;  // files only; subdirs are skipped from the count
    uint64_t totalBytes; // sum of file sizes at this one level
    bool truncated;      // true iff the walk hit STORAGE_WALK_CAP
};

/// One-level walk: files counted, subdirs skipped (no recursion). Stops at
/// STORAGE_WALK_CAP entries scanned.
StorageRow computeStorageRow(IDirectoryLister& lister, const char* path);

/// Curated well-known paths surfaced by the Storage overview (ADR-0166).
struct CuratedPath {
    const char* label;
    const char* path;
};

constexpr CuratedPath CURATED_PATHS[] = {
    {"Handshakes",  "/adversary/captures/handshakes"},
    {"Credentials", "/adversary/captures/credentials"},
    {"Packets",     "/adversary/captures/packets"},
    {"Wardriving",  "/adversary/captures/wardriving"},
    {"SubGHz",      "/adversary/subghz"},
    {"IR codes",    "/adversary/ir"},
    {"BadUSB",      "/adversary/badusb"},
    {"Dashboard",   "/adversary/dashboard"},
    {"Logs",        "/adversary/logs"},
    {"Coredumps",   "/adversary/coredumps"},
};

constexpr size_t CURATED_PATH_COUNT = sizeof(CURATED_PATHS) / sizeof(CURATED_PATHS[0]);

}  // namespace adversary
