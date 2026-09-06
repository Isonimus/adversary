/**
 * @file capture_registry.h
 * @brief Registry of captured handshakes with metadata cache
 * 
 * Scans capture directories at startup and caches handshake metadata
 * for fast access. Updated live when new captures are saved.
 */

#pragma once

#include <set>
#include <vector>
#include <cstring>
#include "handshake_metadata.h"
#include "handshake_record.h"
#include "utils/filename_utils.h"
#include <functional>

#ifndef ESP32
#include "../../test/common/arduino_mocks.h"
#include "manifest_store.h"
#endif

// Forward declaration to avoid circular include
namespace adversary {
enum class WpaSecStatus : uint8_t;
}

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "config/config.h"
#include "manifest_store.h"
#include "modules/system/time_manager.h"
#endif

namespace adversary {

/// recIndex sentinel: this summary has no backing manifest record yet
/// (bootstrap-(A) deferred entry — materialized on first status change).
static constexpr uint16_t NO_RECORD = 0xFFFF;

/// Boot-time compaction trigger: rewrite the manifest when at least this many
/// dead/tombstoned slots have accumulated (each wastes 256 B + a skipped read).
static constexpr uint32_t COMPACT_DEAD_THRESHOLD = 16;

// ============================================================================
// HandshakeMetadata <-> HandshakeRecord bridge
//
// The manifest record is the persistent on-disk form; HandshakeMetadata is the
// in-RAM/detail form the UI and services already speak. These pure converters
// keep the mapping in one place so the write path (capture-save) and the read
// path (getMetadata) can't drift. Service state (status/password/timestamps) is
// carried verbatim; capture facts map field-for-field.
// ============================================================================

/// Build an ACTIVE on-disk record from capture metadata (does not set pcapSize
/// or seal — the store seals on write; callers fill pcapSize from the artifact).
inline void recordFromMetadata(const HandshakeMetadata& m, HandshakeRecord& rec) {
    memset(&rec, 0, sizeof(rec));
    rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
    memcpy(rec.bssid, m.bssid, 6);
    strncpy(rec.ssid, m.ssid, sizeof(rec.ssid) - 1);
    rec.channel = m.channel;

    if (strcmp(m.type, "PMKID") == 0)      rec.type = static_cast<uint8_t>(RecordType::PMKID);
    else if (strcmp(m.type, "EAPOL") == 0) rec.type = static_cast<uint8_t>(RecordType::EAPOL);
    else                                   rec.type = static_cast<uint8_t>(RecordType::FOURWAY);

    uint8_t flags = 0;
    if (m.hasMsg1)            flags |= RF_HAS_MSG1;
    if (m.hasMsg2)            flags |= RF_HAS_MSG2;
    if (m.hasMsg3)            flags |= RF_HAS_MSG3;
    if (m.hasMsg4)            flags |= RF_HAS_MSG4;
    if (m.hasPMKID)            flags |= RF_HAS_PMKID;
    if (m.hasGPS)              flags |= RF_HAS_GPS;
    if (m.wpaSecPassword[0])   flags |= RF_HAS_PASSWORD;
    if (m.pwncrackPassword[0]) flags |= RF_HAS_PC_PASSWORD;
    rec.flags = flags;

    rec.quality = m.quality;
    rec.rssi = m.signalStrength;
    rec.capturedAt = m.capturedAt;

    rec.wpaSecStatus = static_cast<uint8_t>(m.wpaSecStatus);
    rec.wpaSecUploadedAt = m.wpaSecUploadedAt;
    rec.wpaSecCrackedAt = m.wpaSecCrackedAt;
    strncpy(rec.wpaSecPassword, m.wpaSecPassword, sizeof(rec.wpaSecPassword) - 1);

    rec.pwncrackStatus = static_cast<uint8_t>(m.pwncrackStatus);
    rec.pwncrackUploadedAt = m.pwncrackUploadedAt;
    rec.pwncrackCrackedAt = m.pwncrackCrackedAt;
    strncpy(rec.pwncrackPassword, m.pwncrackPassword, sizeof(rec.pwncrackPassword) - 1);

    rec.flags2 = m.has22000 ? RF2_HAS_HC22000 : 0;

    if (m.hasGPS) {
        rec.lat = m.latitude;
        rec.lon = m.longitude;
        rec.alt = m.altitude;
        rec.sats = m.satellites;
    }
}

/// Hydrate detail metadata from an on-disk record (read path).
inline void metadataFromRecord(const HandshakeRecord& rec, HandshakeMetadata& m) {
    m = HandshakeMetadata();  // reset to defaults
    strncpy(m.ssid, rec.ssid, sizeof(m.ssid) - 1);
    memcpy(m.bssid, rec.bssid, 6);
    m.channel = rec.channel;

    switch (static_cast<RecordType>(rec.type)) {
        case RecordType::PMKID: strncpy(m.type, "PMKID", sizeof(m.type) - 1); break;
        case RecordType::EAPOL: strncpy(m.type, "EAPOL", sizeof(m.type) - 1); break;
        default:                strncpy(m.type, "4WAY",  sizeof(m.type) - 1); break;
    }

    m.capturedAt = rec.capturedAt;
    m.hasMsg1  = (rec.flags & RF_HAS_MSG1)  != 0;
    m.hasMsg2  = (rec.flags & RF_HAS_MSG2)  != 0;
    m.hasMsg3  = (rec.flags & RF_HAS_MSG3)  != 0;
    m.hasMsg4  = (rec.flags & RF_HAS_MSG4)  != 0;
    m.hasPMKID = (rec.flags & RF_HAS_PMKID) != 0;
    m.quality = rec.quality;
    m.signalStrength = rec.rssi;

    m.wpaSecStatus = static_cast<WpaSecStatus>(rec.wpaSecStatus);
    m.wpaSecUploadedAt = rec.wpaSecUploadedAt;
    m.wpaSecCrackedAt = rec.wpaSecCrackedAt;
    strncpy(m.wpaSecPassword, rec.wpaSecPassword, sizeof(m.wpaSecPassword) - 1);

    m.pwncrackStatus = static_cast<WpaSecStatus>(rec.pwncrackStatus);
    m.pwncrackUploadedAt = rec.pwncrackUploadedAt;
    m.pwncrackCrackedAt = rec.pwncrackCrackedAt;
    strncpy(m.pwncrackPassword, rec.pwncrackPassword, sizeof(m.pwncrackPassword) - 1);
    m.has22000 = (rec.flags2 & RF2_HAS_HC22000) != 0;

    if (rec.flags & RF_HAS_GPS) {
        m.hasGPS = true;
        m.latitude = rec.lat;
        m.longitude = rec.lon;
        m.altitude = rec.alt;
        m.satellites = rec.sats;
    }
}

/**
 * @brief Lightweight handshake summary — the in-RAM manifest index entry
 *
 * Backs both the firmware Captures list and the GUI list endpoint. Holds just
 * enough to render a row and to locate the full record: SSID (display) + BSSID
 * (primary key) + the manifest record index for O(1) seek-based updates.
 * Filename is derived from SSID; the full record is read on demand.
 *
 * Memory: ~46 bytes per entry.
 */
struct HandshakeSummary {
    char ssid[33];                ///< SSID (display identifier)
    uint32_t size;                ///< File size in bytes
    WpaSecStatus wpaSecStatus;    ///< WPA-SEC status (1 byte)
    WpaSecStatus pwncrackStatus;  ///< pwncrack status (1 byte, enum reused)
    bool hasGPS = false;          ///< RF_HAS_GPS flag (for the list GPS column)
    bool has22000 = false;        ///< RF2_HAS_HC22000 (capture is pwncrack-uploadable)
    uint8_t bssid[6];             ///< Primary key (0 until known — bootstrap A)
    uint16_t recIndex;            ///< Manifest record index, or NO_RECORD

