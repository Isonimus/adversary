/**
 * @file manifest_store.h
 * @brief Flat fixed-size binary store for HandshakeRecord (manifest.bin)
 *
 * On-disk layout:
 *   [ ManifestHeader (32 B) ][ Record 0 (256 B) ]...[ Record N-1 ]
 *
 * Record `i` lives at the stable offset HEADER_SIZE + i*RECORD_SIZE, so a
 * mutation is a single seek+write of 256 bytes — no full-file rewrite, no
 * JSON, ~256 B of stack. This is the central memory/crash-safety win of the
 * storage rework (docs/DESIGN-storage-rework.md §3, §8).
 *
 * Crash safety: append writes the record first, then bumps the header count;
 * a crash in between is detected on next open (header count vs file size) and
 * the partial trailing record is ignored. Each record carries a CRC so a torn
 * 256-byte write is detected on read.
 *
 * The store opens/closes the file per operation (matching the codebase's SD
 * idiom) and caches only the record count in RAM.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>

#include "handshake_record.h"

#ifdef ESP32
#include <SD.h>
#else
#include <SD.h>  // resolves to test/common/SD.h host-backed mock
#endif

namespace adversary {

/// 32-byte file header.
struct __attribute__((packed)) ManifestHeader {
    uint32_t magic;        ///< MANIFEST_MAGIC
    uint16_t formatVersion;
    uint16_t recordSize;   ///< Bytes per record (sanity)
    uint32_t recordCount;  ///< Committed record count
    uint8_t  flags;        ///< MF_* bitfield (bit0: dir fully reconciled)
    uint8_t  reserved[19];
};

static_assert(sizeof(ManifestHeader) == 32, "ManifestHeader must be 32 bytes");

class ManifestStore {
public:
    static constexpr uint32_t MAGIC          = 0x314D4841;  // 'A','H','M','1'
    // v2: HandshakeRecord gained the pwncrack service block. Bumping this makes
    // open() reject a v1 manifest so the registry recreates it (no migration —
    // the .pcap files are re-indexed and service statuses repopulate on sync).
    static constexpr uint16_t FORMAT_VERSION = 2;
    static constexpr uint16_t RECORD_SIZE    = 256;
    static constexpr uint32_t HEADER_SIZE    = 32;
    static constexpr uint32_t COUNT_OFFSET   = 8;   // offsetof(ManifestHeader, recordCount)
    static constexpr uint32_t FLAGS_OFFSET   = 12;  // offsetof(ManifestHeader, flags)

    /// Header flag: the manifest fully indexes the .pcap directory, so the slow
    /// boot dir scan can be skipped. Set once after the first reconcile and kept
    /// in sync by addHandshake() persisting a record for every new capture.
    static constexpr uint8_t MF_AUTHORITATIVE = 0x01;

    /**
     * @brief Open the manifest, creating an empty one if missing.
     *
     * Validates the header on an existing file and reconciles the committed
     * count with the actual file size (drops a torn trailing append).
     * @return true on success; false on unrecoverable header corruption.
     */
    bool open(const char* path) {
        ready_ = false;
        count_ = 0;
        flags_ = 0;
        if (!path || !path[0]) return false;
        strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';

        // Recover an interrupted compaction (write-temp → swap). If the original
        // is gone but the temp survived, the crash landed mid-swap → promote the
        // temp. If both exist, the swap never started → the original is valid and
        // the temp is partial garbage → drop it.
        char tmpPath[96];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path_);
        if (!SD.exists(path_)) {
            if (SD.exists(tmpPath)) SD.rename(tmpPath, path_);
        } else if (SD.exists(tmpPath)) {
            SD.remove(tmpPath);
        }

        if (!SD.exists(path_)) {
            return create_();
        }

        File f = SD.open(path_, FILE_READ);
        if (!f) return false;

        ManifestHeader hdr;
        size_t got = f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
        uint32_t fileSize = (uint32_t)f.size();
        f.close();

        if (got != sizeof(hdr) || hdr.magic != MAGIC ||
            hdr.formatVersion != FORMAT_VERSION || hdr.recordSize != RECORD_SIZE) {
            return false;  // caller decides whether to rebuild from .pcap dir
        }

        // Reconcile committed count with what the file can actually hold.
        uint32_t derived = (fileSize >= HEADER_SIZE)
                               ? (fileSize - HEADER_SIZE) / RECORD_SIZE
                               : 0;
        count_ = (hdr.recordCount < derived) ? hdr.recordCount : derived;
        flags_ = hdr.flags;
        ready_ = true;
        return true;
    }

    bool isReady() const { return ready_; }
    uint32_t count() const { return count_; }

    /// True if the manifest fully indexes the .pcap dir (boot scan can be skipped).
    bool isAuthoritative() const { return ready_ && (flags_ & MF_AUTHORITATIVE); }

    /// Persist the authoritative flag (one byte) after a full reconcile.
    bool markAuthoritative() {
        if (!ready_) return false;
        if (flags_ & MF_AUTHORITATIVE) return true;
        uint8_t v = flags_ | MF_AUTHORITATIVE;
        File f = SD.open(path_, "r+");
        if (!f) return false;
        bool ok = f.seek(FLAGS_OFFSET) && f.write(&v, 1) == 1;
        f.close();
        if (ok) flags_ = v;
        return ok;
    }

    /**
     * @brief Rewrite the manifest dropping non-ACTIVE / corrupt records.
     *
     * Lossless for live data: every valid ACTIVE record is copied verbatim
     * (CRC seal preserved), header flags are preserved, dead/tombstoned/torn
     * slots are dropped. Record indices change, so callers must re-hydrate any
     * cached recIndex afterwards.
     *
     * Crash-safe: writes a temp file and swaps it in (remove + rename); open()
     * recovers either side of the swap, and the .pcap files remain the ultimate
     * ground truth. @return true on success (count_ refreshed to the live count).
     */
    bool compact() {
        if (!ready_) return false;

        char tmpPath[96];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path_);
        SD.remove(tmpPath);  // clear any stale temp

        // Pass 1: count survivors so the header can be written correct up front
        // (avoids a write-mode seek-back into the header).
        uint32_t live = 0;
        {
            File src = SD.open(path_, FILE_READ);
            if (!src) return false;
            bool ok = src.seek(HEADER_SIZE);
            HandshakeRecord rec;
            for (uint32_t i = 0; ok && i < count_; ++i) {
                if (src.read(reinterpret_cast<uint8_t*>(&rec), RECORD_SIZE) != RECORD_SIZE) break;
                if (rec.state == static_cast<uint8_t>(RecordState::ACTIVE) &&
                    handshakeRecordValid(rec)) {
                    live++;
                }
            }
            src.close();
        }

        // Pass 2: header + survivors → temp file.
        File src = SD.open(path_, FILE_READ);
        File dst = SD.open(tmpPath, FILE_WRITE);
        if (!src || !dst) {
            if (src) src.close();
            if (dst) dst.close();
            SD.remove(tmpPath);
            return false;
        }

        ManifestHeader hdr{};
        hdr.magic = MAGIC;
        hdr.formatVersion = FORMAT_VERSION;
        hdr.recordSize = RECORD_SIZE;
        hdr.recordCount = live;
        hdr.flags = flags_;  // preserve MF_AUTHORITATIVE
        bool ok = dst.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr) &&
                  src.seek(HEADER_SIZE);

        HandshakeRecord rec;
        for (uint32_t i = 0; ok && i < count_; ++i) {
            if (src.read(reinterpret_cast<uint8_t*>(&rec), RECORD_SIZE) != RECORD_SIZE) { ok = false; break; }
            if (rec.state != static_cast<uint8_t>(RecordState::ACTIVE) || !handshakeRecordValid(rec)) {
                continue;
            }
            if (dst.write(reinterpret_cast<const uint8_t*>(&rec), RECORD_SIZE) != RECORD_SIZE) ok = false;
        }
        src.close();
        dst.close();
        if (!ok) { SD.remove(tmpPath); return false; }

        // Swap temp over the original.
        if (!SD.remove(path_)) { SD.remove(tmpPath); return false; }
        if (!SD.rename(tmpPath, path_)) return false;  // open() recovers the temp

        return open(path_);  // refresh count_/flags_ from the compacted file
    }

    /// Read record `index`. @return true if read and structurally complete.
    bool readAt(uint32_t index, HandshakeRecord& out) const {
        if (!ready_ || index >= count_) return false;
        File f = SD.open(path_, FILE_READ);
        if (!f) return false;
        bool ok = f.seek(recordOffset_(index)) &&
                  f.read(reinterpret_cast<uint8_t*>(&out), RECORD_SIZE) == RECORD_SIZE;
        f.close();
        return ok;
    }

    /// Overwrite record `index` in place (re-seals version+CRC).
    bool writeAt(uint32_t index, HandshakeRecord rec) {
        if (!ready_ || index >= count_) return false;
        handshakeRecordSeal(rec);
        File f = SD.open(path_, "r+");  // read/update, no truncate, must exist
        if (!f) return false;
        bool ok = f.seek(recordOffset_(index)) &&
                  f.write(reinterpret_cast<const uint8_t*>(&rec), RECORD_SIZE) == RECORD_SIZE;
        f.close();
        return ok;
    }

    /**
     * @brief Append a record at the end and commit the new count.
     * @param outIndex receives the new record's index (optional).
     */
    /**
     * @brief Stream every record through a callback with a SINGLE file open.
     *
     * Hydration path: opening the file once and reading 256 B records back to
     * back avoids the per-record SD.open/close churn that fragments the heap at
     * boot. @p fn is invoked as fn(uint32_t index, const HandshakeRecord&).
     * @return false if the store isn't ready or the file can't be opened.
     */
    template <typename Fn>
    bool readEach(Fn&& fn) const {
        if (!ready_) return false;
        File f = SD.open(path_, FILE_READ);
        if (!f) return false;
        bool ok = f.seek(HEADER_SIZE);
        HandshakeRecord rec;
        for (uint32_t i = 0; ok && i < count_; ++i) {
            if (f.read(reinterpret_cast<uint8_t*>(&rec), RECORD_SIZE) != RECORD_SIZE) break;
            fn(i, static_cast<const HandshakeRecord&>(rec));
        }
        f.close();
        return ok;
    }

    bool append(HandshakeRecord rec, uint32_t* outIndex = nullptr) {
        if (!ready_) return false;
        handshakeRecordSeal(rec);
        File f = SD.open(path_, "r+");
        if (!f) return false;

        // 1) Write the record at end-of-file first (uncommitted until count bumps).
        bool ok = f.seek(recordOffset_(count_)) &&
                  f.write(reinterpret_cast<const uint8_t*>(&rec), RECORD_SIZE) == RECORD_SIZE;
        if (ok) {
            // 2) Commit by bumping the header count.
            uint32_t newCount = count_ + 1;
            ok = f.seek(COUNT_OFFSET) &&
                 f.write(reinterpret_cast<const uint8_t*>(&newCount), sizeof(newCount)) == sizeof(newCount);
        }
        f.close();
        if (!ok) return false;

        if (outIndex) *outIndex = count_;
        count_++;
        return true;
    }

private:
    static uint32_t recordOffset_(uint32_t index) {
        return HEADER_SIZE + index * RECORD_SIZE;
    }

    bool create_() {
        File f = SD.open(path_, FILE_WRITE);  // create/truncate
        if (!f) return false;
        ManifestHeader hdr{};
        hdr.magic = MAGIC;
        hdr.formatVersion = FORMAT_VERSION;
        hdr.recordSize = RECORD_SIZE;
        hdr.recordCount = 0;
        hdr.flags = 0;  // not yet reconciled with the .pcap dir
        bool ok = f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr);
        f.close();
        if (!ok) return false;
        count_ = 0;
        flags_ = 0;
        ready_ = true;
        return true;
    }

    char path_[80] = {0};
    uint32_t count_ = 0;
    uint8_t  flags_ = 0;
    bool ready_ = false;
};

} // namespace adversary
