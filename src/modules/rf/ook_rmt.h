/**
 * @file ook_rmt.h
 * @brief RMT-backed OOK capture/replay on the CC1101 GDO0 line (slice-0003).
 *
 * The ESP32 RMT peripheral times edges in hardware, so capture is immune to the
 * jitter a micros() loop suffers under WiFi/other ISRs and replay is precise and
 * non-blocking (CLAUDE.md's raw-RF pitfall). This unit is JUST the RMT binding on
 * CC1101_GDO0 (G13) — it knows nothing about the CC1101 registers or the SD bus.
 * The caller sequences it: cc1101ConfigureOok() + cc1101EnterRx()/EnterTx()
 * around it, then cc1101Idle() and SDManager::remount() after.
 *
 * Firmware-only (RMT is ESP32 hardware); native builds never see this. The pulse
 * train model it fills/reads, OokSignal, is the native-tested pure type.
 */

#pragma once

#include <cstdint>

#include "ook_signal.h"

namespace adversary {
namespace rf {

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

} // namespace rf
} // namespace adversary