    HandshakeSummary() : size(0), wpaSecStatus(static_cast<WpaSecStatus>(0)),
                         pwncrackStatus(static_cast<WpaSecStatus>(0)), recIndex(NO_RECORD) {
        memset(ssid, 0, sizeof(ssid));
        memset(bssid, 0, sizeof(bssid));
    }

    HandshakeSummary(const char* s, uint32_t sz = 0, WpaSecStatus status = static_cast<WpaSecStatus>(0),
                     WpaSecStatus pcStatus = static_cast<WpaSecStatus>(0))
        : size(sz), wpaSecStatus(status), pwncrackStatus(pcStatus), recIndex(NO_RECORD) {
        memset(ssid, 0, sizeof(ssid));
        memset(bssid, 0, sizeof(bssid));
        if (s) strncpy(ssid, s, sizeof(ssid) - 1);
    }

    /// Derive filename from SSID (e.g., "MyNetwork" -> "MyNetwork.pcap")
    void getFilename(char* output, size_t outputLen) const {
        filename_utils::getHandshakeFilename(ssid, output, outputLen);
    }
};

/**
 * @brief Singleton registry of captured handshakes with lazy metadata loading
 * 
 * Pre-loads only filenames at startup for minimal memory usage.
 * Full metadata is loaded on-demand via sliding window cache.
 */
class CaptureRegistry {
public:
    static CaptureRegistry& getInstance() {
        static CaptureRegistry instance;
        return instance;
    }
    
    /**
     * @brief Scan all capture directories and populate caches
     * Call once during splash screen init
     */
    void scanAll();
    
    /**
     * @brief Aggressively clear ALL internal caches, lists, and SSID sets to free memory
     * Call before starting memory-intensive tasks (BLE init, SSL).
     */
    void clearAll();
    
    /// Callback when an action item is selected
    using ProgressCallback = std::function<void(int count, const char* label)>;
    
    /**
     * @brief Reload handshake list (call after add/delete)
     */
    void rescan() { clearAll(); scanAll(); }
    
    /**
     * @brief Set progress callback for scanning operations
     */
    void setProgressCallback(ProgressCallback cb) { progressCb_ = cb; }
    
    /**
     * @brief Check if handshake exists for SSID
     */
    bool hasHandshake(const char* ssid) const;
    
    /**
     * @brief Check if credentials exist for SSID
     */
    bool hasCredentials(const char* ssid) const;
    
    /**
     * @brief Add handshake and update cache
     * @param size File size in bytes (for instant list loading)
     * @param status WPA-SEC status
     */
    void addHandshake(const char* ssid, 
                      const HandshakeMetadata* metadata = nullptr,
                      uint32_t size = 0,
                      WpaSecStatus status = WpaSecStatus::NOT_UPLOADED);
    
    /**
     * @brief Add credentials SSID (call when capture saves)
     */
    void addCredentials(const char* ssid);
    
