#pragma once

// Centralized heap thresholds for memory-sensitive operations.
// Kept in one place so the TLS upload path and any future gating share the
// same tuning. Values are in bytes unless noted.

#include <stddef.h>
#include <stdint.h>

namespace adversary {
namespace heap_policy {

// --- Mid-upload pacing (see modules/network/tls_upload.cpp) ---------------
// A large upload can outrun the WiFi link: un-acked TX data piles up in heap
// and can collapse it mid-write (TLS dies with conn=0; the err=48 "PADLOCK
// alignment" message is a red herring from mbedtls_strerror()). When free heap
// dips below the soft floor we drain the send queue before feeding more, and
// cleanly abort below the hard floor so the file is retried on the next sync.
constexpr size_t   TLS_WRITE_SOFT_FLOOR = 24000;  // start draining below this
constexpr size_t   TLS_WRITE_HARD_FLOOR = 14000;  // clean-abort below this
constexpr uint32_t TLS_WRITE_DRAIN_MS   = 3000;   // max drain wait per stall

// --- Pre-connect gating ----------------------------------------------------
// Refuse to start a TLS upload when the largest contiguous free block is too
// small to hold the mbedTLS record buffers + handshake, rather than failing
// part-way through connect(). The canvas purge in
// SystemManager::prepareForMemoryIntensiveTask() normally keeps us well above
// this; it is a safety net.
constexpr size_t   MIN_CONTIG_FOR_TLS = 20000;

// --- Adaptive canvas purge -------------------------------------------------
// When a caller asks to keep the 64800-byte canvas resident (purgeCanvas=false,
// e.g. the bulk sync wanting live UI), prepareForMemoryIntensiveTask() purges
// it anyway if the largest contiguous block has fallen below this. mbedTLS
// needs a contiguous run for its record buffers and dies with -32512 otherwise.
// Set above MIN_CONTIG_FOR_TLS so we purge with headroom, not at the cliff.
// The trigger case is post-Karma heap fragmentation: free heap is ~80KB but the
// largest block is ~19KB. Normal operation keeps the largest block ~70KB+, far
// above this, so the purge never trips and the canvas stays resident.
constexpr size_t   TLS_CONTIG_PURGE_FLOOR = 30000;

} // namespace heap_policy
} // namespace adversary
