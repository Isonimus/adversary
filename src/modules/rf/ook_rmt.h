/**
 * @file ook_rmt.h
 * @brief OOK TX/RX drive on the CC1101 GDO0 line (slice-0003, slice-0017).
 *
 * Capture and replay use the ESP32 RMT peripheral, which times edges in hardware,
 * so capture is immune to the jitter a micros() loop suffers under WiFi/other ISRs
 * and replay is precise and non-blocking (CLAUDE.md's raw-RF pitfall). Jamming
 * (slice-0017) is the opposite case: it wants energy on the channel and does NOT
 * care about edge precision, so it is a plain bit-banged GDO0 drive — no RMT. This
 * unit is JUST the line binding on CC1101_GDO0 (G13); it knows nothing about the
 * CC1101 registers or the SD bus. The caller sequences it: cc1101ConfigureOok() +
 * cc1101EnterRx()/EnterTx() around it, then cc1101Idle() and SDManager::remount().
 *
 * The I/O is firmware-only (ESP32 hardware); native builds never link it. The pure
 * types/helpers it exposes — OokSignal, planJamNoise() — are native-tested.
 */

#pragma once

#include <cstdint>

#include "ook_signal.h"

namespace adversary {
namespace rf {

/// Sub-GHz jam emission mode (slice-0017).
enum class JamMode : uint8_t {
    CarrierWave,      ///< GDO0 held high: steady unmodulated carrier.
    ModulatedNoise,   ///< GDO0 toggled fast: energy spread across the channel.
};

/// Fast-toggle half-period for ModulatedNoise, in microseconds (~25 kHz square).
constexpr uint32_t JAM_NOISE_HALF_PERIOD_US = 20;
/// A GDO0 half-period must fit a 16-bit micro-timer field; guards a units slip.
constexpr uint32_t JAM_MAX_HALF_PERIOD_US = 32767;

/// How a noise burst of @p chunkMs decomposes into GDO0 high/low toggle cycles.
struct JamNoisePlan {
    bool valid;        ///< false if the half-period is out of range or yields 0 cycles.
    uint32_t cycles;   ///< number of full high+low toggle cycles to fill the chunk.
};

/**
 * @brief Pure plan for a ModulatedNoise burst: cycles to fill @p chunkMs by
 *        toggling GDO0 every @p halfPeriodUs. Native-tested (test_ook_jam); this is
 *        the one quantitative piece of the otherwise-I/O jammer (a ms/µs slip would
 *        make the burst 1000x wrong).
 */
constexpr JamNoisePlan planJamNoise(uint32_t chunkMs, uint32_t halfPeriodUs) {
    if (halfPeriodUs == 0 || halfPeriodUs > JAM_MAX_HALF_PERIOD_US) {
        return {false, 0};
    }
    const uint32_t cycles = (chunkMs * 1000u) / (2u * halfPeriodUs);
    return {cycles > 0, cycles};
}

/**
 * @brief Capture one OOK pulse train from GDO0 into @p out.
 *
 * Arms RMT RX and waits up to @p windowMs for a transmission; a burst ends when
 * the line stays idle past the frame gap. Fills @p out.firstLevelHigh and
 * @p out.durationsUs (microseconds); the caller sets @p out.frequencyHz to the
 * preset it tuned. The CC1101 must already be configured and in RX.
 * @return true if a non-empty train was captured; false on timeout (no signal),
 *         an RMT error, or an empty capture — never a partial/garbage signal.
 */
bool ookRmtCapture(uint32_t windowMs, OokSignal& out);

/**
 * @brief Replay @p sig verbatim on GDO0 via RMT TX.
 *
 * The CC1101 must already be configured and in TX. Transmits the recorded edge
 * train a small fixed number of times (fixed-code receivers debounce and need
 * several frames to latch). Rejects a signal with no edges or an edge longer than
 * the RMT counter can represent (a corrupt/hand-crafted file) rather than
 * silently clamping it.
 * @return true if the train was transmitted; false on a bad signal or RMT error.
 */
bool ookRmtReplay(const OokSignal& sig);

/**
 * @brief Emit one jam burst on GDO0 for ~@p chunkMs, then release the pin (slice-0017).
 *
 * The CC1101 must already be configured and in TX. CarrierWave holds GDO0 high;
 * ModulatedNoise toggles it at JAM_NOISE_HALF_PERIOD_US. Returns with GDO0 back at
 * INPUT_PULLUP so the between-burst keyboard scan on the shared matrix row is clean
 * (CLAUDE.md pin-conflict invariant) — the caller loops this while the jam key is
 * held and calls cc1101Idle() when it stops.
 * @return true on a completed burst; false only on an invalid noise plan.
 */
bool ookJamBurst(JamMode mode, uint32_t chunkMs);

} // namespace rf
} // namespace adversary
