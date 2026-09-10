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

#include <cmath>
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

// --- FSK modem register math (slice-0019 Phase 2) ----------------------------
//
// Unlike OOK, an FSK signal has no envelope to slice: the CC1101 must demodulate
// it, so the deviation, data rate and RX bandwidth must be programmed to match the
// transmitter. These three register groups are pure functions of the target values
// (CC1101 §12/§13, f_xtal = 26 MHz) and are native-tested in test_cc1101_fsk,
// because a wrong mantissa/exponent silently mistunes the demod into noise — the
// same failure mode as a wrong carrier, and just as invisible without a fixture.

/// MDMCFG2 MOD_FORMAT field values (bits [6:4]). Named so a screen never writes a
/// bare 0x03. 4-FSK has no raw-async capture path (2 bits/symbol can't ride one
/// GDO0 line) and MSK is only valid above ~26 kBaud — see slice-0019.
constexpr uint8_t CC1101_MOD_2FSK   = 0x00;
constexpr uint8_t CC1101_MOD_GFSK   = 0x01;
constexpr uint8_t CC1101_MOD_ASK_OOK = 0x03;
constexpr uint8_t CC1101_MOD_4FSK   = 0x04;
constexpr uint8_t CC1101_MOD_MSK    = 0x07;

/// The CC1101 supports MSK only at data rates above this floor (§12). Below it the
/// screen must reject an MSK preset rather than silently mis-tune (slice-0019).
constexpr double CC1101_MSK_MIN_BAUD = 26000.0;

/// DRATE_E (MDMCFG4 low nibble) + DRATE_M (MDMCFG3) split for a data rate.
struct Cc1101DrateRegs {
    uint8_t drateE;  // 0..15  -> MDMCFG4[3:0]
    uint8_t drateM;  // 0..255 -> MDMCFG3
};

/// DEVIATN/MDMCFG4/MDMCFG3 register triple for an FSK modem configuration.
struct Cc1101ModemRegs {
    uint8_t deviatn;  // DEVIATN: DEVIATION_E (bits[6:4]) + DEVIATION_M (bits[2:0])
    uint8_t mdmcfg4;  // CHANBW_E/M (bits[7:4]) + DRATE_E (bits[3:0])
    uint8_t mdmcfg3;  // DRATE_M
};

/**
 * @brief Frequency deviation (Hz) -> DEVIATN byte (CC1101 §16.1).
 *
 * f_dev = (f_xtal / 2^17) * (8 + DEVIATION_M) * 2^DEVIATION_E, with the exponent
 * in [0,7] and the mantissa in [0,7]. Picks the (E,M) pair closest to the target
 * (canonical lowest-exponent form on a tie). 47.6 kHz -> 0x47, matching the OOK
 * block's hand-written DEVIATN and the SmartRF default.
 */
inline uint8_t cc1101DeviatnReg(double devHz) {
    const double base = (CC1101_XTAL_MHZ * 1e6) / 131072.0;  // f_xtal / 2^17
    uint8_t bestE = 0, bestM = 0;
    double bestErr = 1e18;
    for (uint8_t e = 0; e < 8; ++e) {
        const double ideal = devHz / (base * static_cast<double>(1u << e));
        long m = std::lround(ideal) - 8;
        if (m < 0) m = 0;
        if (m > 7) m = 7;
        const double achieved = base * static_cast<double>(8 + m)
                              * static_cast<double>(1u << e);
        const double err = std::fabs(achieved - devHz);
        if (err < bestErr) {
            bestErr = err;
            bestE = e;
            bestM = static_cast<uint8_t>(m);
        }
    }
    return static_cast<uint8_t>((bestE << 4) | bestM);
}

/**
 * @brief Data rate (baud) -> DRATE_E/DRATE_M (CC1101 §12).
 *
 * R = ((256 + DRATE_M) * 2^DRATE_E / 2^28) * f_xtal. The exponent is the floor of
 * log2(R * 2^20 / f_xtal); the mantissa is then solved and rounded, carrying into
 * the exponent if it rounds up to 256. 4.8 kBaud -> {7, 0x83}; 250 kBaud ->
 * {13, 0x3B} (both SmartRF-documented).
 */
