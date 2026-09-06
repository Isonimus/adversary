/**
 * @file handshake_record.h
 * @brief Fixed-size (256 B) on-disk record for the BSSID-keyed capture manifest
 *
 * Single source of truth for handshake/PMKID capture facts + WPA-SEC service
 * state. Records live at stable offsets inside manifest.bin and are mutated
 * in place with one seek+write (no JSON, no full-file rewrite) — see
 * docs/DESIGN-storage-rework.md.
 *
 * The struct is packed so the in-RAM layout IS the on-disk layout; do not add
 * fields except by consuming `reserved` (keeps the format stable) and bumping
 * RECORD_VERSION.
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace adversary {

/// Per-record lifecycle state (the `state` byte).
enum class RecordState : uint8_t {
    EMPTY     = 0,  ///< Never written / zeroed slot
    ACTIVE    = 1,  ///< Live record
    TOMBSTONE = 2,  ///< Logically deleted; slot kept so other offsets are stable
};

/// Capture kind (the `type` byte).
enum class RecordType : uint8_t {
    FOURWAY = 0,  ///< 4-way EAPOL handshake
    PMKID   = 1,  ///< PMKID-only capture (.22000)
    EAPOL   = 2,  ///< Partial EAPOL
};

/// Bitmask values for HandshakeRecord::flags.
enum RecordFlags : uint8_t {
    RF_HAS_MSG1        = 1 << 0,
    RF_HAS_MSG2        = 1 << 1,
    RF_HAS_MSG3        = 1 << 2,
    RF_HAS_MSG4        = 1 << 3,
    RF_HAS_PMKID       = 1 << 4,
    RF_HAS_GPS         = 1 << 5,
    RF_HAS_PASSWORD    = 1 << 6,  ///< wpaSecPassword populated (list-badge hint)
    RF_HAS_PC_PASSWORD = 1 << 7,  ///< pwncrackPassword populated (list-badge hint)
};

/// Second flags byte (`flags` is full). Lives in the post-CRC reserved area, so
/// adding it needs no format bump: existing records read it as 0. Not CRC-covered
/// (hint-only; a stray bit just mis-counts a pwncrack job, never corrupts data).
enum RecordFlags2 : uint8_t {
    RF2_HAS_HC22000 = 1 << 0,  ///< a "{ssid}.22000" exists on SD (pwncrack-uploadable)
};

/// Current schema version for a single record (HandshakeRecord::recVersion).
/// v2 added the pwncrack service block (status/timestamps/password).
static constexpr uint8_t RECORD_VERSION = 2;

/**
 * @brief One capture entry, fixed at 256 bytes on disk.
 *
 * Primary key is `bssid` (BSSID), not SSID — SSID is display-only. The CRC
 * covers every field up to (but excluding) `crc` itself, so a torn 256-byte
 * write is detected on load and the slot can be dropped/rebuilt from the
 * surviving .pcap.
 */
struct __attribute__((packed)) HandshakeRecord {
    uint8_t  recVersion;        ///< Per-record schema version (RECORD_VERSION)
    uint8_t  state;             ///< RecordState
    uint8_t  bssid[6];          ///< PRIMARY KEY
    char     ssid[33];          ///< Display only (NUL-terminated)
    uint8_t  channel;
    uint8_t  type;              ///< RecordType
    uint8_t  flags;             ///< RecordFlags bitfield
    uint8_t  quality;           ///< 0..100
    int8_t   rssi;              ///< dBm
    uint32_t capturedAt;        ///< Unix epoch (real, now NTP/time-gate exists)
    uint32_t pcapSize;          ///< Cached artifact size for the list view

    // --- WPA-SEC service state (the mutable hot-update fields) ---
    uint8_t  wpaSecStatus;      ///< enum WpaSecStatus (stored as byte)
    uint32_t wpaSecUploadedAt;
    uint32_t wpaSecCrackedAt;
    char     wpaSecPassword[65];

    // --- pwncrack service state (parallel to WPA-SEC; .22000 upload) ---
    uint8_t  pwncrackStatus;    ///< enum WpaSecStatus reused (stored as byte)
    uint32_t pwncrackUploadedAt;
    uint32_t pwncrackCrackedAt;
    char     pwncrackPassword[65];

    // --- GPS (inline, optional; gated by RF_HAS_GPS) ---
    double   lat;
    double   lon;
    float    alt;
    uint8_t  sats;

    uint16_t crc;               ///< CRC-16 over bytes [0, offsetof(crc))
    uint8_t  flags2;            ///< RecordFlags2 (post-CRC; 0 in pre-existing records)
    uint8_t  reserved[30];      ///< Pad to 256; future fields carve from here
};

static_assert(sizeof(HandshakeRecord) == 256, "HandshakeRecord must be exactly 256 bytes");

/// Byte span the CRC is computed over (everything before the `crc` field).
static constexpr size_t HS_CRC_SPAN = offsetof(HandshakeRecord, crc);

/**
 * @brief CRC-16/CCITT-FALSE over the record's covered span.
 *
 * Poly 0x1021, init 0xFFFF, no reflection. Cheap, allocation-free, plenty to
 * catch a torn write.
 */
inline uint16_t handshakeRecordCrc(const HandshakeRecord& rec) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&rec);
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < HS_CRC_SPAN; ++i) {
        crc ^= static_cast<uint16_t>(p[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

/// Stamp version + CRC so the record is ready to persist.
inline void handshakeRecordSeal(HandshakeRecord& rec) {
    rec.recVersion = RECORD_VERSION;
    rec.crc = handshakeRecordCrc(rec);
}

/**
 * @brief Plausibility + integrity check for a record read from disk.
 * @return true if the record is intact and usable.
 */
inline bool handshakeRecordValid(const HandshakeRecord& rec) {
    if (rec.recVersion == 0 || rec.recVersion > RECORD_VERSION) return false;
    if (rec.state != static_cast<uint8_t>(RecordState::ACTIVE) &&
        rec.state != static_cast<uint8_t>(RecordState::TOMBSTONE)) {
        return false;
    }
    if (rec.ssid[sizeof(rec.ssid) - 1] != '\0') return false;  // unterminated SSID
    return rec.crc == handshakeRecordCrc(rec);
}

} // namespace adversary
