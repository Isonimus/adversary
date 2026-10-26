/**
 * @file crash_report.h
 * @brief Pure, host-testable core of the crash-log entry (ADR-0126, slice-0127).
 *
 * The device shell (system_manager.cpp) gathers the inputs (reset reason, the RTC
 * flight-recorder snapshot, wall clock) and does the SD I/O; the classification and
 * the entry text live here so they compile and are tested natively without pulling
 * in <esp_system.h>.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace adversary {

/**
 * @brief Host-testable mirror of the esp_reset_reason_t cases this firmware handles.
 *
 * The device translates the IDF enum into this (a thin switch) so the classifier below
 * stays pure. Keep the two in sync: a new esp_reset_reason_t worth logging gets a case
 * here and in the device-side mapper.
 */
enum class ResetReason : uint8_t {
    PowerOn,       ///< Clean power-on (not a crash)
    Panic,         ///< Exception / abort
    IntWdt,        ///< Interrupt watchdog
    TaskWdt,       ///< Task watchdog
    OtherWdt,      ///< Any other watchdog
    Brownout,      ///< Supply voltage dropped
    ExternalSdio,  ///< External (SDIO) reset
    Software,      ///< Deliberate software reset (not a crash)
    DeepSleep,     ///< Deep-sleep wake (not a crash)
    Unknown,       ///< Anything else
};

/// Result of classifying a reset cause.
struct ResetClassification {
    const char* name;  ///< Stable human-readable label written to crash.log
    bool isCrash;      ///< True for an abnormal reset whose heap state is worth recording
};

/**
 * @brief Classify a reset cause. Pure: no I/O, no globals.
 *
 * A non-crash reset clears the consecutive-crash streak (the device resets its counter
 * whenever isCrash is false), so DeepSleep and Software both count as clean here.
 */
ResetClassification classifyReset(ResetReason reason);

/// Last heap state the RTC flight recorder sampled before the reset. Bytes; uptimeMs
/// is millis() at the sample.
struct HeapSnapshot {
    uint32_t freeHeap;
    uint32_t largestFreeBlock;
    uint32_t minFreeHeap;
    uint32_t uptimeMs;
};

/**
 * @brief Render one crash.log entry into @p out (snprintf semantics: always
 *        NUL-terminates when @p outSize > 0, returns the length a large-enough buffer
 *        would hold).
 *
 * @param snapshotValid false when RTC memory did not survive the reset (cold power-on
 *        or brownout). The heap lines then read "unavailable" instead of stale or
 *        garbage numbers, so a reader never mistakes random RAM for crash state.
 * @param timestamp preformatted wall-clock string ("unsynced" when the clock was never
 *        set), produced by the device from the snapshot's stored epoch.
 */
int formatCrashEntry(char* out, size_t outSize,
                     const ResetClassification& classification,
                     uint16_t crashCount,
                     bool snapshotValid,
                     const HeapSnapshot& snapshot,
                     const char* timestamp);

} // namespace adversary
