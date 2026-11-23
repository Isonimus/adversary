/**
 * @file cc1101.cpp
 * @brief CC1101 sub-GHz driver over the SD-shared FSPI bus (slice-0003 / 0019).
 *
 * Firmware-only: the register-driving I/O crosses the HAL boundary and only
 * compiles for the Cardputer. The pure carrier/RSSI/FSK-modem register math lives
 * inline in cc1101.h and is native-tested (test/test_cc1101_freq, _rssi, _fsk).
 * OOK and FSK both drive GDO0 as an async-serial line so the RMT layer is shared.
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
#include "expansion_cap.h"
#include "../../config/pins.h"
#include "../storage/sd_manager.h"

#if defined(TARGET_CARDPUTER)
#include <Arduino.h>
#include <SPI.h>
#include "utils/log.h"
#endif

namespace adversary {
namespace hal {

#if defined(TARGET_CARDPUTER)

namespace {

// --- CC1101 register addresses (datasheet single-access addresses) -----------
constexpr uint8_t REG_IOCFG2   = 0x00;
// GDOx_CFG=0x2F = "HW to 0"; with GDOx_INV (bit 6) set, the same pin reads "HW
// to 1" (TI SWRS061 Table 41 -- verified against the datasheet, not memory).
// The Cap CC1101 & NFC's RF_SW1 line is wired to this chip's GDO2 pin, so this
// is how its antenna switch is driven; the legacy cap never reads this value.
constexpr uint8_t GDOX_CFG_HW_TO_0 = 0x2F;
constexpr uint8_t GDOX_INV_BIT     = 0x40;
constexpr uint8_t REG_IOCFG0   = 0x02;
constexpr uint8_t REG_FIFOTHR  = 0x03;
constexpr uint8_t REG_PKTLEN   = 0x06;
constexpr uint8_t REG_PKTCTRL1 = 0x07;
constexpr uint8_t REG_PKTCTRL0 = 0x08;
constexpr uint8_t REG_ADDR     = 0x09;
constexpr uint8_t REG_FSCTRL1  = 0x0B;
constexpr uint8_t REG_FSCTRL0  = 0x0C;
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

// FSK is constant-envelope: a single PA entry (FREND0=0x10 selects index 0), full
// power. No on/off toggling — the data rides the frequency, not the amplitude.
constexpr uint8_t FSK_PATABLE_POWER = 0xC0;

// Chip-ready poll budget after SRES. Spike measured ~350-380 us; give margin.
constexpr uint32_t CHIP_READY_TIMEOUT_US = 10000;
// State-transition poll budget (SRX/STX). Spike measured RX entry ~790-807 us.
constexpr uint32_t STATE_TIMEOUT_US = 5000;

// capSpiSettings() and deselectSdCard() are shared across the cap radios — see
// cap_bus.h (extracted at the third consumer, rule of three).

// Drive the CC1101 chip-select as an output, de-asserted. Kept driven for the
// whole radio session; cc1101Idle() restores it to the SD-safe INPUT_PULLUP.
void selectChipSelectOutput() {
    pinMode(pins::activeCc1101Cs, OUTPUT);
    digitalWrite(pins::activeCc1101Cs, HIGH);
}

void writeReg(SPIClass* bus, uint8_t addr, uint8_t value) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::activeCc1101Cs, LOW);
    bus->transfer(addr);
    bus->transfer(value);
    digitalWrite(pins::activeCc1101Cs, HIGH);
    bus->endTransaction();
}

// Drive the Cap CC1101 & NFC's 2-signal RF switch for the chosen carrier. A
// no-op on the legacy multi-radio cap, which has no such switch. Must run
// AFTER the IOCFG2=0x0D write above: it deliberately overrides that register
// (GDO2 is consumed by this cap's antenna switch, not exposed as a data pin --
// only GDO0/IOCFG0 is, and that write is untouched).
void driveCc1101NfcAntennaSwitch(SPIClass* bus, double carrierMHz) {
    if (resolvedExpansionCap() != ExpansionCap::Cc1101Nfc) return;
    const Cc1101NfcSwitchState sw = cc1101NfcSwitchState(carrierMHz);
    pinMode(pins::CC1101NFC_RFSW0, OUTPUT);
    digitalWrite(pins::CC1101NFC_RFSW0, sw.sw0 ? HIGH : LOW);
    writeReg(bus, REG_IOCFG2, GDOX_CFG_HW_TO_0 | (sw.sw1 ? GDOX_INV_BIT : 0));
    LOG_I("CC1101", "NFC cap antenna switch: %.2f MHz -> SW0=%d SW1=%d",
          carrierMHz, sw.sw0, sw.sw1);
}

uint8_t readStatusReg(SPIClass* bus, uint8_t addr) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::activeCc1101Cs, LOW);
    bus->transfer(READ_BURST | addr);
    uint8_t value = bus->transfer(0x00);
    digitalWrite(pins::activeCc1101Cs, HIGH);
    bus->endTransaction();
    return value;
}

// Datasheet-timed SO/MISO-low wait (SS19.1.1/19.1.2): CHIP_RDYn observed on SO
// while CSn is held low, proving the crystal has stabilised. Best-effort: a
// chip that is already stable (SO already low) returns immediately, and a
// genuinely absent chip still lets the caller proceed to waitChipReady(),
// which is the real pass/fail gate either way.
bool waitMisoLow(uint32_t timeoutUs) {
    uint32_t start = micros();
    while (digitalRead(pins::SD_MISO) != LOW) {
        if (micros() - start > timeoutUs) return false;
    }
    return true;
}

// Full manual power-up/reset per SS19.1.2, Figure 27. strobe(SRES) below is a
// blind "pulse CS, send SRES" -- the datasheet's own text says that is valid
// only "if the user wants to reset the CC1101" from an already-running state;
// first contact after a less certain power-up needs the explicit SO-low
// handshake (wait low, send SRES, wait low again) instead. Cap CC1101 & NFC
// only -- the legacy cap's existing blind strobe is untouched.
constexpr uint32_t XOSC_WAIT_TIMEOUT_US = 5000;  // generous vs typical crystal startup

void cc1101NfcManualReset(SPIClass* bus) {
    const int8_t cs = pins::activeCc1101Cs;
    // No pinMode() on SD_MISO: the shared SPIClass bus already owns and
    // configures that pin (SDManager owns it). Reconfiguring it here was
    // found to destabilise the SD card's own use of the same physical pin,
    // observed on real hardware as repeated "sdWait failed" / "f_mount
    // failed" errors starting right when this code began running. A plain
    // digitalRead() samples the pad level directly regardless of which
    // peripheral the GPIO matrix also routes it to, so no mode change is
    // needed to read it.

    // Wake-up pulse: CS low then high, held at least 40us from the initial low
    // (a plain GPIO wiggle -- the datasheet shows no SPI clocking here).
    uint32_t pulseStart = micros();
    digitalWrite(cs, LOW);
    digitalWrite(cs, HIGH);
    while (micros() - pulseStart < 40) { /* busy-wait the required 40us */ }

    // CS low again, then wait for SO to go low (oscillator stable) before
    // issuing SRES, per Figure 27.
    digitalWrite(cs, LOW);
    waitMisoLow(XOSC_WAIT_TIMEOUT_US);

    bus->beginTransaction(capSpiSettings());
    bus->transfer(STROBE_SRES);
    bus->endTransaction();

    // Wait for SO low again: reset complete, chip in IDLE.
    waitMisoLow(XOSC_WAIT_TIMEOUT_US);
    digitalWrite(cs, HIGH);
}

