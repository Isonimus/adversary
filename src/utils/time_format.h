#pragma once

/**
 * @file time_format.h
 * @brief Pure time/duration string formatting helpers.
 */

#include <stdint.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace adversary {
namespace utils {

/**
 * @brief Format a duration as "HH:MM:SS" into @p out.
 *
 * Hours carry a two-digit minimum width (so short durations read "01:02:03") but are not
 * capped — a long or corrupt @p totalSeconds grows the hours field rather than truncating.
 * The caller must size @p out for the worst case: a uint32_t duration is at most
 * "1193046:59:59" (14 bytes incl. NUL), so a 16-byte buffer is safe.
 *
 * Pure (no clock reads), so it is unit-testable off-hardware.
 *
 * @param totalSeconds Duration in seconds
 * @param out          Output buffer (NUL-terminated); size it >= 16 for any uint32_t input
 * @param outLen       Size of @p out
 */
inline void formatDuration(uint32_t totalSeconds, char* out, size_t outLen) {
    const uint32_t hours = totalSeconds / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;
    snprintf(out, outLen, "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
}

/**
 * @brief Break a UTC epoch into LOCAL wall-clock fields using a display-only offset.
 *
 * Applies a signed minute offset and decomposes with gmtime_r. gmtime_r is reentrant and
 * reads no TZ global, so adding the offset to the epoch first yields local fields without ever
 * mutating the canonical UTC clock (ADR-0118: the offset is display-only). Pure (no clock
 * reads), so it is unit-testable off-hardware.
 *
 * @param utcEpoch      Canonical UTC epoch (seconds), e.g. TimeManager::now()
 * @param offsetMinutes Signed display offset in minutes (UTC-12:00 .. +14:00 => -720 .. +840)
 * @param out           Broken-down local fields (must be non-null)
 * @return true on success; false only if gmtime_r rejects the shifted epoch (out of range)
 */
inline bool localTimeFields(time_t utcEpoch, int16_t offsetMinutes, struct tm* out) {
    const time_t localEpoch = utcEpoch + static_cast<time_t>(offsetMinutes) * 60;
    return gmtime_r(&localEpoch, out) != nullptr;
}

/**
 * @brief Format a signed minute offset as a settings-row label: "UTC", "UTC+5:30", "UTC-3:00".
 *
 * The hour carries the sign; the minute part uses the absolute remainder so a negative offset
 * is never rendered with a negative minute. Pure, so it is unit-testable off-hardware.
 *
 * @param offsetMinutes Signed display offset in minutes
 * @param out           Output buffer (NUL-terminated); size >= 10 is safe ("UTC-12:00")
 * @param outLen        Size of @p out
 */
inline void formatUtcOffset(int16_t offsetMinutes, char* out, size_t outLen) {
    if (offsetMinutes == 0) {
        snprintf(out, outLen, "UTC");
        return;
    }
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const int magnitude = std::abs(static_cast<int>(offsetMinutes));
    snprintf(out, outLen, "UTC%c%d:%02d", sign, magnitude / 60, magnitude % 60);
}

} // namespace utils
} // namespace adversary
