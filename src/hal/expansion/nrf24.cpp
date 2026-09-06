/**
 * @file nrf24.cpp
 * @brief NRF24L01+ RPD scanner over the SD-shared FSPI bus (slice-0006).
 *
 * Firmware-only: the register I/O crosses the HAL boundary and only compiles for
 * the Cardputer. The pure channel->frequency math lives inline in nrf24.h and is
 * native-tested (test/test_nrf24_channel).
 *
 * The scan is the well-known nRF24 "poor man's spectrum analyzer" (Nordic
 * datasheet): put the radio in RX on a channel, dwell briefly, read the RPD
 * (Received Power Detector) latch. RPD needs no packet reception, address match
 * or CRC, so it detects arbitrary 2.4 GHz energy above the chip's fixed -64 dBm
 * gate — which is exactly why the analyzer is verifiable against ambient WiFi/BLE
 * with no target hardware.
 */

#include "nrf24.h"

#include "cap_bus.h"
#include "../../config/pins.h"
#include "../storage/sd_manager.h"

#if defined(TARGET_CARDPUTER)
#include <Arduino.h>
#include <SPI.h>
#endif

namespace adversary {
namespace hal {

#if defined(TARGET_CARDPUTER)

namespace {

// SPI command bits (ORed onto a 5-bit register address).
constexpr uint8_t CMD_R_REGISTER = 0x00;
constexpr uint8_t CMD_W_REGISTER = 0x20;

// Registers used by the scanner.
constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_EN_AA  = 0x01;  // auto-ack enable (per pipe)
constexpr uint8_t REG_RF_CH  = 0x05;
constexpr uint8_t REG_RPD    = 0x09;  // Received Power Detector (bit 0)

// CONFIG bits.
constexpr uint8_t CONFIG_PWR_UP  = 0x02;
constexpr uint8_t CONFIG_PRIM_RX = 0x01;
constexpr uint8_t CONFIG_RX_ON   = CONFIG_PWR_UP | CONFIG_PRIM_RX;
constexpr uint8_t CONFIG_POWER_DOWN = 0x00;  // PWR_UP cleared

constexpr uint8_t EN_AA_DISABLE_ALL = 0x00;  // no auto-ack: we never reply
constexpr uint8_t RPD_OCCUPIED_MASK = 0x01;

// Timing (datasheet). Tpd2stby: power-down -> standby settling before the first
// RX; Tstby2a: standby -> active (RX) settling that must elapse before RPD is
// meaningful on a channel.
constexpr uint32_t PWR_UP_SETTLE_US = 5000;
constexpr uint32_t RX_SETTLE_US = 130;

void writeReg(SPIClass* bus, uint8_t reg, uint8_t value) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(CMD_W_REGISTER | reg);
    bus->transfer(value);
    digitalWrite(pins::NRF24_CS, HIGH);
    bus->endTransaction();
}

uint8_t readReg(SPIClass* bus, uint8_t reg) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(CMD_R_REGISTER | reg);
    uint8_t value = bus->transfer(0xFF);
    digitalWrite(pins::NRF24_CS, HIGH);
    bus->endTransaction();
    return value;
}

}  // namespace

bool nrf24BeginRxScan() {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) {
        Serial.println("[NRF24] SPI bus not owned (launcher mount); scan skipped");
        return false;
    }

    deselectSdCard();
    // CE low keeps the radio in standby while we configure it; CS driven, idle.
    pinMode(pins::NRF24_CE, OUTPUT);
    digitalWrite(pins::NRF24_CE, LOW);
    pinMode(pins::NRF24_CS, OUTPUT);
    digitalWrite(pins::NRF24_CS, HIGH);

    writeReg(bus, REG_EN_AA, EN_AA_DISABLE_ALL);
    writeReg(bus, REG_CONFIG, CONFIG_RX_ON);
    delayMicroseconds(PWR_UP_SETTLE_US);  // Tpd2stby before the first RX
    return true;
}

bool nrf24SetChannel(uint8_t channel) {
    if (channel > NRF24_MAX_CHANNEL) return false;
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return false;
    writeReg(bus, REG_RF_CH, channel);
    return true;
}

bool nrf24SampleRpd(uint32_t dwellUs) {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return false;

    // CE high enters RX; dwell (settling + listen), then drop back to standby and
    // read the latched RPD. CE is a GPIO, not on the SPI bus, so this never
    // disturbs SD.
    digitalWrite(pins::NRF24_CE, HIGH);
    delayMicroseconds(RX_SETTLE_US + dwellUs);
    digitalWrite(pins::NRF24_CE, LOW);

    return (readReg(bus, REG_RPD) & RPD_OCCUPIED_MASK) != 0;
}

void nrf24Idle() {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return;

    deselectSdCard();
    digitalWrite(pins::NRF24_CE, LOW);
    pinMode(pins::NRF24_CS, OUTPUT);
    digitalWrite(pins::NRF24_CS, HIGH);
    writeReg(bus, REG_CONFIG, CONFIG_POWER_DOWN);

    releaseChipSelect(pins::NRF24_CS);
    // NRF24_CE stays driven LOW as the idle hold — not on the SPI bus.
}

#else  // !TARGET_CARDPUTER

bool nrf24BeginRxScan() { return false; }
bool nrf24SetChannel(uint8_t) { return false; }
bool nrf24SampleRpd(uint32_t) { return false; }
void nrf24Idle() {}

#endif  // TARGET_CARDPUTER

} // namespace hal
} // namespace adversary
