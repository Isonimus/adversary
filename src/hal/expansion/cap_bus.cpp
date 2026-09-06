/**
 * @file cap_bus.cpp
 * @brief Shared SD-bus borrowing helpers for the multi-radio cap (see cap_bus.h).
 */

#include "cap_bus.h"

#if defined(TARGET_CARDPUTER)

#include "../../config/pins.h"

#include <Arduino.h>

namespace adversary {
namespace hal {

namespace {
// Matches the SD mount speed: slow and safe, shared by every cap radio.
constexpr uint32_t CAP_SPI_HZ = 4000000;
}  // namespace

SPISettings capSpiSettings() {
    return SPISettings(CAP_SPI_HZ, MSBFIRST, SPI_MODE0);
}

void deselectSdCard() {
    pinMode(pins::SD_CS, OUTPUT);
    digitalWrite(pins::SD_CS, HIGH);
}

void releaseChipSelect(int8_t pin) {
    pinMode(pin, INPUT_PULLUP);
}

} // namespace hal
} // namespace adversary

#endif  // TARGET_CARDPUTER
