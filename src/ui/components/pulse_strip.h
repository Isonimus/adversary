#pragma once

/**
 * @file pulse_strip.h
 * @brief Static logic-analyzer preview of a captured on/off pulse train
 *        (slice-0005), shared by the IR and Sub-GHz capture screens.
 *
 * A captured IR-raw or OOK signal is a list of alternating on/off durations. To
 * let an operator *see* whether they caught a clean, structured frame or a
 * short/ragged noise fragment before saving it, this component downsamples that
 * train onto the display width as a per-column high-duty strip and blits it.
 *
 * The strip is deliberately **static** — computed from stored edges and painted
 * only on the save step and the detail screen, never during a transmit. IR
 * emission is a CPU bit-bang whose carrier is corrupted by any competing draw
 * (slice-0004 bring-up), so nothing here runs while a waveform is being sent.
 *
 * The downsampler `pulseStripColumns()` is pure and display-free so it is
 * native-testable (test/test_pulse_strip); `renderPulseStrip()` only blits its
 * output and exists on the device build.
 */

#include <cstddef>
#include <cstdint>

#include "hal/display/canvas_types.h"

namespace adversary {
namespace ui {

/// Full-fill duty value for a column that is high for its entire time window.
constexpr uint8_t PULSE_STRIP_FULL = 255;

/**
 * @brief Downsample an alternating on/off duration train to @p width columns.
 *
 * Each output column holds the fraction of its time window that the signal is
 * high, scaled to 0..PULSE_STRIP_FULL. Fill (not point-sampling) is used so a
 * burst denser than one pixel reads as a partial fill instead of aliasing to a
 * stray on or off, and a long gap keeps its proportional share of the width.
 *
 * @param durationsUs  alternating edge widths (us); durationsUs[0] is the first
 *                      level, whose polarity is given by @p firstLevelHigh.
 * @param count        number of edges.
 * @param firstLevelHigh  true if durationsUs[0] is a mark/on (IR-raw is always
 *                        mark-first; OOK carries this in OokSignal).
 * @param outColumns   caller buffer of at least @p width bytes; filled [0,width)
 *                      on success and left untouched on failure.
 * @param width        target column count (display width in px).
 * @return true on success; false (leaving @p outColumns untouched) if the train
 *         is unusable (null pointer, zero count, zero width, or a train that
 *         sums to zero) so the caller can draw a graceful placeholder.
 */
bool pulseStripColumns(const uint16_t* durationsUs, size_t count,
                       bool firstLevelHigh, uint8_t* outColumns, size_t width);

/**
 * @brief Blit a pulse strip into the rectangle (@p x, @p y, @p w, @p h).
 *
 * Renders the columns from pulseStripColumns() as bottom-anchored bars whose
 * height is the column's high-duty. On a degenerate train (pulseStripColumns()
 * returns false) it draws a flat baseline so the operator sees "nothing
 * structured here" rather than a blank or a crash. Device-only: the Canvas draw
 * primitives are firmware.
 */
void renderPulseStrip(Canvas& canvas, const uint16_t* durationsUs, size_t count,
                      bool firstLevelHigh, int x, int y, int w, int h);

} // namespace ui
} // namespace adversary