inline Cc1101DrateRegs cc1101DrateRegs(double baud) {
    const double fxtal = CC1101_XTAL_MHZ * 1e6;
    int e = static_cast<int>(std::floor(std::log2(baud * 1048576.0 / fxtal)));
    if (e < 0) e = 0;
    if (e > 15) e = 15;
    long m = std::lround(baud * 268435456.0 / (fxtal * static_cast<double>(1u << e)))
           - 256;
    if (m == 256) {  // rounded up a full mantissa: carry into the exponent
        m = 0;
        ++e;
    }
    if (e > 15) {
        e = 15;
        m = 255;
    }
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    return {static_cast<uint8_t>(e), static_cast<uint8_t>(m)};
}

/**
 * @brief RX channel bandwidth (Hz) -> packed CHANBW_E/CHANBW_M nibble (CC1101 §13).
 *
 * BW = f_xtal / (8 * (4 + CHANBW_M) * 2^CHANBW_E), both fields in [0,3]. Returns
 * (CHANBW_E << 2) | CHANBW_M in the low four bits, i.e. the value that occupies
 * MDMCFG4 bits [7:4]. Picks the nearest representable bandwidth. 203 kHz -> 0x8.
 */
inline uint8_t cc1101ChanbwNibble(double bwHz) {
    const double fxtal = CC1101_XTAL_MHZ * 1e6;
    uint8_t bestE = 0, bestM = 0;
    double bestErr = 1e18;
    for (uint8_t e = 0; e < 4; ++e) {
        for (uint8_t m = 0; m < 4; ++m) {
            const double bw = fxtal
                / (8.0 * static_cast<double>(4 + m) * static_cast<double>(1u << e));
            const double err = std::fabs(bw - bwHz);
            if (err < bestErr) {
                bestErr = err;
                bestE = e;
                bestM = m;
            }
        }
    }
    return static_cast<uint8_t>((bestE << 2) | bestM);
}

/**
 * @brief Compose the DEVIATN/MDMCFG4/MDMCFG3 triple for an FSK modem config.
 *
 * MDMCFG4 packs the CHANBW nibble (bits[7:4]) with DRATE_E (bits[3:0]); MDMCFG3 is
 * DRATE_M; DEVIATN is the deviation byte. (47.6 kHz, 4.8 kBaud, 203 kHz) ->
 * {0x47, 0x87, 0x83} — the 0x87 cross-checks the OOK block's MDMCFG4.
 */
inline Cc1101ModemRegs cc1101ModemRegs(double devHz, double baud, double rxBwHz) {
    const Cc1101DrateRegs drate = cc1101DrateRegs(baud);
    const uint8_t chanbw = cc1101ChanbwNibble(rxBwHz);
    return {cc1101DeviatnReg(devHz),
            static_cast<uint8_t>((chanbw << 4) | drate.drateE),
            drate.drateM};
}

/**
 * @brief Whether an FSK modem config is realisable on the CC1101.
 *
 * MSK is only valid above CC1101_MSK_MIN_BAUD (§12); the other schemes have no
 * such floor. The screen filters its presets through this and cc1101ConfigureFsk
 * refuses an invalid one (fail loud) rather than mis-tuning. Pure, native-tested.
 */
inline bool cc1101FskConfigValid(uint8_t modFormat, double dataRateBaud) {
    if (modFormat == CC1101_MOD_MSK) return dataRateBaud >= CC1101_MSK_MIN_BAUD;
    return true;
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

/**
 * @brief Reset + configure the CC1101 for FSK async-serial capture/replay.
 *
 * Programs MOD_FORMAT (@p modFormat, a CC1101_MOD_* FSK value) with the deviation/
 * data-rate/RX-bandwidth register math (cc1101ModemRegs), tunes to @p carrierMHz,
 * and routes the FSK-demodulated NRZ line to GDO0 — the same async-serial path OOK
 * uses, so the RMT capture/replay layer is unchanged; the caller stores the
 * modulation descriptor so replay can rebuild this exact configuration. Requires an
 * owned SD bus (SDManager::spiBus() != nullptr).
 * @return true if the chip acknowledged (VERSION read back as a seated part).
 */
bool cc1101ConfigureFsk(double carrierMHz, uint8_t modFormat, double deviationHz,
                        double dataRateBaud, double rxBwHz);

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
