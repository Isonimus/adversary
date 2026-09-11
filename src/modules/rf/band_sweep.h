#pragma once

/**
 * @file band_sweep.h
 * @brief Pure peak-hold model for the CC1101 RSSI band-sweep (slice-0019).
 *
 * The sweep tunes the CC1101 to each band preset in turn and reads RSSI; this
 * unit holds the peak dBm seen per band while the operator holds a remote, and
 * names the strongest band. It is deliberately hardware-free so it is
 * native-tested (test/test_band_sweep); the radio I/O lives in hal/cc1101 and the
 * bars in the screen. Mirrors the spectrum_scan split for the NRF24 analyzer.
 */

#include <cstddef>
#include <cstdint>

namespace adversary {
namespace rf {

/// Bands the sweep covers: the four CC1101 frequency presets (315/433/868/915).
constexpr size_t BAND_SWEEP_COUNT = 4;

/// Sentinel peak for a band not yet sampled — below any real CC1101 RSSI floor,
/// so a single real reading always wins the peak-hold and the strongest search.
constexpr int16_t BAND_RSSI_NONE = INT16_MIN;

/**
 * @brief Per-band peak-held RSSI (dBm) accumulated across sweeps.
 *
 * `peakDbm[b]` is the strongest reading band b has shown since reset(). Peak-hold
 * (not last-value) so a remote pressed briefly during one band's sample leaves a
 * lasting mark the operator can read, rather than decaying the instant the sweep
 * moves on.
 */
struct BandSweep {
    int16_t peakDbm[BAND_SWEEP_COUNT];

    /// Mark every band unsampled for a fresh sweep.
    void reset();

    /// Fold a fresh reading into band @p band's peak (keeps the max). Out-of-range
    /// band indices are ignored — the sweep loop only ever passes 0..COUNT-1.
    void observe(size_t band, int16_t dbm);
};

/**
 * @brief Index of the strongest sampled band, or -1 if none sampled yet.
 *
 * Ties resolve to the lowest index (deterministic highlight). Pure.
 */
int bandSweepStrongest(const BandSweep& sweep);

} // namespace rf
} // namespace adversary