    /**
     * @brief Remove handshake from cache (call when deleted)
     */
    void removeHandshake(const char* ssid);
    
    /**
     * @brief Get counts for display
     */
    size_t getHandshakeCount() const { return summaries_.size(); }
    size_t getCredentialCount() const { return credentialSSIDs_.size(); }
    
    // ========================================================================
    // NEW: Lightweight summaries API (recommended)
    // ========================================================================
    
    /**
     * @brief Get all handshake summaries (lightweight, for list display)
     */
    const std::vector<HandshakeSummary>& getHandshakeSummaries() const { return summaries_; }
    
    /**
     * @brief Clear summaries to free memory before SSL operations
     * @deprecated Removed in Part 3 to prevent heap fragmentation gaps. Use clearAll() for full purge.
     */
    void clearSummariesForSSL() {
        // No-op to prevent fragmentation gaps
    }
    
    /**
     * @brief Rescan summaries from SD after SSL (restores what clearSummariesForSSL freed)
     * @note Only scans if currently empty.
     */
    void rescanSummaries() {
        if (summaries_.empty()) {
            hydrateAndReconcile();
        }
    }
    
    /**
     * @brief Get metadata by filename (lazy loads from SD if not cached)
     * @return Pointer to metadata (valid until cache eviction) or nullptr on error
     */
    const HandshakeMetadata* getMetadata(const char* filename);
    
    /**
     * @brief Find filename by SSID (for wpasec password matching)
     * @return Filename if found, empty string if not
     */
    const char* findFilenameBySSID(const char* ssid) const;
    
    /**
     * @brief Clear the on-demand metadata read slot (call before SSL/BLE tasks)
     * @note Manifest reads are fresh, so this is just freeing the small buffer.
     */
    void invalidateCache(const char* /*filename*/ = nullptr) { metadataSlot_ = HandshakeMetadata(); }

    /**
     * @brief Update WPA-SEC status for a handshake (updates summary and saves to binary file)
     */
    void setWpaSecStatus(const char* ssid, WpaSecStatus status);

    /**
     * @brief Mark a capture CRACKED and store its password in the manifest.
     *
     * Matches by BSSID first (robust to emoji/odd SSIDs the WPA-SEC reply keys
     * by ap_bssid), then falls back to SSID. Writes status + password into the
     * record in one in-place seek+write; materializes a thin record if the
     * entry was still deferred.
     * @return true if a matching capture was found and persisted.
     */
    bool setCrackedPassword(const uint8_t bssid[6], const char* ssid, const char* password);

    /**
     * @brief Update pwncrack status for a handshake (RAM + in-place manifest write).
     *        Parallel to setWpaSecStatus(); keyed by SSID.
     */
    void setPwncrackStatus(const char* ssid, WpaSecStatus status);

    /**
     * @brief Mark a capture CRACKED by pwncrack and store its password.
     *        Parallel to setCrackedPassword() (BSSID match first, then SSID).
     * @return true if a matching capture was found and persisted.
     */
    bool setPwncrackCrackedPassword(const uint8_t bssid[6], const char* ssid, const char* password);

    /**
     * @brief Re-sync the manifest with the actual .pcap files on the SD card.
     *
     * For out-of-band SD edits (offload/prune captures on a computer, reinsert
     * the card): the manifest is authoritative so it otherwise won't notice.
     * Prunes records whose .pcap is gone, indexes .pcap the manifest doesn't
     * know about, compacts, and re-marks authoritative. WPA-SEC status/passwords
     * for surviving captures are preserved (it is NOT a manifest wipe).
     * @return the resulting handshake count. ESP32 only.
     */
    int rebuildIndex();

private:
    CaptureRegistry() = default;
    CaptureRegistry(const CaptureRegistry&) = delete;
    CaptureRegistry& operator=(const CaptureRegistry&) = delete;
    
    void scanHandshakesDirectory();
    void scanCredentialsDirectory();
    String extractSSIDFromFilename(const char* filename) const;

    // Manifest (single source of truth for WPA-SEC service state).
    bool openManifest();          // Open/create manifest.bin (self-heals bad header)
    int  loadFromManifest();      // Hydrate summaries_ from ACTIVE records; returns count
    void hydrateAndReconcile();   // Hydrate, and (once) index orphan .pcap into manifest
    int  persistDeferredOrphans();// Append a thin record for each RAM-only summary
    bool persistStatus(HandshakeSummary& summary, WpaSecStatus status);
    bool persistRecordFromMetadata(HandshakeSummary& summary, const HandshakeMetadata& m);

    // Fast SSID lookup sets - DEPRECATED: removed to save heap.
    // Use linear searches in summaries_ instead (fast enough for <200 entries).
    std::vector<String> credentialSSIDs_;

    // Lightweight summaries (filename + ssid only)
    std::vector<HandshakeSummary> summaries_;

    // On-demand metadata read slot. getMetadata() reads the manifest record
    // fresh into here and returns &metadataSlot_; the pointer is valid until the
    // next getMetadata() call (all callers consume it before calling again).
    mutable HandshakeMetadata metadataSlot_;

    // BSSID-keyed manifest backing the summaries_ index.
    ManifestStore manifest_;
    bool manifestReady_ = false;

