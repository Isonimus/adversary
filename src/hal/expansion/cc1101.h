/**
 * @file cc1101.h
 * @brief CC1101 sub-GHz driver for OOK raw capture/replay (slice-0003).
 *
 * The CC1101 sits on the SD-shared FSPI bus behind the multi-radio cap; this
 * driver borrows SDManager's SPIClass and the slice-0002 chip-select idiom, so
 * it never bit-bangs and the SD card is re-mounted after use by the caller.
 *
 * The frequency->register math is a pure inline function so it is exercised in
 * test/test_cc1101_freq without hardware; the register-driving functions are
 * firmware-only (defined in cc1101.cpp, TARGET_CARDPUTER).
 */

#pragma once

#include <cstdint>

namespace adversary {
namespace hal {

/// CC1101 FREQ2/FREQ1/FREQ0 carrier-frequency register triple.
struct Cc1101FreqRegs {
    uint8_t freq2;
    uint8_t freq1;
    uint8_t freq0;
};

/// Crystal on the multi-radio cap's CC1101 (26 MHz), the base of the FREQ word.
constexpr double CC1101_XTAL_MHZ = 26.0;

/**
 * @brief Carrier frequency (MHz) -> FREQ2/1/0 registers.
 *
 * FREQ = round(f_carrier * 2^16 / f_xtal); the CC1101 synthesises
 * f_carrier = (f_xtal / 2^16) * FREQ. Pure and inline for native testing.
 * 433.92 MHz -> {0x10, 0xB0, 0x71}, confirmed on-device (slice-0003 spike).
 */
inline Cc1101FreqRegs cc1101FreqRegs(double carrierMHz) {
    uint32_t freq = static_cast<uint32_t>(
        carrierMHz * 65536.0 / CC1101_XTAL_MHZ + 0.5);
    return {static_cast<uint8_t>((freq >> 16) & 0xFF),
            static_cast<uint8_t>((freq >> 8) & 0xFF),
            static_cast<uint8_t>(freq & 0xFF)};
}

/// The OOK band presets the UI offers. Named, never bare literals at call sites.
constexpr double CC1101_FREQ_315_MHZ = 315.0;
constexpr double CC1101_FREQ_43392_MHZ = 433.92;
constexpr double CC1101_FREQ_86835_MHZ = 868.35;
constexpr double CC1101_FREQ_915_MHZ = 915.0;

/// Datasheet RSSI offset (dB) subtracted when converting the raw RSSI register to
/// dBm (CC1101 §17.3, typical value). The band-sweep only ranks bands relatively,
/// so the exact offset is uncritical; this keeps the readout close to true dBm.
constexpr int CC1101_RSSI_OFFSET_DBM = 74;

/**
 * @brief Raw RSSI status-register byte -> dBm (CC1101 §17.3). Pure, native-tested.
 *
 * The byte is a two's-complement value in half-dB steps: >=128 reads as negative
 * (weak), <128 as positive (strong). dBm = rssi_dec/2 - offset. A wrong sign or
 * offset here silently mis-ranks the bands, so it is pinned by test_cc1101_rssi.
 */
inline int16_t cc1101RssiDbm(uint8_t raw) {
    const int rssiDec = (raw >= 128) ? (static_cast<int>(raw) - 256)
                                     : static_cast<int>(raw);
    return static_cast<int16_t>(rssiDec / 2 - CC1101_RSSI_OFFSET_DBM);
}

// --- Firmware register-driving functions (defined in cc1101.cpp) -------------

/**
 * @brief Reset + configure the CC1101 for OOK/ASK async-serial at @p carrierMHz.
 *
 * Polls the VERSION status register for chip-ready after SRES (writing config
 * before the crystal settles silently drops it), then writes the SmartRF-derived
 * OOK register set and tunes to @p carrierMHz. GDO0 becomes the async serial data
 * line (RX: chip drives it; TX: RMT drives it). Requires an owned SD bus
 * (SDManager::spiBus() != nullptr).
 * @return true if the chip acknowledged (VERSION read back as a seated part).
 */
bool cc1101ConfigureOok(double carrierMHz);

/// Strobe into RX and wait for MARCSTATE=RX. @return true iff RX was reached.
bool cc1101EnterRx();

/**
 * @brief Read the RSSI status register and convert to dBm (see cc1101RssiDbm).
 *
 * Must be called while the radio is in RX and has had time to settle; the band
 * sweep enters RX, waits RSSI_SETTLE, then reads. @return the dBm estimate, or
 * INT16_MIN if the SD/FSPI bus is not owned (nothing to read).
 */
int16_t cc1101ReadRssiDbm();

/// Strobe into TX and wait for MARCSTATE=TX. @return true iff TX was reached.
bool cc1101EnterTx();

/// Idle + power-down the radio and restore the SD-safe chip-select state.
void cc1101Idle();

} // namespace hal
} // namespace adversary
