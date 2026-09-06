/**
 * @file pulse_strip.cpp
 * @brief Pulse-strip downsampler + renderer (slice-0005).
 *
 * The downsampler integrates high (mark/on) time into each pixel column's time
 * window and reports it as a duty value, so the strip is an honest density view
 * rather than a point-sample that could drop or fabricate narrow pulses.
 */

#include "pulse_strip.h"

namespace adversary {
namespace ui {

bool pulseStripColumns(const uint16_t* durationsUs, size_t count,
                       bool firstLevelHigh, uint8_t* outColumns, size_t width) {
    if (durationsUs == nullptr || outColumns == nullptr || count == 0 || width == 0) {
        return false;
    }

    // uint64 throughout: total can reach ~134M (OOK_MAX_PULSES * 65535 us) and
    // the position math multiplies that by the column index.
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) total += durationsUs[i];
    if (total == 0) return false;  // a zero-length train has no structure to draw

    // Forward cursor over the edge train. `seg` is the first segment whose end
    // extends into the current column's window; it only advances as columns do,
    // so the walk is amortised O(count + width). The cursor is not consumed by a
    // segment that straddles a column boundary — the inner integration uses a
    // local copy so that segment is still available to the next column.
    size_t seg = 0;
    uint64_t segBase = 0;             // cumulative start time of segment `seg`
    bool segHigh = firstLevelHigh;    // polarity of segment `seg`

    for (size_t x = 0; x < width; ++x) {
        const uint64_t winStart = (total * x) / width;
        const uint64_t winEnd = (total * (x + 1)) / width;
        const uint64_t winSpan = winEnd - winStart;

        // Drop segments that end at or before this window begins.
        while (seg < count && segBase + durationsUs[seg] <= winStart) {
            segBase += durationsUs[seg];
            ++seg;
            segHigh = !segHigh;
        }

        // Integrate high time over [winStart, winEnd) with a local walk so the
        // shared cursor stays put on any boundary-straddling segment.
        uint64_t highTime = 0;
        size_t s = seg;
        uint64_t base = segBase;
        bool high = segHigh;
        while (s < count && base < winEnd) {
            const uint64_t segEnd = base + durationsUs[s];
            if (high) {
                const uint64_t lo = base > winStart ? base : winStart;
                const uint64_t hi = segEnd < winEnd ? segEnd : winEnd;
                if (hi > lo) highTime += hi - lo;
            }
            base = segEnd;
            ++s;
            high = !high;
        }

        // A zero-span window (train shorter than the width in us) reads as empty
        // rather than dividing by zero — a degenerate but defined result.
        outColumns[x] = winSpan == 0
                            ? 0
                            : static_cast<uint8_t>((highTime * PULSE_STRIP_FULL) / winSpan);
    }

    return true;
}

} // namespace ui
} // namespace adversary

#if defined(TARGET_CARDPUTER) || defined(TARGET_M5STICK)

#include <vector>

#include "ui/theme.h"

namespace adversary {
namespace ui {

void renderPulseStrip(Canvas& canvas, const uint16_t* durationsUs, size_t count,
                      bool firstLevelHigh, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    canvas.drawRect(x, y, w, h, theme::TEXT_SECONDARY());

    // Inset the drawable area by the 1px frame.
    const int innerX = x + 1;
    const int innerY = y + 1;
    const int innerW = w - 2;
    const int innerH = h - 2;
    if (innerW <= 0 || innerH <= 0) return;

    const int baseline = innerY + innerH - 1;  // bottom row: the "off" level

    std::vector<uint8_t> cols(static_cast<size_t>(innerW));
    if (!pulseStripColumns(durationsUs, count, firstLevelHigh, cols.data(),
                           cols.size())) {
        // Degenerate train: draw a flat baseline so the operator reads "nothing
        // structured here" instead of a blank rectangle.
        canvas.drawLine(innerX, baseline, innerX + innerW - 1, baseline,
                        theme::TEXT_SECONDARY());
        return;
    }

    for (int c = 0; c < innerW; ++c) {
        const int barHeight = (cols[static_cast<size_t>(c)] * innerH) / PULSE_STRIP_FULL;
        if (barHeight <= 0) {
            canvas.drawPixel(innerX + c, baseline, theme::TEXT_SECONDARY());
        } else {
            canvas.fillRect(innerX + c, baseline - barHeight + 1, 1, barHeight,
                            theme::ACCENT());
        }
    }
}

} // namespace ui
} // namespace adversary

#endif  // TARGET_CARDPUTER || TARGET_M5STICK
