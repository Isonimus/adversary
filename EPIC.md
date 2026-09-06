# The Adversary - Project Epic

## 🎯 Project Overview

**The Adversary** is a M5Stack Cardputer/M5StickC Plus2 compatible red-team focused wireless pentesting tool designed for security professionals and researchers. It provides a comprehensive suite of WiFi attack vectors in a portable, user-friendly package.

---

## 📋 Epic: MVP Development Kickoff

### Vision Statement
Build a robust, modular wireless pentesting firmware that enables security professionals to perform comprehensive WiFi assessments using affordable M5Stack hardware.

### Target Hardware
- **Primary**: M5Stack Cardputer (ESP32-S3)
- **Secondary**: M5StickC Plus2 (ESP32-PICO-D4)

---

## 🚀 MVP Features (Phase 1)

### 1. Core System
| Feature | Priority | Description |
|---------|----------|-------------|
| Boot Splash Screen | P0 | Animated splash with progress bar showing init status |
| SD Card Manager | P0 | First module - handles all file I/O operations |
| Menu System | P0 | Navigation menu accessible via ESC key |
| State Machine | P0 | Manages app states (idle, active, menu, etc.) |
| Display Manager | P0 | Abstracted display handling for different hardware |
| Input Handler | P0 | Unified keyboard/button input processing |

### 2. WiFi Reconnaissance
| Feature | Priority | Description |
|---------|----------|-------------|
| Network Scanner | P0 | Scan and list nearby WiFi networks |
| Client Enumeration | P1 | List connected clients per network |
| Packet Sniffer | P1 | Raw 802.11 packet capture |
| Channel Hopper | P1 | Automatic channel switching for scanning |

### 3. Attack Vectors
| Feature | Priority | Description |
|---------|----------|-------------|
| Deauth Attack | P0 | 802.11 deauthentication frames |
| Handshake Capture | P0 | WPA/WPA2 4-way handshake capture |
| Evil Twin AP | P1 | Rogue access point creation |
| Karma AP | P2 | Auto-responding to probe requests |
| Beacon Spam | P2 | Broadcast fake network beacons |
| Probe Flood | P2 | Probe request flooding |

### 4. Data Management
| Feature | Priority | Description |
|---------|----------|-------------|
| PCAP Export | P0 | Save captures in PCAP format |
| Handshake Storage | P0 | Organized handshake file storage |
| Session Logging | P1 | Activity and event logging |
| Config Persistence | P1 | Save/load user preferences |

---

## 🏗️ Architecture Overview

### Directory Structure
```
/adversary
├── src/
│   ├── main.cpp                 # Entry point
│   ├── config/
│   │   ├── config.h             # Global configuration
│   │   └── pins.h               # Hardware pin definitions
│   ├── core/
│   │   ├── state_machine.cpp/h  # Application state management
│   │   ├── event_bus.cpp/h      # Event-driven communication
│   │   └── task_scheduler.cpp/h # FreeRTOS task management
│   ├── hal/
│   │   ├── display/
│   │   │   ├── display_hal.h    # Display abstraction interface
│   │   │   ├── cardputer_display.cpp/h
│   │   │   └── m5stick_display.cpp/h
│   │   ├── input/
│   │   │   ├── input_hal.h      # Input abstraction interface
│   │   │   ├── cardputer_input.cpp/h
│   │   │   └── m5stick_input.cpp/h
│   │   └── storage/
│   │       └── sd_manager.cpp/h # SD card operations
│   ├── modules/
│   │   ├── scanner/
│   │   │   ├── wifi_scanner.cpp/h
│   │   │   └── channel_hopper.cpp/h
│   │   ├── attack/
│   │   │   ├── deauth.cpp/h
│   │   │   ├── handshake_capture.cpp/h
│   │   │   ├── evil_twin.cpp/h
│   │   │   └── karma_ap.cpp/h
│   │   ├── sniffer/
│   │   │   ├── packet_sniffer.cpp/h
│   │   │   └── packet_parser.cpp/h
│   │   └── pcap/
│   │       └── pcap_writer.cpp/h
│   ├── ui/
│   │   ├── screens/
│   │   │   ├── splash_screen.cpp/h
│   │   │   ├── menu_screen.cpp/h
│   │   │   ├── scanner_screen.cpp/h
│   │   │   └── attack_screen.cpp/h
│   │   ├── components/
│   │   │   ├── progress_bar.cpp/h
│   │   │   ├── list_view.cpp/h
│   │   │   └── status_bar.cpp/h
│   │   └── theme.h              # UI colors and styles
│   └── utils/
│       ├── mac_utils.cpp/h      # MAC address utilities
│       ├── wifi_utils.cpp/h     # WiFi helper functions
│       ├── time_utils.cpp/h     # Time formatting
│       └── string_utils.cpp/h   # String manipulation
├── test/
│   ├── test_main.cpp
│   ├── test_sd_manager.cpp
│   ├── test_state_machine.cpp
│   ├── test_mac_utils.cpp
│   └── mocks/
│       ├── mock_display.h
│       ├── mock_sd.h
│       └── mock_wifi.h
├── data/
│   └── (SPIFFS data files)
├── lib/
│   └── (external libraries)
├── platformio.ini
├── CLAUDE.md
├── EPIC.md
└── README.md
```