    ProgressCallback progressCb_;
};

// ============================================================================
// Inline Implementations
// ============================================================================

inline void CaptureRegistry::clearAll() {
    credentialSSIDs_.clear();
    credentialSSIDs_.shrink_to_fit();
    summaries_.clear();
    summaries_.shrink_to_fit();
    metadataSlot_ = HandshakeMetadata();
    Serial.println("[CaptureRegistry] All SSIDs and summaries cleared (RAM freed)");
}

inline void CaptureRegistry::scanAll() {
#ifdef ESP32
    Serial.println("[CaptureRegistry] Scanning capture directories...");
    uint32_t start = millis();
    
    clearAll();

    // Manifest first: it is the source of truth for persisted WPA-SEC state and
    // BSSID keys. Hydrates summaries_, and only scans the .pcap dir when the
    // manifest doesn't yet fully index it (then marks it authoritative so future
    // boots skip the slow scan entirely).
    hydrateAndReconcile();

    // Scan credentials (just SSIDs)
    scanCredentialsDirectory();

    uint32_t elapsed = millis() - start;
    Serial.printf("[CaptureRegistry] Found %zu handshakes, %zu credentials in %lu ms\n",
                  summaries_.size(), credentialSSIDs_.size(), elapsed);
#endif
}

inline void CaptureRegistry::scanHandshakesDirectory() {
#ifdef ESP32
    File dir = SD.open(config::SD_HANDSHAKES_PATH);
    if (!dir || !dir.isDirectory()) {
        Serial.printf("[CaptureRegistry] Cannot open: %s\n", config::SD_HANDSHAKES_PATH);
        return;
    }
    
    File entry;
    int scanned = 0;  // .pcap files iterated (drives progress; most are already known)
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            size_t len = strlen(name);

            // Only process .pcap files (fastest check first)
            if (len > 5 && strcmp(name + len - 5, ".pcap") == 0) {
                String ssid = extractSSIDFromFilename(name);

                // Capture size (one call only)
                uint32_t fileSize = entry.size();

                // Already represented by a manifest record? Just refresh its
                // cached size and skip (avoids duplicate rows).
                bool known = false;
                for (auto& s : summaries_) {
                    if (strcmp(s.ssid, ssid.c_str()) == 0) {
                        s.size = fileSize;
                        known = true;
                        break;
                    }
                }

                // Deferred entry: no manifest record yet (bssid=0 / NOT_UPLOADED).
                if (!known) {
                    summaries_.emplace_back(ssid.c_str(), fileSize, WpaSecStatus::NOT_UPLOADED);
                }

                // Progress is driven off files iterated, not just newly-added
                // entries: with the manifest pre-hydrated, most files are already
                // known and add nothing, so a "new entries" counter would appear
                // frozen for the whole (slow) FAT scan. Tracking the running total
                // keeps the number climbing the whole time.
                if (progressCb_ && (++scanned % 10 == 0)) {
                    char label[32];
                    snprintf(label, sizeof(label), "Handshakes: %zu", summaries_.size());
                    progressCb_((int)summaries_.size(), label);
                }
            }
        }
        entry.close();
    }

    if (progressCb_) {
        char label[32];
        snprintf(label, sizeof(label), "Handshakes: %zu", summaries_.size());
        progressCb_((int)summaries_.size(), label);
    }

    dir.close();
#endif
}

/**
 * @brief Load metadata on-demand with caching
 */
inline const HandshakeMetadata* CaptureRegistry::getMetadata(const char* filename) {
#ifdef ESP32
    if (!filename) return nullptr;

    // Manifest is the single source of truth. Find the summary for this file and
    // read its record fresh (256 B seek+read, no JSON, no heap) into the shared
    // read slot — reading fresh also sidesteps the cache-staleness bug that
    // motivated the rework. The returned pointer is valid until the next call.
    String ssid = extractSSIDFromFilename(filename);
    for (auto& s : summaries_) {
        if (strcmp(s.ssid, ssid.c_str()) != 0) continue;
        if (s.recIndex != NO_RECORD) {
            HandshakeRecord rec;
            if (manifest_.readAt(s.recIndex, rec) && handshakeRecordValid(rec)) {
                metadataFromRecord(rec, metadataSlot_);
                return &metadataSlot_;
            }
        }
        break;  // matched the summary but it has no record yet (bootstrap-deferred)
    }
    return nullptr;
#else
    (void)filename;
    return nullptr;
#endif
}

/**
 * @brief Get filename for SSID (derived, not stored)
 * @note Since HandshakeSummary no longer stores filename, use getHandshakeFilename() directly
 */
inline const char* CaptureRegistry::findFilenameBySSID(const char* ssid) const {
    (void)ssid;
    // DEPRECATED: Filename is now derived from SSID via filename_utils::getHandshakeFilename()
    // Callers should use the getFilename() method on HandshakeSummary instead
    return nullptr;
}

inline void CaptureRegistry::scanCredentialsDirectory() {
#ifdef ESP32
    File dir = SD.open(config::SD_CREDENTIALS_PATH);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    
    File entry;
    int count = 0;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            size_t len = strlen(name);
            
            // Process .json credential files
            if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
                String ssid = extractSSIDFromFilename(name);
                if (ssid.length() > 0) {
                    credentialSSIDs_.push_back(ssid);
                    count++;
                    
                    // Update progress every 5 items
                    if (progressCb_ && (count % 5 == 0)) {
                        char label[32];
                        snprintf(label, sizeof(label), "Credentials: %d", count);
                        progressCb_(summaries_.size() + count, label);
                    }
                }
            }
        }
        entry.close();
    }
    
    if (progressCb_) {
        char label[32];
        snprintf(label, sizeof(label), "Credentials: %d", count);
        progressCb_(summaries_.size() + count, label);
    }
    
    dir.close();
