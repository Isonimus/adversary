#pragma once

/**
 * @file pins.h
 * @brief Hardware pin definitions for different target platforms
 */

#include <stdint.h>

namespace adversary {
namespace pins {

// ===========================================
// M5Stack Cardputer Pin Definitions
// ===========================================
#if defined(TARGET_CARDPUTER)

// SD Card (directly accessible, directly mounted)
constexpr int8_t SD_CS = 12;
constexpr int8_t SD_MOSI = 14;
constexpr int8_t SD_MISO = 39;
constexpr int8_t SD_CLK = 40;

// Display (handled by M5Unified)
constexpr int8_t LCD_CS = 37;
constexpr int8_t LCD_DC = 34;
constexpr int8_t LCD_RST = 33;
constexpr int8_t LCD_BL = 38;

// Keyboard (I2C via M5Cardputer library)
constexpr int8_t KB_INT = 46;

// IR Transmitter
constexpr int8_t IR_TX = 44;

// Speaker
constexpr int8_t SPEAKER = 41;

// Battery ADC
constexpr int8_t BAT_ADC = 10;

// GPS Module — Grove port (AT6668)
constexpr int8_t GPS_RX = 1;   // ESP32 RX ← GPS TX
constexpr int8_t GPS_TX = 2;   // ESP32 TX → GPS RX

// GPS Module — Cap LoRa 1262 on-board GPS (ATGM336H)
constexpr int8_t CAP_GPS_RX = 15;  // ESP32 RX ← module TX
constexpr int8_t CAP_GPS_TX = 13;  // ESP32 TX → module RX

// Cap LoRa 1262 (SX1262) — shares SPI bus with SD card
// NSS must be pulled HIGH before SD init to prevent bus contention
constexpr int8_t LORA_NSS  = 5;
constexpr int8_t LORA_RST  = 3;
constexpr int8_t LORA_BUSY = 6;
constexpr int8_t LORA_IRQ = 4;

// Cap CC1101 + NRF24L01 + IR (multi-radio cap) — shares the SD SPI bus.
// Uses the SAME top-side header as the LoRa cap; only one cap is attached at a
// time, so these pin numbers deliberately overlap the LORA_*/CAP_GPS_* defs
// above (e.g. G15 = CC1101 CS here vs CAP_GPS_RX there). Both radio CS lines
// must be de-asserted before SD init — see setup() in main.cpp and SDManager.
constexpr int8_t CC1101_CS   = 15;  // CC1101 chip-select (NSS)
constexpr int8_t CC1101_GDO0 = 13;  // CC1101 GDO0 (labelled "CE/io0" on the cap)
constexpr int8_t NRF24_CS    = 4;   // NRF24L01 chip-select (CSN)
constexpr int8_t NRF24_CE    = 3;   // NRF24L01 chip-enable
constexpr int8_t CAP_IR_TX   = 6;   // Cap IR transmitter (3 emitters, one line) — distinct from built-in IR_TX (44)
constexpr int8_t CAP_IR_RX   = 5;   // Cap IR receiver

// ===========================================
// M5StickC Plus2 Pin Definitions
// ===========================================
#elif defined(TARGET_M5STICK)

// SD Card (via external module if connected)
constexpr int8_t SD_CS = -1;  // No built-in SD
constexpr int8_t SD_MOSI = -1;
constexpr int8_t SD_MISO = -1;
constexpr int8_t SD_CLK = -1;

// Display (handled by M5Unified)
constexpr int8_t LCD_CS = 5;
constexpr int8_t LCD_DC = 23;
constexpr int8_t LCD_RST = 18;
constexpr int8_t LCD_BL = -1;

// Buttons
constexpr int8_t BTN_A = 37;
constexpr int8_t BTN_B = 39;
constexpr int8_t BTN_PWR = -1;  // Power button

// IR Transmitter
constexpr int8_t IR_TX = 19;

// No multi-radio cap on the M5Stick; the cap-IR pins exist only so the
// cross-target IR capability code (irTxPin) compiles here (slice-0004).
constexpr int8_t CAP_IR_TX = -1;
constexpr int8_t CAP_IR_RX = -1;

// Speaker (buzzer)
constexpr int8_t SPEAKER = 2;

// Battery ADC
constexpr int8_t BAT_ADC = -1;  // Managed by AXP192

// GPS Module (external, if connected via Grove port)
constexpr int8_t GPS_RX = 33;  // ESP32 RX from GPS TX (Grove port)
constexpr int8_t GPS_TX = 32;  // ESP32 TX to GPS RX (Grove port)

// Cap on-board GPS is exclusive to the Cardputer expansion header (Cardputer
// ADV); the StickC has no such header and uses Grove GPS only. These are -1 so
// the shared GPS probe (gps_config.h GPS_PIN_SETS) compiles here and skips the
// absent "Cap" pin set at runtime (rxPin < 0), matching the native branch.
constexpr int8_t CAP_GPS_RX = -1;
constexpr int8_t CAP_GPS_TX = -1;

// ===========================================
// Native/Test Build (No real pins)
// ===========================================
#else

constexpr int8_t SD_CS = -1;
constexpr int8_t SD_MOSI = -1;
constexpr int8_t SD_MISO = -1;
constexpr int8_t SD_CLK = -1;
constexpr int8_t LCD_CS = -1;
constexpr int8_t LCD_DC = -1;
constexpr int8_t LCD_RST = -1;
constexpr int8_t LCD_BL = -1;
constexpr int8_t SPEAKER = -1;
constexpr int8_t BAT_ADC = -1;
constexpr int8_t IR_TX = -1;
constexpr int8_t CAP_IR_TX  = -1;  // cap-IR pins (slice-0004): present so the IR
constexpr int8_t CAP_IR_RX  = -1;  // capability code is native-testable
constexpr int8_t GPS_RX     = -1;
constexpr int8_t GPS_TX     = -1;
constexpr int8_t CAP_GPS_RX = -1;
constexpr int8_t CAP_GPS_TX = -1;

#endif

} // namespace pins
} // namespace adversary
