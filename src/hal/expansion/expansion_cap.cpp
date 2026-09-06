/**
 * @file expansion_cap.cpp
 * @brief Shared-bus SPI probes for the multi-radio expansion cap (slice-0002).
 *
 * The CC1101 and NRF24L01 sit on the SD SPI data lines (SCK/MISO/MOSI on
 * G40/G39/G14). Rather than bit-bang — which drives those pins directly and
 * leaves the SD bus wedged with no clean recovery — the probes borrow the exact
 * SPIClass instance SDManager mounts the card on and transact through it with
 * proper begin/endTransaction locking and per-device chip-selects. That is the
 * standard multiple-devices-on-one-bus pattern; the SD bus is never disturbed.
 *
 * When the launcher pre-mounted the card (SDManager Method 1), we do not own the
 * bus instance and cannot safely re-init FSPI, so the probes are skipped and the
 * operator override decides (slice-0002 / ADR-0001). Cardputer-only.
 */

#include "expansion_cap.h"

#include "cap_bus.h"
#include "../../config/pins.h"
#include "../storage/sd_manager.h"

#if defined(TARGET_CARDPUTER)
#include <Arduino.h>
#include <SPI.h>
#endif

namespace adversary {
namespace hal {

namespace {

ExpansionCap s_cap = ExpansionCap::None;
CapProbeResult s_lastProbe;

#if defined(TARGET_CARDPUTER)

// CC1101 command/register constants
constexpr uint8_t CC1101_READ_BURST = 0xC0;  // read + burst bits for status regs
constexpr uint8_t CC1101_REG_VERSION = 0x31;
constexpr uint8_t CC1101_STROBE_SIDLE = 0x36;
constexpr uint8_t CC1101_STROBE_SPWD = 0x39;  // enter power-down

// NRF24L01 command/register constants
constexpr uint8_t NRF24_CMD_R_REGISTER = 0x00;
constexpr uint8_t NRF24_CMD_W_REGISTER = 0x20;
constexpr uint8_t NRF24_REG_CONFIG = 0x00;
constexpr uint8_t NRF24_REG_RF_CH = 0x05;
constexpr uint8_t NRF24_RF_CH_PROBE = 0x2A;       // arbitrary R/W pattern for read-back
constexpr uint8_t NRF24_CONFIG_POWERDOWN = 0x08;  // EN_CRC set, PWR_UP cleared

// capSpiSettings() / deselectSdCard() / releaseChipSelect() are shared with the
// CC1101 and NRF24 drivers — see cap_bus.h (extracted at the third consumer).

#endif  // TARGET_CARDPUTER

}  // namespace

bool probeCC1101() {
#if defined(TARGET_CARDPUTER)
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return false;

    deselectSdCard();
    pinMode(pins::CC1101_CS, OUTPUT);
    digitalWrite(pins::CC1101_CS, HIGH);

    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(CC1101_READ_BURST | CC1101_REG_VERSION);
    uint8_t version = bus->transfer(0x00);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();

    releaseChipSelect(pins::CC1101_CS);

    s_lastProbe.cc1101Version = version;
    // A floating/absent bus reads all-zero or all-one; a seated part returns a
    // fixed silicon revision. The exact value is pinned in slice-0002 As built.
    bool present = (version != 0x00 && version != 0xFF);
    Serial.printf("[Cap] CC1101 VERSION=0x%02X -> %s\n", version,
                  present ? "present" : "absent");
    return present;
#else
    return false;
#endif
}

bool probeNRF24() {
#if defined(TARGET_CARDPUTER)
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return false;

    deselectSdCard();
    // CE low keeps the radio in standby while we touch its registers.
    pinMode(pins::NRF24_CE, OUTPUT);
    digitalWrite(pins::NRF24_CE, LOW);
    pinMode(pins::NRF24_CS, OUTPUT);
    digitalWrite(pins::NRF24_CS, HIGH);

    bus->beginTransaction(capSpiSettings());

    // Save the current RF_CH so the probe is non-destructive.
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(NRF24_CMD_R_REGISTER | NRF24_REG_RF_CH);
    uint8_t original = bus->transfer(0xFF);
    digitalWrite(pins::NRF24_CS, HIGH);

    // Write a known pattern.
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(NRF24_CMD_W_REGISTER | NRF24_REG_RF_CH);
    bus->transfer(NRF24_RF_CH_PROBE);
    digitalWrite(pins::NRF24_CS, HIGH);

    // Read it back.
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(NRF24_CMD_R_REGISTER | NRF24_REG_RF_CH);
    uint8_t readback = bus->transfer(0xFF);
    digitalWrite(pins::NRF24_CS, HIGH);

    // Restore the original value.
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(NRF24_CMD_W_REGISTER | NRF24_REG_RF_CH);
    bus->transfer(original);
    digitalWrite(pins::NRF24_CS, HIGH);

    bus->endTransaction();
    releaseChipSelect(pins::NRF24_CS);

    bool present = (readback == NRF24_RF_CH_PROBE);
    Serial.printf("[Cap] NRF24 RF_CH read-back=0x%02X -> %s\n", readback,
                  present ? "present" : "absent");
    return present;
#else
    return false;
#endif
}

#if defined(TARGET_CARDPUTER)
namespace {

// Drive both radios to a known idle so nothing emits (and the NRF24 board LED
// stops lighting from a floating CE) before the operator asks for RF. Only
// called once the multi-radio cap is the resolved cap, so driving NRF24_CE
// (G3) low never holds a LoRa cap's RST line.
void capRadiosToIdle() {
    SPIClass* bus = SDManager::getInstance().spiBus();
    if (!bus) return;

    deselectSdCard();
    // NRF24: hold CE low and power the radio down.
    pinMode(pins::NRF24_CE, OUTPUT);
    digitalWrite(pins::NRF24_CE, LOW);
    pinMode(pins::NRF24_CS, OUTPUT);
    digitalWrite(pins::NRF24_CS, HIGH);
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::NRF24_CS, LOW);
    bus->transfer(NRF24_CMD_W_REGISTER | NRF24_REG_CONFIG);
    bus->transfer(NRF24_CONFIG_POWERDOWN);
    digitalWrite(pins::NRF24_CS, HIGH);
    bus->endTransaction();
    releaseChipSelect(pins::NRF24_CS);
    // NRF24_CE stays driven LOW as the idle hold — not on the SPI bus.

