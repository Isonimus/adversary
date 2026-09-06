/**
 * @file nrf24.h
 * @brief NRF24L01+ driver for the 2.4 GHz spectrum analyzer (slice-0006).
 *
 * The nRF24L01+ is the multi-radio cap's second radio. This slice uses it purely
 * as a passive channel-occupancy sensor: it sweeps the 126 RF channels and reads
 * the on-board Received Power Detector (RPD) — no packets, no address, no TX. The
 * radio shares the SD FSPI bus, so the driver borrows SDManager's SPIClass via
 * cap_bus.h exactly as the CC1101 driver does; the caller remounts the card after
 * a sweep. Nothing here decodes or transmits (Mousejack is a later, dongle-gated
 * slice).
 *
 * The channel->frequency math is a pure inline function, native-tested in
 * test/test_nrf24_channel; the register-driving functions are firmware-only
 * (defined in nrf24.cpp, TARGET_CARDPUTER).
 */

#pragma once

#include <cstdint>

namespace adversary {
namespace hal {

/// Highest valid nRF24 RF channel (datasheet: RF_CH is 0..125).
constexpr uint8_t NRF24_MAX_CHANNEL = 125;
/// Channel 0 sits at 2400 MHz; each channel steps 1 MHz (datasheet F = 2400+ch).
constexpr uint16_t NRF24_BASE_MHZ = 2400;
/// Number of channels the analyzer sweeps (0..125 inclusive).
constexpr uint16_t NRF24_CHANNEL_COUNT = NRF24_MAX_CHANNEL + 1;

/**
 * @brief RF channel -> centre frequency in MHz. Pure and inline for native tests.
 *
 * @param channel  RF channel, 0..NRF24_MAX_CHANNEL.
 * @param outMHz   receives the centre frequency on success; left untouched on
 *                 failure.
 * @return true on success; false (no write) for a null out-pointer or a channel
 *         above NRF24_MAX_CHANNEL — fail loud, never clamp a bad channel to a
 *         real frequency.
 */
inline bool nrf24ChannelToMHz(uint8_t channel, uint16_t* outMHz) {
    if (outMHz == nullptr || channel > NRF24_MAX_CHANNEL) return false;
    *outMHz = static_cast<uint16_t>(NRF24_BASE_MHZ + channel);
    return true;
}

// --- Firmware register-driving functions (defined in nrf24.cpp) --------------

/**
 * @brief Power up and configure the radio as a passive RX scanner.
 *
 * Sets PWR_UP|PRIM_RX with auto-ack off and no pipes opened, waits out the
 * power-up settling, and leaves the chip-select driven for the sweep session.
 * Borrows the SD bus (SDManager::spiBus()).
 * @return true once the radio is in RX standby; false if the bus is unowned
 *         (launcher mount) — same gating as the CC1101 path.
 */
bool nrf24BeginRxScan();

/**
 * @brief Tune the radio to an RF channel for the next RPD sample.
 * @return false for an out-of-range channel or an unowned bus (no write).
 */
bool nrf24SetChannel(uint8_t channel);

/**
 * @brief Listen on the current channel for @p dwellUs, then read the RPD latch.
 *
 * Drives CE high to enter RX, waits the receiver settling time plus @p dwellUs,
 * drops CE, and reads the Received Power Detector (register 0x09, bit 0), which
 * latches when received power exceeded the chip's fixed -64 dBm threshold during
 * the window. Coarse presence, not a graded level — that limit is surfaced in the
 * UI.
 * @return true iff RPD was set (channel occupied above -64 dBm this window).
 */
bool nrf24SampleRpd(uint32_t dwellUs);

/**
 * @brief Power the radio down, hold CE low, and restore the SD-safe chip-select.
 */
void nrf24Idle();

} // namespace hal
} // namespace adversary