#endif
}

inline String CaptureRegistry::extractSSIDFromFilename(const char* filename) const {
#ifdef ESP32
    // Filename formats:
    // Handshakes: SSID_millis.pcap (new format)
    // Credentials: SSID_credentials.json
    
    String fname(filename);

    // For credentials: remove _credentials.json suffix
    int credIdx = fname.indexOf("_credentials.json");
    if (credIdx > 0) {
        return fname.substring(0, credIdx);
    }

    // Handshakes are named {sanitizedSSID}.pcap — the stem IS the SSID, so just
    // strip the extension. (Do NOT strip a trailing _<digits>: the old
    // {SSID}_{millis}.pcap scheme is gone, and doing so corrupted SSIDs that
    // legitimately end in _<digits> like "MOVISTAR_3483" — breaking the
    // filename<->SSID round-trip, the scan dedup check, getMetadata lookups, and
    // churning phantom records through rebuildIndex.)
    int dotIdx = fname.lastIndexOf('.');
    if (dotIdx <= 0) {
        return fname;
    }
    return fname.substring(0, dotIdx);
#else
    (void)filename;
    return "";
#endif
}

inline bool CaptureRegistry::hasHandshake(const char* ssid) const {
    if (!ssid || ssid[0] == '\0') return false;
    for (const auto& s : summaries_) {
        if (strcmp(s.ssid, ssid) == 0) return true;
    }
    return false;
}

inline bool CaptureRegistry::hasCredentials(const char* ssid) const {
    if (!ssid || ssid[0] == '\0') return false;
    for (const auto& s : credentialSSIDs_) {
        if (s == ssid) return true;
    }
    return false;
}

inline void CaptureRegistry::addHandshake(const char* ssid, 
                                           const HandshakeMetadata* metadata,
                                           uint32_t size,
                                           WpaSecStatus status) {
    if (!ssid || ssid[0] == '\0') return;

    // Upsert the summary by SSID: a re-capture updates the existing row (and its
    // backing record) in place rather than creating a duplicate, and a deferred
    // bootstrap entry gets materialized here.
    HandshakeSummary* summary = nullptr;
    for (auto& s : summaries_) {
        if (strcmp(s.ssid, ssid) == 0) { summary = &s; break; }
    }
    if (summary) {
        if (size) summary->size = size;
        summary->wpaSecStatus = status;
        if (metadata) { summary->hasGPS = metadata->hasGPS; summary->has22000 = metadata->has22000; }
    } else {
        summaries_.emplace_back(ssid, size, status);
        summary = &summaries_.back();
        if (metadata) { summary->hasGPS = metadata->hasGPS; summary->has22000 = metadata->has22000; }
    }

#ifdef ESP32
    // Persist a full BSSID-bearing record to the manifest (single source of
    // truth): capture facts (channel/type/flags/GPS) + service state. A
    // re-capture overwrites the existing slot in place (no duplicate, no rewrite
    // of the whole file).
    if (metadata && manifestReady_) {
        memcpy(summary->bssid, metadata->bssid, 6);
        persistRecordFromMetadata(*summary, *metadata);
    }
    Serial.printf("[CaptureRegistry] Added handshake: %s\n", ssid);
#endif
}

inline void CaptureRegistry::addCredentials(const char* ssid) {
    if (ssid && ssid[0] != '\0') {
        credentialSSIDs_.push_back(ssid);
#ifdef ESP32
        Serial.printf("[CaptureRegistry] Added credentials: %s\n", ssid);
#endif
    }
}

inline void CaptureRegistry::removeHandshake(const char* ssid) {
    if (!ssid) return;
    
#ifdef ESP32
    // Find and remove from summaries by ssid
    for (auto it = summaries_.begin(); it != summaries_.end(); ++it) {
        if (strcmp(it->ssid, ssid) == 0) {
            // Tombstone the backing record so its slot/offset stays stable for
            // the others (lazy compaction handles pile-up later).
            if (manifestReady_ && it->recIndex != NO_RECORD) {
                HandshakeRecord rec;
                if (manifest_.readAt(it->recIndex, rec)) {
                    rec.state = static_cast<uint8_t>(RecordState::TOMBSTONE);
                    manifest_.writeAt(it->recIndex, rec);
                }
            }
            summaries_.erase(it);
            Serial.printf("[CaptureRegistry] Removed handshake: %s\n", ssid);
            return;
        }
    }
#else
    (void)ssid;
#endif
}

// ============================================================================
// Manifest-backed status persistence (manifest.bin)
//
// Replaces the old wpasec_status.bin (which was fully rewritten on every
// change). WPA-SEC status now lives in the BSSID-keyed manifest and is updated
// with a single in-place seek+write — no full-file rewrite, no JSON, ~256 B of
// stack. See docs/DESIGN-storage-rework.md.
// ============================================================================

inline bool CaptureRegistry::openManifest() {
#ifdef ESP32
    static char manifestPath[96];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.bin", config::SD_HANDSHAKES_PATH);

    manifestReady_ = manifest_.open(manifestPath);
    if (!manifestReady_) {
        // Bad header / corrupt: discard and recreate. No migration — the
        // .pcap files remain the ground truth; statuses repopulate on re-upload.
        Serial.println("[CaptureRegistry] manifest.bin invalid; recreating");
        SD.remove(manifestPath);
        manifestReady_ = manifest_.open(manifestPath);
    }
    return manifestReady_;
#else
    return false;
#endif
}

