/**
 * @file proximity_tracker.cpp
 * @brief Implementation of the pure RSSI hot/cold tracker core (ADR-0128).
 */

#include "core/proximity_tracker.h"

namespace adversary {

namespace {

/// Round a float dBm to the nearest int for display, away from zero.
int roundDbm(float value) {
    return value >= 0.0f ? static_cast<int>(value + 0.5f) : static_cast<int>(value - 0.5f);
}

} // namespace

ProximityTracker::ProximityTracker(const ProximityConfig& config)
    : m_config(config) {
    reset();
}

void ProximityTracker::reset() {
    m_hasSample = false;
    m_lastRawRssi = 0;
    m_lastSampleMs = 0;
    m_needle = 0.0f;
    m_baseline = 0.0f;
}

void ProximityTracker::addSample(int rssiDbm, uint32_t nowMs) {
    const float sample = static_cast<float>(rssiDbm);
    if (!m_hasSample) {
        // Seed both EWMAs with the first reading so the needle does not crawl up from zero.
        m_needle = sample;
        m_baseline = sample;
        m_hasSample = true;
    } else {
        m_needle += m_config.needleAlpha * (sample - m_needle);
        m_baseline += m_config.baselineAlpha * (sample - m_baseline);
    }
    m_lastRawRssi = rssiDbm;
    m_lastSampleMs = nowMs;
}

ProximityReading ProximityTracker::evaluate(uint32_t nowMs) const {
    ProximityReading reading{};

    if (!m_hasSample) {
        reading.state = ProximityState::Acquiring;
        reading.trend = ProximityTrend::Steady;
        reading.band = ProximityBand::Far;
        return reading;
    }

    reading.smoothedRssi = roundDbm(m_needle);
    reading.lastRawRssi = m_lastRawRssi;
    // nowMs is monotonic and >= the last sample time; guard against a caller passing an
    // earlier instant so age never underflows into a huge unsigned value.
    reading.ageMs = nowMs >= m_lastSampleMs ? nowMs - m_lastSampleMs : 0;

    // Band from the smoothed value.
    if (m_needle >= static_cast<float>(m_config.closeDbm)) {
        reading.band = ProximityBand::Close;
    } else if (m_needle < static_cast<float>(m_config.farDbm)) {
        reading.band = ProximityBand::Far;
    } else {
        reading.band = ProximityBand::Mid;
    }

    // Direction: needle above baseline means the signal is rising, i.e. getting closer.
    const float drift = m_needle - m_baseline;
    if (drift > m_config.trendDeltaDb) {
        reading.trend = ProximityTrend::Approaching;
    } else if (drift < -m_config.trendDeltaDb) {
        reading.trend = ProximityTrend::Receding;
    } else {
        reading.trend = ProximityTrend::Steady;
    }

    // Meter: map the smoothed value across [floor, ceil] to 0..100, clamped.
    const float span = static_cast<float>(m_config.ceilDbm - m_config.floorDbm);
    if (span > 0.0f) {
        const float ratio = (m_needle - static_cast<float>(m_config.floorDbm)) / span;
        const float clamped = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
        reading.meterPercent = static_cast<uint8_t>(clamped * 100.0f + 0.5f);
    } else {
        reading.meterPercent = 0;
    }

    // Lost-signal state machine: fresh -> stale (hold, dimmed) -> drop (release the lock).
    if (reading.ageMs > m_config.dropMs) {
        reading.state = ProximityState::Dropped;
    } else if (reading.ageMs > m_config.staleMs) {
        reading.state = ProximityState::Lost;
    } else {
        reading.state = ProximityState::Tracking;
    }

    return reading;
}

} // namespace adversary
