/**
 * @file proximity_tracker.h
 * @brief Pure, host-testable core of the RSSI hot/cold tracker (ADR-0128).
 *
 * The device shells (BLE scanner tracker, WiFi-AP tracker) gather live RSSI from the radio
 * and render the result; the smoothing, banding, direction detection and lost-signal state
 * machine live here so they compile and are tested natively without any radio or clock.
 *
 * The output is honest relative signal, never distance: RSSI-to-metres needs a calibrated
 * TxPower-at-1m and an environment path-loss constant we do not have, so a distance readout
 * would be a confident lie. Stronger means closer, and nothing more precise is claimed.
 */

#pragma once

#include <cstdint>

namespace adversary {

/// Signal band driving the red/orange/green colour. Labelled as signal, not distance.
enum class ProximityBand : uint8_t { Far, Mid, Close };

/// Direction of travel inferred from the smoothed signal, within a dead-band.
enum class ProximityTrend : uint8_t { Steady, Approaching, Receding };

/// Lifecycle of a single locked target.
enum class ProximityState : uint8_t {
    Acquiring,  ///< Locked, but no advertisement/beacon heard yet.
    Tracking,   ///< A fresh sample within the stale window.
    Lost,       ///< No sample past staleMs: hold the last reading, dimmed.
    Dropped,    ///< No sample past dropMs: the shell should release the lock.
};

/**
 * @brief Tunable knobs for the tracker. All dB/ms live here so no bare magic value sits in
 *        a branch; see kDefaultProximityConfig for the Balanced starting point.
 */
struct ProximityConfig {
    float needleAlpha;    ///< Fast EWMA weight (0..1): the meter feel. Higher = snappier.
    float baselineAlpha;  ///< Slow EWMA weight for the trend baseline. Lower = steadier.
    float trendDeltaDb;   ///< Needle-minus-baseline dead-band before calling a direction.
    int8_t closeDbm;      ///< Smoothed >= this -> Close.
    int8_t farDbm;        ///< Smoothed < this -> Far; between farDbm and closeDbm -> Mid.
    int8_t floorDbm;      ///< Meter 0%: weakest signal shown.
    int8_t ceilDbm;       ///< Meter 100%: strongest signal shown.
    uint32_t staleMs;     ///< No sample for this long -> Lost.
    uint32_t dropMs;      ///< No sample for this long -> Dropped (release the lock).
};

/**
 * @brief Balanced default (chosen 2026-10-26). Follows movement within ~1 s while damping
 *        per-sample jitter; close >= -55 dBm, far < -75 dBm; hold a lost target 3 s then
 *        drop at 15 s. Tunable on-device (slice-0129 follow-up).
 */
constexpr ProximityConfig kDefaultProximityConfig = {
    /* needleAlpha   */ 0.35f,
    /* baselineAlpha */ 0.08f,
    /* trendDeltaDb  */ 2.0f,
    /* closeDbm      */ -55,
    /* farDbm        */ -75,
    /* floorDbm      */ -90,
    /* ceilDbm       */ -40,
    /* staleMs       */ 3000,
    /* dropMs        */ 15000,
};

/// What the shell renders. Snapshot produced by ProximityTracker::evaluate.
struct ProximityReading {
    ProximityState state;
    int smoothedRssi;      ///< Rounded dBm, the needle value (meaningful once a sample is in).
    int lastRawRssi;       ///< Last raw sample in dBm (0 before any sample).
    ProximityBand band;    ///< From the smoothed value.
    ProximityTrend trend;  ///< Approaching / receding / steady.
    uint8_t meterPercent;  ///< 0..100 bar fill, smoothed value mapped across [floor, ceil].
    uint32_t ageMs;        ///< Time since the last sample at the query instant.
};

/**
 * @brief Smooths RSSI samples and reports honest hot/cold state. Pure: no clock, no radio.
 *
 * Feed it live samples with addSample(rssiDbm, nowMs) where nowMs is a monotonic
 * millisecond clock, then call evaluate(nowMs) to render. reset() re-arms it for a new
 * locked target.
 */
class ProximityTracker {
public:
    explicit ProximityTracker(const ProximityConfig& config = kDefaultProximityConfig);

    /// Re-arm for a fresh lock: forget all samples and return to Acquiring.
    void reset();

    /// Feed one live RSSI sample (dBm) observed at nowMs.
    void addSample(int rssiDbm, uint32_t nowMs);

    /// Render the tracker state as of nowMs (does not mutate; safe to call every frame).
    ProximityReading evaluate(uint32_t nowMs) const;

    bool hasSample() const { return m_hasSample; }

private:
    ProximityConfig m_config;
    bool m_hasSample;
    int m_lastRawRssi;
    uint32_t m_lastSampleMs;
    float m_needle;    ///< Fast EWMA state.
    float m_baseline;  ///< Slow EWMA state (trend reference).
};

} // namespace adversary