### SD Card Structure
```
/adversary/
├── config/
│   └── settings.json           # User preferences
├── captures/
│   ├── handshakes/
│   │   └── SSID_BSSID_timestamp.pcap
│   └── packets/
│       └── capture_timestamp.pcap
├── logs/
│   └── session_timestamp.log
└── wordlists/
    └── (optional wordlists)
```

---

## 🔄 State Machine Design

```
┌─────────────────────────────────────────────────────────┐
│                      BOOT                                │
│  (Hardware init, SD check, splash screen)               │
└─────────────────────┬───────────────────────────────────┘
                      │ Init complete
                      ▼
┌─────────────────────────────────────────────────────────┐
│                      IDLE                                │
│  (Main menu displayed, waiting for input)               │
│                                                          │
│  ESC → Stay in menu                                     │
│  SELECT → Enter selected mode                           │
└─────────────────────┬───────────────────────────────────┘
                      │ Mode selected
                      ▼
┌─────────────────────────────────────────────────────────┐
│                     ACTIVE                               │
│  (Running scan/attack/capture)                          │
│                                                          │
│  ESC → Cancel operation → Return to IDLE                │
│  Operation complete → Return to IDLE                    │
└─────────────────────────────────────────────────────────┘
```

### States Enum
```cpp
enum class AppState {
    BOOT,           // System initialization
    SPLASH,         // Splash screen with progress
    IDLE,           // Menu/waiting for input
    SCANNING,       // WiFi scanning active
    SNIFFING,       // Packet capture active
    ATTACKING,      // Attack in progress
    CAPTURING,      // Handshake capture mode
    AP_RUNNING,     // Evil Twin/Karma AP active
    ERROR,          // Error state
    SHUTDOWN        // Graceful shutdown
};
```

---

## 📅 Sprint Planning

### Sprint 1: Foundation (Week 1-2)
- [x] Project setup and documentation
- [x] PlatformIO configuration for both targets
- [x] SD Card Manager implementation
- [x] Display HAL implementation
- [x] Input HAL implementation (M5Cardputer ADV keyboard)
- [x] Splash screen with progress bar
- [x] Basic state machine (21 tests passing)

