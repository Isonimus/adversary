# Design — Capture metadata storage rework (single BSSID-keyed manifest)

**Date:** 2026-06-26 · **Status:** design, not started · **Decision:** Option C
(see memory `storage-rework-decision`)

Replaces the handshake/WPA-SEC (and WiGLE) metadata storage with one flat,
fixed-size binary **manifest** that is the single source of truth for both the
firmware and the web GUI.

---

## 1. Constraints (these drive everything)

1. **Minimal memory at every stage.** The current design evolved specifically to
   dodge heap spikes when updating records after TLS uploads/refreshes (heap is
   already ~62 KB-down from the TLS connection then). The new system must be *at
   least as lean*, and ideally leaner: **no ArduinoJson in the update/refresh hot
   path**, no full-file rewrites, no per-record heap allocation.
2. **No migration.** Firmware is unreleased and existing on-SD metadata is
   inconsistent (older `.pcap`/`.csv` predate newer fields). We **rebuild from
   scratch** and **re-upload** rather than migrate. Old `.json` sidecars and
   `wpasec_status.bin` are discarded. The raw `.pcap`/`.csv` artifacts are kept.
3. **Single source of truth.** Firmware and GUI read the *same* state. The GUI
   must never depend on per-file JSON that can drift from the registry.
4. **Stable identity.** Key handshakes by **BSSID** (6 bytes), not SSID. SSID is
   display-only (emoji/`/`/spaces/duplicates break SSID-as-key today).

## 2. Why the current design fails (brief)

`wpaSecStatus` lives in 3 stores (RAM `summaries_`, `wpasec_status.bin`,
per-file `<SSID>.json`); `setWpaSecStatus()` updates the first two but **never the
JSON**, so the GUI detail (reads JSON) shows stale status. `wpasec_status.bin` is
**fully rewritten on every change** (~164 records ×20 during one cracked fetch).
SSID-as-key → "No metadata for <emoji SSID>". Per-file JSON parse uses a
`StaticJsonDocument<1024>` *per file*. Full analysis in memory
`storage-rework-decision`.

---

## 3. The model

### 3.1 On disk — one flat, fixed-size record file

`/adversary/captures/handshakes/manifest.bin`

```
[ Header (32 B) ][ Record 0 (256 B) ][ Record 1 ]...[ Record N-1 ]
```

- **Header:** magic, format version, record count, record size. Lets us validate
  and evolve.
- **Records are a fixed 256 B** (power-of-two, ≤ one SD 512 B sector pair → a
  record never straddles more than one write unit, so a torn write damages at most
  that one record). Record `i` lives at a **stable offset** `32 + i*256`.

```c
// packed, fixed 256 bytes (pad/reserve the remainder for future fields so the
// format does NOT churn when we add a field — we write into reserved space)
struct HandshakeRecord {
    uint8_t  recVersion;        // per-record schema version
    uint8_t  state;             // ACTIVE | TOMBSTONE (deleted)
    uint8_t  bssid[6];          // PRIMARY KEY
    char     ssid[33];          // display only
    uint8_t  channel;
    uint8_t  type;              // 4WAY | PMKID | EAPOL
    uint8_t  flags;             // hasMsg1..4, hasPMKID, hasGPS bitfield
    uint8_t  quality;           // 0..100
    int8_t   rssi;
    uint32_t capturedAt;        // epoch (real, now that NTP/time gate exists)
    uint32_t pcapSize;          // cached for the list view
    // --- WPA-SEC service state (mutable; the hot-update fields) ---
    uint8_t  wpaSecStatus;      // enum WpaSecStatus
    uint32_t wpaSecUploadedAt;
    uint32_t wpaSecCrackedAt;
    char     wpaSecPassword[65];
    // --- GPS (inline, optional) ---
    double   lat, lon;
    float    alt;
    uint8_t  sats;
    uint8_t  reserved[ ... ];   // pad to 256
};
```

**In-place O(1) updates.** Changing a record (e.g. status after upload) is
`seek(32 + i*256); write(record)` — one 256 B write, **no rewrite of the file, no
JSON, ~256 B of stack**. This is the central win and directly fixes both the churn
and the heap-spike problems.