void strobe(SPIClass* bus, uint8_t command) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::activeCc1101Cs, LOW);
    bus->transfer(command);
    digitalWrite(pins::activeCc1101Cs, HIGH);
    bus->endTransaction();
}

void writePaTable(SPIClass* bus, const uint8_t (&table)[8]) {
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::activeCc1101Cs, LOW);
    bus->transfer(WRITE_BURST | REG_PATABLE);
    for (uint8_t byte : table) bus->transfer(byte);
    digitalWrite(pins::activeCc1101Cs, HIGH);
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
    LOG_E("CC1101", "chip-ready timeout, VERSION=0x%02X (expected 0x%02X)",
          version, CC1101_VERSION_EXPECTED);
    // LOG_E on-device only reaches the SD error-log file; if that file fails
    // to open (e.g. the card is in a bad state), the message above is silently
    // dropped. LOG_W always reaches Serial regardless, so this failure is never
    // invisible again.
    LOG_W("CC1101", "chip-ready timeout, VERSION=0x%02X (expected 0x%02X)",
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
    LOG_E("CC1101", "MARCSTATE never reached 0x%02X", targetState);
    LOG_W("CC1101", "MARCSTATE never reached 0x%02X", targetState);  // see waitChipReady note on LOG_E/SD masking
    return false;
}

}  // namespace

