/**
 * @file band_sweep.cpp
 * @brief Pure peak-hold model for the CC1101 RSSI band-sweep (see band_sweep.h).
 */

#include "band_sweep.h"

namespace adversary {
namespace rf {

void BandSweep::reset() {
    for (size_t b = 0; b < BAND_SWEEP_COUNT; ++b) peakDbm[b] = BAND_RSSI_NONE;
}

void BandSweep::observe(size_t band, int16_t dbm) {
    if (band >= BAND_SWEEP_COUNT) return;
    if (dbm > peakDbm[band]) peakDbm[band] = dbm;  // BAND_RSSI_NONE is the floor
}

int bandSweepStrongest(const BandSweep& sweep) {
    int best = -1;
    for (size_t b = 0; b < BAND_SWEEP_COUNT; ++b) {
        if (sweep.peakDbm[b] == BAND_RSSI_NONE) continue;
        if (best == -1 || sweep.peakDbm[b] > sweep.peakDbm[best]) {
            best = static_cast<int>(b);
        }
    }
    return best;
}

} // namespace rf
} // namespace adversary