    // CC1101: idle then power-down strobes.
    pinMode(pins::CC1101_CS, OUTPUT);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->beginTransaction(capSpiSettings());
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(CC1101_STROBE_SIDLE);
    digitalWrite(pins::CC1101_CS, HIGH);
    digitalWrite(pins::CC1101_CS, LOW);
    bus->transfer(CC1101_STROBE_SPWD);
    digitalWrite(pins::CC1101_CS, HIGH);
    bus->endTransaction();
    releaseChipSelect(pins::CC1101_CS);
}

}  // namespace
#endif  // TARGET_CARDPUTER

ExpansionCap detectExpansionCap(CapOverride ov) {
    bool cc1101 = false;
    bool nrf24 = false;
    if (ov == CapOverride::Auto) {
#if defined(TARGET_CARDPUTER)
        if (!SDManager::getInstance().spiBus()) {
            Serial.println("[Cap] SPI bus not owned (launcher mount); "
                           "auto-probe skipped, override decides");
        }
#endif
        cc1101 = probeCC1101();
        nrf24 = probeNRF24();
    }
    s_lastProbe.cc1101Present = cc1101;
    s_lastProbe.nrf24Present = nrf24;
    s_cap = resolveExpansionCap(cc1101, nrf24, ov);

#if defined(TARGET_CARDPUTER)
    if (s_cap == ExpansionCap::MultiRadio) {
        capRadiosToIdle();
    }
#endif
    return s_cap;
}

const CapProbeResult& lastCapProbe() {
    return s_lastProbe;
}

ExpansionCap resolvedExpansionCap() {
    return s_cap;
}

} // namespace hal
} // namespace adversary