inline int CaptureRegistry::loadFromManifest() {
#ifdef ESP32
    if (!manifestReady_) return 0;
    uint32_t n = manifest_.count();

    // Reserve up front so the bulk hydrate doesn't repeatedly grow+realloc the
    // vector (each realloc leaves a fragmentation gap). The +16 covers the few
    // deferred .pcap entries scanHandshakesDirectory() appends afterwards.
    summaries_.reserve(n + 16);

    int loaded = 0;
    // Single file open for the whole hydrate (was one SD.open per record).
    manifest_.readEach([&](uint32_t i, const HandshakeRecord& rec) {
        if (!handshakeRecordValid(rec)) return;
        if (rec.state != static_cast<uint8_t>(RecordState::ACTIVE)) return;  // skip tombstones

        HandshakeSummary s(rec.ssid, rec.pcapSize, static_cast<WpaSecStatus>(rec.wpaSecStatus),
                           static_cast<WpaSecStatus>(rec.pwncrackStatus));
        memcpy(s.bssid, rec.bssid, 6);
        s.hasGPS = (rec.flags & RF_HAS_GPS) != 0;
        s.has22000 = (rec.flags2 & RF2_HAS_HC22000) != 0;
        s.recIndex = static_cast<uint16_t>(i);
        summaries_.push_back(s);
        loaded++;
    });
    Serial.printf("[CaptureRegistry] Hydrated %d records from manifest.bin (%u total)\n", loaded, n);
    return loaded;
#else
    return 0;
#endif
}

inline void CaptureRegistry::hydrateAndReconcile() {
#ifdef ESP32
    openManifest();
    int loaded = loadFromManifest();

    // Reclaim dead/tombstoned slots once they pile up. Lossless: every live
    // record (status/password) is copied; only dead/torn slots are dropped.
    // recIndex changes, so re-hydrate summaries_ from the compacted file.
    uint32_t total = manifest_.count();
    uint32_t dead = (total > (uint32_t)loaded) ? total - (uint32_t)loaded : 0;
    if (dead >= COMPACT_DEAD_THRESHOLD && manifest_.compact()) {
        Serial.printf("[CaptureRegistry] Compacted manifest: dropped %u dead (%u -> %u records)\n",
                      dead, total, manifest_.count());
        summaries_.clear();
        loaded = loadFromManifest();
    }
    (void)loaded;

    // Show the hydrated count straight away (boot splash) — when the dir scan
    // below runs it is slow, so without this the progress would sit at 0.
    if (progressCb_ && !summaries_.empty()) {
        char label[32];
        snprintf(label, sizeof(label), "Handshakes: %zu", summaries_.size());
        progressCb_((int)summaries_.size(), label);
    }

    if (manifest_.isAuthoritative()) {
        // The manifest already indexes every .pcap — skip the ~19s FAT scan.
        Serial.printf("[CaptureRegistry] Manifest authoritative (%zu); skipping .pcap dir scan\n",
                      summaries_.size());
        return;
    }

    // First run (or a freshly recreated manifest): index any .pcap the manifest
    // doesn't know about, persist a thin record for each, then mark the manifest
    // authoritative so subsequent boots are instant. addHandshake() keeps the
    // invariant afterwards by persisting a record for every new capture.
    scanHandshakesDirectory();
    int orphans = persistDeferredOrphans();
    if (manifest_.markAuthoritative()) {
        Serial.printf("[CaptureRegistry] Reconciled %d orphan .pcap into manifest; now authoritative\n",
                      orphans);
    }
#endif
}

inline int CaptureRegistry::persistDeferredOrphans() {
#ifdef ESP32
    if (!manifestReady_) return 0;
    int n = 0;
    for (auto& s : summaries_) {
        if (s.recIndex != NO_RECORD) continue;  // already backed by a record

        // Thin bootstrap record: the .pcap exists but its metadata (bssid,
        // channel, msg flags) is unknown until a re-capture enriches it. This is
        // what turns a RAM-only deferred entry into something the manifest can
        // own, so the next boot can trust the manifest and skip the dir scan.
        HandshakeRecord rec{};
        rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
        memcpy(rec.bssid, s.bssid, 6);  // 0 for orphans
        strncpy(rec.ssid, s.ssid, sizeof(rec.ssid) - 1);
        rec.type = static_cast<uint8_t>(RecordType::FOURWAY);
        rec.pcapSize = s.size;
        rec.wpaSecStatus = static_cast<uint8_t>(s.wpaSecStatus);

        uint32_t idx = 0;
        if (manifest_.append(rec, &idx)) {
            s.recIndex = static_cast<uint16_t>(idx);
            n++;
        }
    }
    return n;
#else
    return 0;
#endif
}

