/**
 * @file cc1101.cpp
 * @brief CC1101 sub-GHz OOK driver over the SD-shared FSPI bus (slice-0003).
 *
 * Firmware-only: the register-driving I/O crosses the HAL boundary and only
 * compiles for the Cardputer. The pure carrier->register math lives inline in
 * cc1101.h and is native-tested (test/test_cc1101_freq).
 *
 * The register set and timings here are exactly the ones a throwaway on-device
 * spike proved on the real Cardputer ADV before slice-0003 was frozen: the
 * SmartRF-derived OOK/async-serial block (transcribed from the MIT-licensed
 * LSatan/SmartRC-CC1101 lib, so no new dependency), VERSION seated == 0x14 after
 * SRES (~350-380 us), and MARCSTATE == RX after SRX (~790-807 us). The one
 * generalisation over the spike is the carrier frequency: FREQ2/1/0 come from
 * cc1101FreqRegs(carrierMHz) so the UI's presets tune the radio.
 */

#include "cc1101.h"

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

// --- CC1101 register addresses (datasheet single-access addresses) -----------
constexpr uint8_t REG_IOCFG2   = 0x00;
constexpr uint8_t REG_IOCFG0   = 0x02;
constexpr uint8_t REG_FIFOTHR  = 0x03;
constexpr uint8_t REG_PKTLEN   = 0x06;
constexpr uint8_t REG_PKTCTRL1 = 0x07;
constexpr uint8_t REG_PKTCTRL0 = 0x08;
constexpr uint8_t REG_ADDR     = 0x09;
constexpr uint8_t REG_FSCTRL1  = 0x0B;
constexpr uint8_t REG_FREQ2    = 0x0D;
constexpr uint8_t REG_FREQ1    = 0x0E;
constexpr uint8_t REG_FREQ0    = 0x0F;
constexpr uint8_t REG_MDMCFG4  = 0x10;
constexpr uint8_t REG_MDMCFG3  = 0x11;
constexpr uint8_t REG_MDMCFG2  = 0x12;
constexpr uint8_t REG_MDMCFG1  = 0x13;
constexpr uint8_t REG_MDMCFG0  = 0x14;
constexpr uint8_t REG_DEVIATN  = 0x15;
constexpr uint8_t REG_MCSM0    = 0x18;
constexpr uint8_t REG_FOCCFG   = 0x19;
constexpr uint8_t REG_BSCFG    = 0x1A;
constexpr uint8_t REG_AGCCTRL2 = 0x1B;
constexpr uint8_t REG_AGCCTRL1 = 0x1C;
constexpr uint8_t REG_AGCCTRL0 = 0x1D;
constexpr uint8_t REG_FREND1   = 0x21;
constexpr uint8_t REG_FREND0   = 0x22;
constexpr uint8_t REG_FSCAL3   = 0x23;
constexpr uint8_t REG_FSCAL2   = 0x24;
constexpr uint8_t REG_FSCAL1   = 0x25;
constexpr uint8_t REG_FSCAL0   = 0x26;
constexpr uint8_t REG_FSTEST   = 0x29;
constexpr uint8_t REG_TEST2    = 0x2C;
constexpr uint8_t REG_TEST1    = 0x2D;
constexpr uint8_t REG_TEST0    = 0x2E;
constexpr uint8_t REG_RSSI     = 0x34;   // status register (burst-read only)
constexpr uint8_t REG_VERSION  = 0x31;   // status register (burst-read only)
constexpr uint8_t REG_MARCSTATE = 0x35;  // status register (burst-read only)
constexpr uint8_t REG_PATABLE  = 0x3E;

// SPI header bits ORed onto an address.
constexpr uint8_t WRITE_BURST = 0x40;
constexpr uint8_t READ_BURST  = 0xC0;  // status regs must be read with burst set