**Append** for a new BSSID: write at the end, bump header count. **Delete:** set
`state = TOMBSTONE` (keeps every other record's offset stable); compact lazily
(rare, offline-ish) if tombstones pile up.

### 3.2 In RAM — a lean index, lazy full records

We do **not** hold 200 × 256 B in RAM. We hold a lean index entry per record:

```c
struct HsIndexEntry {   // ~48 B
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  wpaSecStatus;
    uint8_t  flags;          // hasPassword, hasGPS (for list badges)
    uint32_t pcapSize;
    uint16_t recIndex;       // offset into manifest.bin = 32 + recIndex*256
};
```

~48 B × 200 ≈ **9.6 KB** (comparable to today's `summaries_` 38 B × 200 ≈ 7.6 KB).
This index backs the firmware Captures screen **and** the GUI list endpoint.

A **full record** is loaded on demand by `seek+read` of one 256 B record — **no
JSON parse, no 1 KB doc**. Detail views need at most 1–2 at a time; a tiny 2-entry
cache (or none) suffices, vs today's 8-entry `MetadataCache` of 1 KB JSON parses.

### 3.3 Lookups

BSSID → index is the common op (upload result, cracked-results match). Options,
cheapest first: linear scan of the index (200 entries × 6 B compare is trivial and
allocation-free), or keep the index sorted by BSSID for binary search. Start
linear; revisit only if profiling says so.

---

## 4. Memory budget vs. today

| Path | Today | New |
|---|---|---|
| List in RAM | `summaries_` ~7.6 KB | index ~9.6 KB |
| Status update after upload | `saveStatusBin()` **rewrites all** N records + JSON `updateMetadata` (1 KB doc) | `seek+write` **one** 256 B record, **no JSON** |
| Cracked-results refresh (M cracked) | M × (full bin rewrite + JSON parse/save) | M × one 256 B in-place write |
| GUI detail | parse `<SSID>.json` (`StaticJsonDocument<1024>`) | read one 256 B record, serialize ~256 B JSON on demand |
| Heap during upload-time updates | JSON docs while heap is already low | ~256 B stack only |

The decisive property: **the mutable hot path (status after upload / cracked
fetch) touches only the flat binary with a stack buffer — zero heap churn, zero
full-file rewrite.**

---

## 5. Critical flows

**Capture (new handshake).** `handshake_save` already has `apBssid`, ssid,
channel, GPS, etc. → fill a `HandshakeRecord`, look up BSSID: if present, update in
place (re-capture preserves prior WPA-SEC state automatically — no more "preserve
cracked password" hack); else append. Update the RAM index entry.

**Upload success (`uploadHandshake`).** Set `wpaSecStatus = UPLOADED`,
`wpaSecUploadedAt`; update RAM index byte + `seek+write` the one record. No JSON.

**Cracked-results fetch (`fetchCrackedResults`).** Response is
`ap_bssid:client_bssid:ssid:password`. **Match by `ap_bssid`** (the primary key →
fixes the emoji-SSID misses). For each: set status CRACKED, copy password,
`wpaSecCrackedAt`; `seek+write` that record. M small writes, no full rewrite.

**GUI list (`/api/files/handshakes`).** Serialize the RAM index (name, size,
`wpaSecStatus`, hasPassword) — already the shape after the 2026-06-26 fix.

**GUI detail.** `seek+read` the record by `recIndex`, serialize that one record to
JSON (~256 B). Status/password are always consistent with the firmware.

---

## 6. WiGLE / wardriving

Different key space (per-CSV-file, not per-BSSID), so it gets its **own** small
manifest with the **same mechanism**: `/adversary/captures/wardriving/manifest.bin`,
fixed records keyed by CSV filename hash, holding `{ filename, uploaded, transId,
uploadedAt, size }`. Replaces the `.wigle` sidecar scheme and lets the wardriving
list show upload status from the registry too. Lower priority than the handshake
core — can be a later phase; until then the existing sidecar keeps working.

---

## 7. No-migration startup & rebuild

- **Discard** old `*.json` sidecars and `wpasec_status.bin` (one-time cleanup, or
  just ignore/overwrite). The user re-uploads to repopulate WPA-SEC state.
- **Bootstrap the index** from the `.pcap` directory:
  - Filenames give SSID + size. BSSID (the key) is **not** in the SSID-based
    filename. Two acceptable options:
    - **(A) Defer:** create index entries with `bssid = 0`, `status = NOT_UPLOADED`;
      the real BSSID + state are filled when the user **re-uploads** (we have the
      record then) or **re-captures**. Simplest, zero parsing.
    - **(B) Derive:** on first boot, read each `.pcap`'s first 802.11 frame to
      extract the BSSID (bounded read, fixed buffer, no JSON) and write a real
      record. One-time, still cheap. Better keys immediately.
  - Recommendation: ship **(A)** (truly minimal), offer **(B)** as an optional
    one-time "rebuild manifest" action.
- **Self-heal:** if `manifest.bin` is missing/corrupt (bad header/magic), rebuild
  the index from the `.pcap` dir via (A). The `.pcap`s remain the ground truth for
  *existence*.

## 8. Crash safety

- Single 256 B record writes are the only mutation; a torn write damages at most
  one record. On load, validate each record (`recVersion`, `state`, plausible
  fields); drop/zero invalid ones (the `.pcap` still exists → re-derivable).
- Header carries the count; a torn append is detected (count vs file size) and
  truncated.
- No multi-record transactions, so there's no half-applied refresh to unwind.

---

## 9. Phased implementation

1. **`HandshakeRecord` + manifest I/O** (`open/append/readAt/writeAt/validate`),
   unit-tested on native with a file-backed mock. No app wiring yet.
2. **In-RAM index + `CaptureRegistry` swap:** build index from manifest (or
   bootstrap (A) from `.pcap` dir); replace `summaries_`/`wpasec_status.bin`
   usage. Keep the public registry API stable where possible to limit blast radius.
3. **Wire the hot paths:** capture-save, `uploadHandshake`, `fetchCrackedResults`
   (match by BSSID), Captures screen, GUI list+detail endpoints.
4. **Delete dead code:** `wpasec_status.bin`, per-file `saveHandshakeMetadata`
   status writes, the "preserve cracked password on recapture" hack, the 8-entry
   JSON `MetadataCache`.
5. **(Later) WiGLE manifest** replacing the `.wigle` sidecar.
6. **Optional (B) rebuild action** to backfill BSSIDs from `.pcap`s.

## 10. Decisions (resolved 2026-06-26)

- **Bootstrap = (A) defer.** Index entries start `bssid=0`/`NOT_UPLOADED`; real
  BSSID + state arrive on re-upload/re-capture. No `.pcap` parsing at boot. ((B)
  derive remains an optional later "rebuild" action.)
- **PMKID `.22000` files are represented in the manifest** with `type = PMKID`
  (unified listing).
- **WiGLE/wardriving manifest = later phase**; the `.wigle` sidecar keeps working
  until then.
- **Record size = 256 B** (≈110 B reserved — comfortable).
- Index holds all ~200 entries in RAM (~9.6 KB, fine on the S3).
- GUI: SSID for display, **BSSID is the internal key** (shown in detail only).