inline int CaptureRegistry::rebuildIndex() {
#ifdef ESP32
    Serial.println("[CaptureRegistry] rebuildIndex: re-syncing manifest with .pcap dir");

    clearAll();
    openManifest();
    loadFromManifest();

    // 1. Prune records whose .pcap is gone (deleted out-of-band). Tombstone the
    //    backing record and drop the row; service state for survivors is kept.
    int pruned = 0;
    for (auto it = summaries_.begin(); it != summaries_.end();) {
        char path[128];
        filename_utils::getHandshakePath(it->ssid, path, sizeof(path));
        if (!SD.exists(path)) {
            if (it->recIndex != NO_RECORD) {
                HandshakeRecord rec;
                if (manifest_.readAt(it->recIndex, rec)) {
                    rec.state = static_cast<uint8_t>(RecordState::TOMBSTONE);
                    manifest_.writeAt(it->recIndex, rec);
                }
            }
            it = summaries_.erase(it);
            pruned++;
        } else {
            ++it;
        }
    }

    // 2. Index any .pcap the manifest doesn't know about (added out-of-band) and
    //    persist a thin record for each.
    scanHandshakesDirectory();
    int added = persistDeferredOrphans();

    // 3. Reclaim the tombstones we just made, then (re)assert authority.
    if (pruned > 0 && manifest_.compact()) {
        summaries_.clear();
        loadFromManifest();
    }
    manifest_.markAuthoritative();

    Serial.printf("[CaptureRegistry] rebuildIndex: +%d added, -%d pruned, %zu total\n",
                  added, pruned, summaries_.size());
    return static_cast<int>(summaries_.size());
#else
    return 0;
#endif
}

inline bool CaptureRegistry::persistStatus(HandshakeSummary& summary, WpaSecStatus status) {
#ifdef ESP32
    if (!manifestReady_) return false;

    if (summary.recIndex != NO_RECORD) {
        // In-place update of the existing record (the hot path).
        HandshakeRecord rec;
        if (!manifest_.readAt(summary.recIndex, rec)) return false;
        rec.wpaSecStatus = static_cast<uint8_t>(status);
        return manifest_.writeAt(summary.recIndex, rec);
    }

    // Deferred entry's first state change: materialize a thin record. Capture
    // facts (channel/type/GPS) fill in later on re-capture; bssid stays 0 until
    // known (bootstrap A).
    HandshakeRecord rec{};
    rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
    memcpy(rec.bssid, summary.bssid, 6);
    strncpy(rec.ssid, summary.ssid, sizeof(rec.ssid) - 1);
    rec.type = static_cast<uint8_t>(RecordType::FOURWAY);
    rec.pcapSize = summary.size;
    rec.wpaSecStatus = static_cast<uint8_t>(status);

    uint32_t idx = 0;
    if (!manifest_.append(rec, &idx)) return false;
    summary.recIndex = static_cast<uint16_t>(idx);
    return true;
#else
    (void)summary; (void)status;
    return false;
#endif
}

inline bool CaptureRegistry::persistRecordFromMetadata(HandshakeSummary& summary,
                                                       const HandshakeMetadata& m) {
#ifdef ESP32
    if (!manifestReady_) return false;

    HandshakeRecord rec;
    recordFromMetadata(m, rec);
    rec.pcapSize = summary.size;

    if (summary.recIndex != NO_RECORD) {
        // Overwrite the existing slot in place (the hot path for re-capture).
        bool ok = manifest_.writeAt(summary.recIndex, rec);
        Serial.printf("[CaptureRegistry] Record updated idx=%u ssid=%s ch=%u type=%u\n",
                      summary.recIndex, rec.ssid, rec.channel, rec.type);
        return ok;
    }

    // First persistence for this capture: append and remember the slot.
    uint32_t idx = 0;
    if (!manifest_.append(rec, &idx)) return false;
    summary.recIndex = static_cast<uint16_t>(idx);
    Serial.printf("[CaptureRegistry] Record appended idx=%u ssid=%s ch=%u type=%u\n",
                  idx, rec.ssid, rec.channel, rec.type);
    return true;
#else
    (void)summary; (void)m;
    return false;
#endif
}

inline void CaptureRegistry::setWpaSecStatus(const char* ssid, WpaSecStatus status) {
    if (!ssid) return;

    for (auto& summary : summaries_) {
        if (strcmp(summary.ssid, ssid) == 0) {
            summary.wpaSecStatus = status;  // RAM (drives list + getStatus)
#ifdef ESP32
            persistStatus(summary, status); // single in-place manifest write
#endif
            return;
        }
    }
}