### Sprint 2: Core UI (Week 3-4)
- [x] Menu system implementation
- [x] Screen navigation (; and . keys for up/down)
- [x] UI components (progress bar, theme)
- [x] Theme system (red team colors)
- [x] ESC key handling (backtick ` on Cardputer)
- [x] Double-buffered rendering (flicker-free)

### Sprint 3: Reconnaissance (Week 5-6)
- [x] WiFi scanner module (33 tests passing)
- [x] Network list display with scrolling
- [x] Channel hopper (support implemented)
- [ ] Client enumeration
- [x] Signal strength display (bar visualization)

### Sprint 4: Capture (Week 7-8)
- [x] Packet sniffer implementation (37 tests)
- [x] PCAP writer module (32 tests)
- [x] Handshake detection (EAPOL frame detection)
- [x] Sniffer UI screen with capture modes
- [x] Capture session management

### Sprint 5: Attacks (Week 9-10)
- [x] Deauth attack implementation (52 tests)
- [x] Target selection UI (scanner callback integration)
- [x] Attack status display with live stats
- [x] ESP32-S3 deauth bypass (pioarduino + symbol weakening)
- [x] Global canvas for flicker-free screen transitions

### Sprint 5.5: Handshake Capture (Week 10)
- [x] HandshakeCapture module with 4-way handshake state machine
- [x] EAPOL frame parsing and message identification (M1-M4)
- [x] Auto-deauth integration for forced reconnection
- [x] HandshakeScreen UI with capture status and progress
- [x] Handshake-to-PCAP export for hashcat/aircrack
- [x] Unit tests for data structures (36 tests)
- [x] Action menu popup for network selection (D/H/T/I shortcuts)
- [x] Order-independent message capture (any M1-M4 order works)
- [x] BSSID verification to filter handshakes by target network
- [x] Spaced deauth bursts (10s pause) for reliable capture
- [x] ANonce extraction from MSG3 as fallback when MSG1 missed
- [x] Hierarchical menu system with submenus
- [x] Menu component with scroll support and back navigation
- [x] Main menu restructure: Wireless/Bluetooth/Infrared/Settings

### Sprint 6: Rogue AP Framework (Week 11-12)
**Shared Infrastructure:**
- [x] SoftAP module - reusable soft access point management
- [x] ArduinoCaptivePortal - DNS hijacking + HTTP server using Arduino libraries
- [x] Credential capture with fixed array storage
- [x] Dynamic DNS/WebServer creation per session (pointer-based for clean socket release)

**Evil Twin AP:**
- [x] Clone target network SSID/BSSID
- [x] Captive portal with customizable phishing pages
- [x] Credential logging (captured in memory, viewable in UI)
- [x] EvilTwinScreen UI with connection stats
- [x] Full WiFi reset on start (esp_wifi_deinit) for clean socket state
- [x] 2-second AP stabilization delay before DNS start
- [ ] Deauth integration to force victims to rogue AP
- [ ] SD card export of credentials

**Karma AP:**
- [x] Probe request listener (promiscuous mode)
- [x] Auto-respond to captured SSID probes
- [x] SSID rotation based on captured probes
- [x] Client tracking and connection stats
- [x] KarmaScreen UI with captured probes list
- [x] Credential view screen (press C)
- [x] Full WiFi deinit on stop for clean state

**Bug Fixes (Sprint 6.5):**
- [x] Fixed ODR violation - moved getCredentials() from header to cpp file
- [x] Fixed DNS port 53 not released - use pointer-based DNSServer/WebServer with new/delete
- [x] Fixed captive portal detection - use 302 redirects instead of 200 responses
- [x] Added forceStopPortal() for explicit cleanup in stopAllAttacks()
- [x] Added stopAllAttacks() centralized cleanup function
- [x] Fixed packet sniffer false deauth detection (reject WIFI_PKT_MISC, validate frame control)
- [x] Fixed EAPOL detection with proper 802.11 header size calculation (QoS/DS flags)
- [x] Added CRACKABLE vs COMPLETE handshake states with 500ms grace period
- [x] Fixed menu flicker with canvas-based double-buffered rendering
- [x] Fixed SD write failures with queue-based packet buffering (SPI bus contention fix)
- [x] Added periodic PCAP flush and error state display for file writes

### Sprint 7: Beacon & Probe Attacks (Week 13-14)
- [x] Beacon Spam module with multiple modes:
  - Rick Roll lyrics as SSIDs
  - Funny/meme SSID collection (25+ SSIDs)
  - Random SSID generation
  - Single SSID broadcast
  - Custom SSID list support
- [x] Proper 802.11 beacon frame building with tagged parameters
- [x] WPA2 RSN IE support for encrypted network advertisement
- [x] BeaconSpamScreen UI with live stats display
- [x] Configurable channel, interval, BSSID randomization
- [x] Channel hopping during beacon spam
- [x] Probe Flood attack module:
  - Multiple modes: Random SSIDs, SSID List, Targeted, Wildcard
  - Channel hopping across 2.4GHz band
  - MAC address randomization
  - Configurable burst mode and intervals
  - ProbeFloodScreen UI with live statistics
  - 58 unit tests

---

## 🎨 UI/UX Guidelines

### Color Scheme (Red Team Theme)
```cpp
// Primary colors
#define COLOR_BG_PRIMARY    0x0000  // Black
#define COLOR_BG_SECONDARY  0x18E3  // Dark gray
#define COLOR_ACCENT        0xF800  // Red
#define COLOR_ACCENT_LIGHT  0xFB20  // Light red
#define COLOR_TEXT_PRIMARY  0xFFFF  // White
#define COLOR_TEXT_SECONDARY 0x7BEF // Gray
#define COLOR_SUCCESS       0x07E0  // Green
#define COLOR_WARNING       0xFD20  // Orange
#define COLOR_ERROR         0xF800  // Red
```

### Screen Layout (Cardputer: 240x135)
```
┌────────────────────────────────────┐
│ [Status Bar - 20px]                │
├────────────────────────────────────┤
│                                    │
│ [Content Area - 95px]              │
│                                    │
├────────────────────────────────────┤
│ [Action Bar - 20px]                │
└────────────────────────────────────┘
```

---

## ⚠️ Legal & Ethical Considerations

### Disclaimer
This tool is intended for:
- Authorized security testing
- Educational purposes
- Research on owned networks

### Built-in Safeguards
1. Startup warning/disclaimer screen
2. Session logging for accountability

---

## 📊 Success Metrics

### MVP Completion Criteria
- [x] Boot to menu in < 3 seconds
- [x] Successful network scan
- [x] Capture handshake to SD card
- [x] Execute deauth attack
- [x] PCAP files readable by Wireshark
- [ ] All P0 features functional
- [x] Unit test coverage > 70% (343 tests passing)

---

## 🔗 References

- [ESP32 WiFi Raw API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [M5Stack Cardputer Docs](https://docs.m5stack.com/en/core/Cardputer)
- [PCAP Format Specification](https://wiki.wireshark.org/Development/LibpcapFileFormat)
- [802.11 Frame Types](https://en.wikipedia.org/wiki/802.11_Frame_Types)

---

*Last Updated: January 2026*
*Version: 0.6.5-alpha*
