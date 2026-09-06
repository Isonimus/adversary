#pragma once

/**
 * @file config.h
 * @brief Global configuration constants for The Adversary
 * 
 * This file contains all compile-time configuration values.
 * Hardware-specific values should be in pins.h
 */

#include <stdint.h>

namespace adversary {
namespace config {

// ===========================================
// Version Information
// ===========================================
constexpr const char* VERSION = "0.7.0-alpha";
constexpr const char* BUILD_DATE = __DATE__;
constexpr const char* PROJECT_NAME = "The Adversary";

// ===========================================
// SD Card Configuration
// ===========================================
constexpr const char* SD_BASE_PATH = "/adversary";
constexpr const char* SD_CONFIG_PATH = "/adversary/config";
constexpr const char* SD_CAPTURES_PATH = "/adversary/captures";
constexpr const char* SD_HANDSHAKES_PATH = "/adversary/captures/handshakes";
constexpr const char* SD_PACKETS_PATH = "/adversary/captures/packets";
constexpr const char* SD_CREDENTIALS_PATH = "/adversary/captures/credentials";
constexpr const char* SD_WARDRIVING_PATH = "/adversary/captures/wardriving";
constexpr const char* SD_LOGS_PATH = "/adversary/logs";
constexpr const char* SD_SETTINGS_FILE = "/adversary/config/adversary.conf";
constexpr const char* SD_WHITELIST_FILE = "/adversary/config/ssid_whitelist.json";
constexpr const char* SD_BADUSB_PATH    = "/adversary/badusb";
constexpr const char* SD_SUBGHZ_PATH    = "/adversary/subghz";  // captured OOK signals (slice-0003)
constexpr const char* SD_IR_PATH        = "/adversary/ir";      // captured IR codes (slice-0004)

// ===========================================
// WiFi Scanner Configuration
// ===========================================
constexpr uint32_t SCAN_TIMEOUT_MS = 5000;
constexpr uint32_t CHANNEL_HOP_INTERVAL_MS = 200;
constexpr uint32_t MAX_NETWORKS = 50;
constexpr uint32_t MAX_CLIENTS_PER_NETWORK = 20;
constexpr uint32_t MAX_TOTAL_CLIENTS = 100;
constexpr uint8_t WIFI_CHANNEL_MIN = 1;
constexpr uint8_t WIFI_CHANNEL_MAX = 14;

// ===========================================
// Packet Capture Configuration
// ===========================================
constexpr uint32_t PACKET_BUFFER_SIZE = 2048;
constexpr uint32_t PACKET_QUEUE_SIZE = 32;
constexpr uint32_t PCAP_FLUSH_INTERVAL_MS = 1000;
constexpr uint32_t MAX_CAPTURE_SIZE_BYTES = 50 * 1024 * 1024;  // 50MB

// ===========================================
// Attack Configuration
// ===========================================
constexpr uint32_t DEAUTH_INTERVAL_MS = 100;
constexpr uint32_t DEAUTH_PACKETS_PER_BURST = 5;
constexpr uint32_t BEACON_INTERVAL_MS = 100;
constexpr uint32_t PROBE_FLOOD_INTERVAL_MS = 10;

// ===========================================
// UI Configuration
// ===========================================
constexpr uint32_t SPLASH_MIN_DURATION_MS = 2000;
constexpr uint32_t MENU_SCROLL_DELAY_MS = 150;
constexpr uint32_t STATUS_UPDATE_INTERVAL_MS = 500;
constexpr uint32_t DOUBLE_PRESS_TIMEOUT_MS = 300;

// ===========================================
// Display Dimensions
// ===========================================
#if defined(TARGET_CARDPUTER)
    constexpr uint16_t SCREEN_WIDTH = 240;
    constexpr uint16_t SCREEN_HEIGHT = 135;
#elif defined(TARGET_M5STICK)
    constexpr uint16_t SCREEN_WIDTH = 240;
    constexpr uint16_t SCREEN_HEIGHT = 135;
#else
    constexpr uint16_t SCREEN_WIDTH = 240;
    constexpr uint16_t SCREEN_HEIGHT = 135;
#endif

// UI Layout
constexpr uint16_t STATUS_BAR_HEIGHT = 20;
constexpr uint16_t ACTION_BAR_HEIGHT = 20;
constexpr uint16_t CONTENT_HEIGHT = SCREEN_HEIGHT - STATUS_BAR_HEIGHT - ACTION_BAR_HEIGHT;

// ===========================================
// Task Configuration (FreeRTOS)
// ===========================================
constexpr uint32_t TASK_STACK_SIZE_SMALL = 2048;
constexpr uint32_t TASK_STACK_SIZE_MEDIUM = 4096;
constexpr uint32_t TASK_STACK_SIZE_LARGE = 8192;
constexpr uint8_t TASK_PRIORITY_LOW = 1;
constexpr uint8_t TASK_PRIORITY_NORMAL = 2;
constexpr uint8_t TASK_PRIORITY_HIGH = 3;
constexpr uint8_t TASK_PRIORITY_CRITICAL = 4;

// ===========================================
// Debug Configuration
// ===========================================
#if CORE_DEBUG_LEVEL > 0
    #define DEBUG_ENABLED 1
#else
    #define DEBUG_ENABLED 0
#endif

#define LOG_TAG "ADV"

} // namespace config
} // namespace adversary
