/**
 * @file expansion_cap.h
 * @brief Top-side expansion-cap detection for the Cardputer.
 *
 * Only one cap attaches to the top-side header at a time, and several of its
 * pins are shared with the SD SPI bus (ADR-0001 / slice-0002). This unit answers
 * one question: which cap is attached? The decision itself is a pure function so
 * the precedence logic is unit-testable without a radio on the bus; the hardware
 * probes that feed it live in expansion_cap.cpp and only compile for firmware.
 */

#pragma once

#include <cstdint>

namespace adversary {
namespace hal {

/**
 * @brief Which expansion cap is currently resolved as attached.
 *
 * The LoRa 1262 cap is intentionally absent: its RF stack is shelved (ADR-0001),
 * and for detection purposes "not the multi-radio cap" collapses to None.
 */
enum class ExpansionCap : uint8_t {
    None,
    MultiRadio,  // CC1101 + NRF24L01 + IR cap
};

/**
 * @brief Operator override for cap detection, persisted in WirelessSettings.
 *
 * A half-seated cap can probe falsely in either direction, so the operator can
 * pin the answer instead of trusting the SPI probe.
 */
enum class CapOverride : uint8_t {
    Auto,             // trust the SPI probes
    ForceNone,        // no cap regardless of probe
    ForceMultiRadio,  // multi-radio cap regardless of probe
};

/**
 * @brief Resolve the attached cap from the probe results and the operator override.
 *
 * Pure: no hardware access, so the full truth table is exercised in
 * test/test_expansion_cap. A non-Auto override wins outright (deterministic cap
 * swaps, and a defence against a half-seated cap probing falsely). On Auto, the
 * multi-radio cap is present iff either of its radios answered its probe.
 */
inline ExpansionCap resolveExpansionCap(bool cc1101Present, bool nrf24Present,
                                        CapOverride ov) {
    switch (ov) {
        case CapOverride::ForceNone:
            return ExpansionCap::None;
        case CapOverride::ForceMultiRadio:
            return ExpansionCap::MultiRadio;
        case CapOverride::Auto:
            break;  // fall through to the probe-driven decision
    }
    return (cc1101Present || nrf24Present) ? ExpansionCap::MultiRadio
                                           : ExpansionCap::None;
}

/**
 * @brief Raw result of the last cap probe, for display and "As built" pinning.
 */
struct CapProbeResult {
    bool cc1101Present = false;
    uint8_t cc1101Version = 0;  // raw CC1101 VERSION byte (0x00/0xFF = absent bus)
    bool nrf24Present = false;
};

// --- Hardware probes (firmware-only; defined in expansion_cap.cpp) ------------

/**
 * @brief Probe the CC1101 over the SD-shared SPI bus.
 * @return true iff the CC1101 VERSION status register returns a plausible
 *         silicon revision (neither 0x00 nor 0xFF, which are the floating/absent
 *         bus reads). The exact revision byte is logged for the slice-0002
 *         "As built" pinning. Must run AFTER the SD mount so it never races the
 *         pre-SD chip-select de-select.
 */
bool probeCC1101();

/**
 * @brief Probe the NRF24L01 over the SD-shared SPI bus.
 * @return true iff a known pattern written to RF_CH reads back unchanged. The
 *         NRF24 has no ID register, so read-back is the only presence test. The
 *         register is restored afterwards. Must run AFTER the SD mount.
 */
bool probeNRF24();

/**
 * @brief Run the probes (unless overridden) and resolve the attached cap.
 *
 * Skips the SPI probes entirely when @p ov forces a result, so a forced
 * MultiRadio wins even if the cap is re-seated and probes would fail.
 */
ExpansionCap detectExpansionCap(CapOverride ov);

/**
 * @brief The probe result recorded by the most recent detectExpansionCap() call.
 *
 * When the override skipped the probes both radios read absent; the Radio screen
 * uses this to show what was actually seen.
 */
const CapProbeResult& lastCapProbe();

/**
 * @brief The cap resolved by the most recent detectExpansionCap() call.
 *
 * The RF screens gate on this (plus an owned SD bus) before touching a radio:
 * with no multi-radio cap resolved there is nothing to drive (slice-0003).
 */
ExpansionCap resolvedExpansionCap();

} // namespace hal
} // namespace adversary
