#pragma once

/**
 * @file spectrum_scan.h
 * @brief Pure occupancy model for the NRF24 2.4 GHz analyzer (slice-0006).
 *
 * The live sweep records, per RF channel, how many sweeps saw energy above the
 * radio's RPD threshold. This unit holds that tally and downsamples it onto the
 * display width as bar heights. It is deliberately hardware-free so it is
 * native-tested (test/test_spectrum_scan); the radio I/O lives in hal/nrf24 and
 * the blit in the screen.
 */

#include <cstddef>
#include <cstdint>

namespace adversary {
namespace rf {

/// Channels the analyzer covers: the nRF24 band is 0..125 (126 channels).
constexpr size_t SPECTRUM_CHANNELS = 126;
/// Bar value for a channel that read occupied on every sweep so far.
constexpr uint8_t SPECTRUM_FULL = 255;

/**
 * @brief Per-channel occupancy tally accumulated across sweeps.
 *
 * `hits[ch]` counts the sweeps in which channel ch's RPD latched; `sweeps` is the
 * denominator that normalises a bar to full-scale. A plain accumulator — the
 * sweep loop owns the increments — so the two fields always advance together
 * through reset() and the loop, and spectrumColumns() reads them as a snapshot.
 */
struct SpectrumScan {
    uint16_t hits[SPECTRUM_CHANNELS];
    uint16_t sweeps;

    /// Zero the tally for a fresh scan.
    void reset();
};

/**
 * @brief Downsample the occupancy tally to @p width bar heights (0..SPECTRUM_FULL).
 *
 * Each output column covers a contiguous band of channels and takes the **max**
 * occupancy in that band (a single busy channel must light its bar — averaging
 * would hide it among quiet neighbours), normalised against @p scan.sweeps so a
 * channel occupied every sweep reads full-scale. When @p width exceeds the
 * channel count some columns cover no channel and read zero.
 *
 * @param scan        the tally snapshot.
 * @param outColumns  caller buffer of at least @p width bytes; filled [0,width)
 *                    on success, left untouched on failure.
 * @param width       number of columns (display width in px).
 * @return true on success; false (leaving @p outColumns untouched) if the input
 *         is unusable (null buffer, zero width, or no sweeps yet) so the caller
 *         can draw a placeholder.
 */
bool spectrumColumns(const SpectrumScan& scan, uint8_t* outColumns, size_t width);

} // namespace rf
} // namespace adversary