bool cc1101ConfigureOok(double carrierMHz) {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) {
        LOG_W("CC1101", "SPI bus not owned (launcher mount); configure skipped");
        return false;
    }

    deselectSdCard();
    selectChipSelectOutput();

    if (resolvedExpansionCap() == ExpansionCap::Cc1101Nfc) {
        cc1101NfcManualReset(bus);
    } else {
        strobe(bus, STROBE_SRES);
    }
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
    driveCc1101NfcAntennaSwitch(bus, carrierMHz);
    writeReg(bus, REG_ADDR, 0x00);
    writeReg(bus, REG_PKTLEN, 0x00);

    const uint8_t ookTable[8] = {OOK_PATABLE_OFF, OOK_PATABLE_ON, 0, 0, 0, 0, 0, 0};
    writePaTable(bus, ookTable);
    return true;
}

bool cc1101ConfigureFsk(double carrierMHz, uint8_t modFormat, double deviationHz,
                        double dataRateBaud, double rxBwHz) {
    if (!cc1101FskConfigValid(modFormat, dataRateBaud)) {
        LOG_W("CC1101", "MSK below %.0f baud floor (%.0f); FSK configure refused",
              CC1101_MSK_MIN_BAUD, dataRateBaud);
        return false;
    }

    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) {
        LOG_W("CC1101", "SPI bus not owned (launcher mount); FSK configure skipped");
        return false;
    }

    deselectSdCard();
    selectChipSelectOutput();

    if (resolvedExpansionCap() == ExpansionCap::Cc1101Nfc) {
        cc1101NfcManualReset(bus);
    } else {
        strobe(bus, STROBE_SRES);
    }
    if (!waitChipReady(bus)) return false;

    const Cc1101FreqRegs freq = cc1101FreqRegs(carrierMHz);
    const Cc1101ModemRegs modem = cc1101ModemRegs(deviationHz, dataRateBaud, rxBwHz);

    // The FS-calibration / TEST / synth block is deliberately duplicated from
    // cc1101ConfigureOok rather than shared: that block is on-device-verified for
    // OOK, and this FSK path is not yet (slice-0019 defers the round-trip). Keeping
    // them separate means an FSK tuning change can never silently regress the
    // proven OOK path. Extract a shared bring-up when a third config arrives.
    writeReg(bus, REG_FSCTRL1, 0x06);
    writeReg(bus, REG_FSCTRL0, 0x00);
    writeReg(bus, REG_FREQ2, freq.freq2);
    writeReg(bus, REG_FREQ1, freq.freq1);
    writeReg(bus, REG_FREQ0, freq.freq0);
    writeReg(bus, REG_MDMCFG4, modem.mdmcfg4);  // CHANBW (RX BW) + DRATE_E
    writeReg(bus, REG_MDMCFG3, modem.mdmcfg3);  // DRATE_M
    writeReg(bus, REG_MDMCFG2,
             static_cast<uint8_t>((modFormat << 4)));  // MOD_FORMAT, no sync (raw)
    writeReg(bus, REG_MDMCFG1, 0x02);
    writeReg(bus, REG_MDMCFG0, 0xF8);
    writeReg(bus, REG_DEVIATN, modem.deviatn);  // ignored by the chip for MSK
    writeReg(bus, REG_MCSM0, 0x18);     // auto-calibrate on IDLE->RX/TX
    writeReg(bus, REG_FOCCFG, 0x16);
    writeReg(bus, REG_BSCFG, 0x6C);
    writeReg(bus, REG_AGCCTRL2, 0x43);  // SmartRF 2-FSK AGC block
    writeReg(bus, REG_AGCCTRL1, 0x40);
    writeReg(bus, REG_AGCCTRL0, 0x91);
    writeReg(bus, REG_FREND1, 0x56);
    writeReg(bus, REG_FREND0, 0x10);    // single-entry PATABLE (constant envelope)
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
    driveCc1101NfcAntennaSwitch(bus, carrierMHz);
    writeReg(bus, REG_ADDR, 0x00);
    writeReg(bus, REG_PKTLEN, 0x00);

    const uint8_t fskTable[8] = {FSK_PATABLE_POWER, 0, 0, 0, 0, 0, 0, 0};
    writePaTable(bus, fskTable);
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
    releaseChipSelect(pins::activeCc1101Cs);
}

#else  // !TARGET_CARDPUTER

bool cc1101ConfigureOok(double) { return false; }
bool cc1101ConfigureFsk(double, uint8_t, double, double, double) { return false; }
bool cc1101EnterRx() { return false; }
bool cc1101EnterTx() { return false; }
int16_t cc1101ReadRssiDbm() { return INT16_MIN; }
void cc1101Idle() {}

#endif  // TARGET_CARDPUTER

} // namespace hal
} // namespace adversary