// Command strobes.
constexpr uint8_t STROBE_SRES  = 0x30;
constexpr uint8_t STROBE_SRX   = 0x34;
constexpr uint8_t STROBE_STX   = 0x35;
constexpr uint8_t STROBE_SIDLE = 0x36;
constexpr uint8_t STROBE_SPWD  = 0x39;  // power-down

// MARCSTATE main-radio-state values (low 5 bits).
constexpr uint8_t MARC_STATE_MASK = 0x1F;
constexpr uint8_t MARC_STATE_RX   = 0x0D;
constexpr uint8_t MARC_STATE_TX   = 0x13;

// The seated CC1101 reports VERSION 0x14 on this cap (spike-measured). Reading
// it back is both the chip-ready gate after SRES and the presence check: writing
// config before VERSION reads true silently drops every write (spike finding).
constexpr uint8_t CC1101_VERSION_EXPECTED = 0x14;

// OOK PATABLE: index 0 = carrier off, index 1 = full power. FREND0=0x11 selects
// the 2-entry table so the OOK modulator toggles between these two.
constexpr uint8_t OOK_PATABLE_OFF = 0x00;
constexpr uint8_t OOK_PATABLE_ON  = 0xC0;

// Chip-ready poll budget after SRES. Spike measured ~350-380 us; give margin.
constexpr uint32_t CHIP_READY_TIMEOUT_US = 10000;
// State-transition poll budget (SRX/STX). Spike measured RX entry ~790-807 us.
constexpr uint32_t STATE_TIMEOUT_US = 5000;

// capSpiSettings() and deselectSdCard() are shared across the cap radios — see
// cap_bus.h (extracted at the third consumer, rule of three).

// Drive the CC1101 chip-select as an output, de-asserted. Kept driven for the
// whole radio session; cc1101Idle() restores it to the SD-safe INPUT_PULLUP.
void selectChipSelectOutput() {
    pinMode(pins::CC1101_CS, OUTPUT);
    digitalWrite(pins::CC1101_CS, HIGH);
}

void writeReg(SPIClass* bus, uint8_t addr, uint8_t value) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(addr);
    bus->transfer(value);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();
}

uint8_t readStatusReg(SPIClass* bus, uint8_t addr) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(READ_BURST | addr);
    uint8_t value = bus->transfer(0x00);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();
    return value;
}

void strobe(SPIClass* bus, uint8_t command) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(command);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();
}

void writePaTable(SPIClass* bus) {
    const uint8_t table[8] = {OOK_PATABLE_OFF, OOK_PATABLE_ON, 0, 0, 0, 0, 0, 0};
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(WRITE_BURST | REG_PATABLE);
    for (uint8_t byte : table) bus->transfer(byte);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();
}

// Poll VERSION until it reads the seated value, proving the chip is up after
// SRES so config writes will stick. Returns false (fail loud) if it never does.
bool waitChipReady(SPIClass* bus) {
    uint32_t start = micros();
    uint8_t version = 0;
    do {
        version = readStatusReg(bus, REG_VERSION);
        if (version == CC1101_VERSION_EXPECTED) return true;
    } while (micros() - start < CHIP_READY_TIMEOUT_US);
    Serial.printf("[CC1101] chip-ready timeout, VERSION=0x%02X (expected 0x%02X)\n",
                  version, CC1101_VERSION_EXPECTED);
    return false;
}

// Strobe into @p targetState and poll MARCSTATE until it is reached.
bool enterState(uint8_t strobeCommand, uint8_t targetState) {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return false;

    deselectSdCard();
    selectChipSelectOutput();

    strobe(bus, STROBE_SIDLE);
    strobe(bus, strobeCommand);

    uint32_t start = micros();
    do {
        uint8_t marc = readStatusReg(bus, REG_MARCSTATE) & MARC_STATE_MASK;
        if (marc == targetState) return true;
    } while (micros() - start < STATE_TIMEOUT_US);
    Serial.printf("[CC1101] MARCSTATE never reached 0x%02X\n", targetState);
    return false;
}

}  // namespace

