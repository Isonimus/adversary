/**
 * @file spectrum_scan.cpp
 * @brief Pure occupancy model for the NRF24 2.4 GHz analyzer (see spectrum_scan.h).
 */

#include "spectrum_scan.h"

#include <cstring>

namespace adversary {
namespace rf {

void SpectrumScan::reset() {
    std::memset(hits, 0, sizeof(hits));
    sweeps = 0;
}

bool spectrumColumns(const SpectrumScan& scan, uint8_t* outColumns, size_t width) {
    if (outColumns == nullptr || width == 0 || scan.sweeps == 0) return false;

    for (size_t col = 0; col < width; ++col) {
        // Half-open channel band [lo, hi) this column represents. Integer scaling
        // tiles the band across the width with no gaps when width <= channels.
        const size_t lo = col * SPECTRUM_CHANNELS / width;
        size_t hi = (col + 1) * SPECTRUM_CHANNELS / width;
        if (hi > SPECTRUM_CHANNELS) hi = SPECTRUM_CHANNELS;

        uint16_t peak = 0;
        for (size_t ch = lo; ch < hi; ++ch) {
            if (scan.hits[ch] > peak) peak = scan.hits[ch];
        }

        uint32_t scaled = static_cast<uint32_t>(peak) * SPECTRUM_FULL / scan.sweeps;
        if (scaled > SPECTRUM_FULL) scaled = SPECTRUM_FULL;  // hits<=sweeps, but guard
        outColumns[col] = static_cast<uint8_t>(scaled);
    }
    return true;
}

} // namespace rf
} // namespace adversary