inline bool CaptureRegistry::setCrackedPassword(const uint8_t bssid[6],
                                                const char* ssid,
                                                const char* password) {
#ifdef ESP32
    if (!ssid || !password) return false;

    auto nonZeroMac = [](const uint8_t* m) {
        return m && (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) != 0;
    };
    bool bssidKnown = nonZeroMac(bssid);

    // Prefer a BSSID match (the WPA-SEC reply is keyed by ap_bssid); fall back to
    // SSID for entries whose BSSID isn't known yet (bootstrap-deferred records).
    HandshakeSummary* summary = nullptr;
    if (bssidKnown) {
        for (auto& s : summaries_) {
            if (nonZeroMac(s.bssid) && memcmp(s.bssid, bssid, 6) == 0) { summary = &s; break; }
        }
    }
    if (!summary) {
        for (auto& s : summaries_) {
            if (strcmp(s.ssid, ssid) == 0) { summary = &s; break; }
        }
    }
    if (!summary) {
        Serial.printf("[CaptureRegistry] Cracked result unmatched: %s\n", ssid);
        return false;
    }

    summary->wpaSecStatus = WpaSecStatus::CRACKED;
    if (bssidKnown) memcpy(summary->bssid, bssid, 6);  // backfill learned BSSID

    // Read the existing record, or materialize a thin one if still deferred.
    HandshakeRecord rec;
    bool haveRecord = (summary->recIndex != NO_RECORD) &&
                      manifest_.readAt(summary->recIndex, rec);
    if (!haveRecord) {
        memset(&rec, 0, sizeof(rec));
        rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
        memcpy(rec.bssid, bssidKnown ? bssid : summary->bssid, 6);
        strncpy(rec.ssid, summary->ssid, sizeof(rec.ssid) - 1);
        rec.type = static_cast<uint8_t>(RecordType::FOURWAY);
        rec.pcapSize = summary->size;
    }

    rec.wpaSecStatus = static_cast<uint8_t>(WpaSecStatus::CRACKED);
    strncpy(rec.wpaSecPassword, password, sizeof(rec.wpaSecPassword) - 1);
    rec.wpaSecPassword[sizeof(rec.wpaSecPassword) - 1] = '\0';
    rec.flags |= RF_HAS_PASSWORD;
    rec.wpaSecCrackedAt = TimeManager::getInstance().isSynced()
                              ? (uint32_t)TimeManager::getInstance().now() : 0;

    if (summary->recIndex != NO_RECORD) {
        return manifest_.writeAt(summary->recIndex, rec);
    }
    uint32_t idx = 0;
    if (!manifest_.append(rec, &idx)) return false;
    summary->recIndex = static_cast<uint16_t>(idx);
    return true;
#else
    (void)bssid; (void)ssid; (void)password;
    return false;
#endif
}

inline void CaptureRegistry::setPwncrackStatus(const char* ssid, WpaSecStatus status) {
    if (!ssid) return;

    for (auto& summary : summaries_) {
        if (strcmp(summary.ssid, ssid) != 0) continue;
        summary.pwncrackStatus = status;  // RAM (drives list badge)
#ifdef ESP32
        if (!manifestReady_) return;
        if (summary.recIndex != NO_RECORD) {
            HandshakeRecord rec;
            if (manifest_.readAt(summary.recIndex, rec)) {
                rec.pwncrackStatus = static_cast<uint8_t>(status);
                manifest_.writeAt(summary.recIndex, rec);
            }
        } else {
            // Materialize a thin record on first pwncrack state change.
            HandshakeRecord rec{};
            rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
            memcpy(rec.bssid, summary.bssid, 6);
            strncpy(rec.ssid, summary.ssid, sizeof(rec.ssid) - 1);
            rec.type = static_cast<uint8_t>(RecordType::FOURWAY);
            rec.pcapSize = summary.size;
            rec.pwncrackStatus = static_cast<uint8_t>(status);
            uint32_t idx = 0;
            if (manifest_.append(rec, &idx)) summary.recIndex = static_cast<uint16_t>(idx);
        }
#endif
        return;
    }
}

inline bool CaptureRegistry::setPwncrackCrackedPassword(const uint8_t bssid[6],
                                                        const char* ssid,
                                                        const char* password) {
#ifdef ESP32
    if (!ssid || !password) return false;

    auto nonZeroMac = [](const uint8_t* m) {
        return m && (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) != 0;
    };
    bool bssidKnown = nonZeroMac(bssid);

    HandshakeSummary* summary = nullptr;
    if (bssidKnown) {
        for (auto& s : summaries_) {
            if (nonZeroMac(s.bssid) && memcmp(s.bssid, bssid, 6) == 0) { summary = &s; break; }
        }
    }
    if (!summary) {
        for (auto& s : summaries_) {
            if (strcmp(s.ssid, ssid) == 0) { summary = &s; break; }
        }
    }
    if (!summary) {
        Serial.printf("[CaptureRegistry] pwncrack result unmatched: %s\n", ssid);
        return false;
    }

    summary->pwncrackStatus = WpaSecStatus::CRACKED;
    if (bssidKnown) memcpy(summary->bssid, bssid, 6);

    HandshakeRecord rec;
    bool haveRecord = (summary->recIndex != NO_RECORD) &&
                      manifest_.readAt(summary->recIndex, rec);
    if (!haveRecord) {
        memset(&rec, 0, sizeof(rec));
        rec.state = static_cast<uint8_t>(RecordState::ACTIVE);
        memcpy(rec.bssid, bssidKnown ? bssid : summary->bssid, 6);
        strncpy(rec.ssid, summary->ssid, sizeof(rec.ssid) - 1);
        rec.type = static_cast<uint8_t>(RecordType::FOURWAY);
        rec.pcapSize = summary->size;
    }

    rec.pwncrackStatus = static_cast<uint8_t>(WpaSecStatus::CRACKED);
    strncpy(rec.pwncrackPassword, password, sizeof(rec.pwncrackPassword) - 1);
    rec.pwncrackPassword[sizeof(rec.pwncrackPassword) - 1] = '\0';
    rec.flags |= RF_HAS_PC_PASSWORD;
    rec.pwncrackCrackedAt = TimeManager::getInstance().isSynced()
                                ? (uint32_t)TimeManager::getInstance().now() : 0;

    if (summary->recIndex != NO_RECORD) {
        return manifest_.writeAt(summary->recIndex, rec);
    }
    uint32_t idx = 0;
    if (!manifest_.append(rec, &idx)) return false;
    summary->recIndex = static_cast<uint16_t>(idx);
    return true;
#else
    (void)bssid; (void)ssid; (void)password;
    return false;
#endif
}

} // namespace adversary