bool cc1101ConfigureOok(double carrierMHz) {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) {
        Serial.println("[CC1101] SPI bus not owned (launcher mount); configure skipped");
        return false;
    }

    deselectSdCard();
    selectChipSelectOutput();

    strobe(bus, STROBE_SRES);
    if (!waitChipReady(bus)) return false;

    const Cc1101FreqRegs freq = cc1101FreqRegs(carrierMHz);

    writeReg(bus, REG_FSCTRL1, 0x06);
    writeReg(bus, REG_FREQ2, freq.freq2);
    writeReg(bus, REG_FREQ1, freq.freq1);
    writeReg(bus, REG_FREQ0, freq.freq0);
    writeReg(bus, REG_MDMCFG4, 0x87);   // RX BW ~203 kHz, DRATE_E=7
    writeReg(bus, REG_MDMCFG3, 0x93);   // DRATE_M=147 -> ~5 kBaud
    writeReg(bus, REG_MDMCFG2, 0x30);   // ASK/OOK, no sync/preamble (raw async)
    writeReg(bus, REG_MDMCFG1, 0x02);
    writeReg(bus, REG_MDMCFG0, 0xF8);
    writeReg(bus, REG_DEVIATN, 0x47);
    writeReg(bus, REG_MCSM0, 0x18);     // auto-calibrate on IDLE->RX/TX
    writeReg(bus, REG_FOCCFG, 0x16);
    writeReg(bus, REG_BSCFG, 0x1C);
    writeReg(bus, REG_AGCCTRL2, 0xC7);
    writeReg(bus, REG_AGCCTRL1, 0x00);
    writeReg(bus, REG_AGCCTRL0, 0xB2);
    writeReg(bus, REG_FREND1, 0x56);
    writeReg(bus, REG_FREND0, 0x11);    // ASK/OOK PA shaping (2-entry PATABLE)
    writeReg(bus, REG_FSCAL3, 0xE9);
    writeReg(bus, REG_FSCAL2, 0x2A);
    writeReg(bus, REG_FSCAL1, 0x00);
    writeReg(bus, REG_FSCAL0, 0x1F);
    writeReg(bus, REG_FSTEST, 0x59);
    writeReg(bus, REG_TEST2, 0x81);
    writeReg(bus, REG_TEST1, 0x35);
    writeReg(bus, REG_TEST0, 0x09);
    writeReg(bus, REG_FIFOTHR, 0x47);
    writeReg(bus, REG_PKTCTRL1, 0x04);
    writeReg(bus, REG_PKTCTRL0, 0x32);  // async serial, infinite length
    writeReg(bus, REG_IOCFG2, 0x0D);
    writeReg(bus, REG_IOCFG0, 0x0D);    // GDO0 = async serial data (RX out / TX in)
    writeReg(bus, REG_ADDR, 0x00);
    writeReg(bus, REG_PKTLEN, 0x00);

    writePaTable(bus);
    return true;
}

bool cc1101EnterRx() {
    return enterState(STROBE_SRX, MARC_STATE_RX);
}

int16_t cc1101ReadRssiDbm() {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return INT16_MIN;  // bus not owned: nothing to read (fail loud)
    return cc1101RssiDbm(readStatusReg(bus, REG_RSSI));
}

bool cc1101EnterTx() {
    return enterState(STROBE_STX, MARC_STATE_TX);
}

void cc1101Idle() {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return;

    deselectSdCard();
    selectChipSelectOutput();
    strobe(bus, STROBE_SIDLE);
    strobe(bus, STROBE_SPWD);

    // Restore the SD-safe de-asserted chip-select (matches the mount-time state).
    releaseChipSelect(pins::CC1101_CS);
}

#else  // !TARGET_CARDPUTER

bool cc1101ConfigureOok(double) { return false; }
bool cc1101EnterRx() { return false; }
bool cc1101EnterTx() { return false; }
int16_t cc1101ReadRssiDbm() { return INT16_MIN; }
void cc1101Idle() {}

#endif  // TARGET_CARDPUTER

} // namespace hal
} // namespace adversary
