/**
 * @file cap_bus.h
 * @brief Shared SD-bus borrowing helpers for the multi-radio expansion cap.
 *
 * The CC1101, NRF24L01 and their probes all sit on the SD FSPI data lines and
 * transact by borrowing SDManager's SPIClass rather than bit-banging (ADR-0001 /
 * slice-0002). Three tiny primitives were copied verbatim into every consumer
 * (expansion_cap.cpp, cc1101.cpp); the NRF24 driver is the third, so they live
 * here once (rule of three). Firmware-only: the definitions drive real pins and
 * only compile for the Cardputer.
 */

#pragma once

#include <cstdint>

#if defined(TARGET_CARDPUTER)

#include <SPI.h>

namespace adversary {
namespace hal {

/// The shared cap SPI clock: the slow, safe SD-mount speed (slice-0002). Every
/// cap radio transacts at this rate so none outruns the borrowed bus.
SPISettings capSpiSettings();

/// De-select the SD card for the duration of a radio transaction. An asserted
/// SD_CS while the radio is clocked corrupts the card (it reads radio traffic as
/// commands → CRC/token errors). The SD driver re-drives SD_CS on its next
/// access, so leaving it HIGH is safe until the caller remounts.
void deselectSdCard();

/// Restore a chip-select to the SD-safe de-asserted state (INPUT_PULLUP), which
/// matches the state the SD mount re-applies — so a radio never leaves a pin
/// driving MISO after it is done.
void releaseChipSelect(int8_t pin);

} // namespace hal
} // namespace adversary

#endif  // TARGET_CARDPUTER
