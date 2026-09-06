# CLAUDE.md - The Adversary Development Guidelines

> This document serves as the primary reference for AI-assisted development on The Adversary project. Keep this document updated as the project evolves.

---

## 🎯 Project Identity

**Name**: The Adversary  
**Type**: Red-team wireless pentesting firmware  
**Targets**: M5Stack Cardputer (ESP32-S3), M5StickC Plus2 (ESP32-PICO-D4)  
**Framework**: PlatformIO with Arduino framework  
**Language**: C++ (C++17 standard)

---

## 📁 Project Structure

```
/adversary/
├── src/                    # Source code
│   ├── main.cpp           # Entry point
│   ├── config/            # Configuration headers
│   ├── core/              # Core system (state, events, tasks)
│   ├── hal/               # Hardware Abstraction Layer
│   ├── modules/           # Feature modules (scanner, attack, etc.)
│   ├── ui/                # User interface (screens, components)
│   └── utils/             # Utility functions
├── test/                  # Unit tests
│   └── mocks/            # Mock objects for testing
├── lib/                   # External libraries
├── data/                  # SPIFFS data files
├── platformio.ini         # Build configuration
├── adr/                   # Architecture decision records
├── slices/                # Feature slice specs
└── CLAUDE.md             # This file
```

---

## 🏛️ Architecture Principles

### 1. Hardware Abstraction Layer (HAL)
All hardware interactions go through abstraction interfaces:

```cpp
// Example: Display HAL
class IDisplay {
public:
    virtual void init() = 0;
    virtual void clear() = 0;
    virtual void drawText(int x, int y, const char* text) = 0;
    virtual void drawRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void update() = 0;
    virtual ~IDisplay() = default;
};

// Platform-specific implementations
class CardputerDisplay : public IDisplay { /* ... */ };
class M5StickDisplay : public IDisplay { /* ... */ };
```

### 2. Module Pattern
Each feature is a self-contained module:

```cpp
// Module interface
class IModule {
public:
    virtual const char* getName() const = 0;
    virtual void init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void update() = 0;  // Called each loop iteration
    virtual bool isActive() const = 0;
    virtual ~IModule() = default;
};
```

### 3. Event-Driven Communication
Modules communicate through an event bus for decoupled architecture:

```cpp
// Event types defined in src/core/event_types.h
// Categories: WiFi, Attacks, Handshake, AP, WPS, Sniffer, BLE, RFID, 
//             Wardriving, System, UI, IR

// Subscribing to events (in screen/handler)
#include "core/event_bus.h"

void MyScreen::init() {
    handlerId_ = EventBus::getInstance().subscribe(
        EventType::HANDSHAKE_CAPTURED,
        [this](const EventData& evt) {
            // Access typed payload
            const char* ssid = evt.payload.handshake.ssid;
            showNotification("Handshake: %s", ssid);
        }
    );
}

void MyScreen::deinit() {
    EventBus::getInstance().unsubscribe(handlerId_);
}

// Publishing events (in module)
EventData event(EventType::CREDENTIAL_CAPTURED);
strncpy(event.payload.credential.ssid, ssid, 32);
strncpy(event.payload.credential.username, user, 63);
EventBus::getInstance().publish(event);

// ISR-safe queuing (for WiFi/BLE callbacks)
EventBus::getInstance().queue(event);  // Processed in main loop
```

**Key Files:**
- `src/core/event_types.h` - Event enum (45+ types)
- `src/core/event_data.h` - Typed payload structs
- `src/core/event_bus.h/.cpp` - Singleton with subscribe/publish/queue

**Currently Emitting Events:**
- `HANDSHAKE_CAPTURED` - From `handshake_capture.cpp`
- `CREDENTIAL_CAPTURED` - From `evil_twin.cpp`, `karma_ap.cpp`
- `WIFI_SCAN_COMPLETED` - From `wifi_scanner.cpp`
- `ATTACK_STATE_CHANGED` - From `deauth.cpp`

### 4. State Machine
Central state management controls application flow:

```cpp
enum class AppState {
    BOOT,
    SPLASH,
    IDLE,
    SCANNING,
    SNIFFING,
    ATTACKING,
    CAPTURING,
    AP_RUNNING,
    ERROR,
    SHUTDOWN
};
```

---

## 📝 Code Standards

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `WifiScanner`, `PacketSniffer` |
| Functions | camelCase | `scanNetworks()`, `parsePacket()` |
| Variables | camelCase | `networkCount`, `isActive` |
| Constants | SCREAMING_SNAKE | `MAX_NETWORKS`, `SCAN_TIMEOUT_MS` |
| Macros | SCREAMING_SNAKE | `DEBUG_LOG()`, `ASSERT()` |
| Files | snake_case | `wifi_scanner.cpp`, `mac_utils.h` |
| Private members | m_ prefix | `m_isRunning`, `m_packetCount` |
| Pointers | p prefix | `pBuffer`, `pDisplay` |

### File Organization

```cpp
// header_file.h
#pragma once

// System includes
#include <stdint.h>
#include <vector>

// Library includes
#include <Arduino.h>

// Project includes
#include "config/config.h"
#include "utils/mac_utils.h"

namespace adversary {

// Forward declarations
class SomeOtherClass;

// Constants
constexpr uint32_t SOME_CONSTANT = 100;

// Class definition
class MyClass {
public:
    // Constructors/Destructor
    MyClass();
    ~MyClass();
    
    // Public methods
    void doSomething();
    
private:
    // Private members
    uint32_t m_value;
};

} // namespace adversary
```

### Function Design

#### Pure Functions Preferred
```cpp
// GOOD: Pure function - no side effects
MacAddress parseMacString(const char* macStr);

// GOOD: Pure function with explicit output
bool parsePacket(const uint8_t* data, size_t len, PacketInfo* outInfo);

// AVOID: Hidden side effects
void processPacket(const uint8_t* data);  // What does it do?
```

#### Error Handling
```cpp
// Use Result type for operations that can fail
template<typename T>
struct Result {
    bool success;
    T value;
    const char* error;
};

// Example usage
Result<NetworkInfo> scanNetwork(const char* ssid);

// Or use optional for nullable returns
std::optional<NetworkInfo> findNetwork(const char* ssid);
```

#### Parameter Guidelines
```cpp
// Use const references for read-only complex types
void displayNetwork(const NetworkInfo& info);

// Use pointers for optional parameters
void scan(ScanOptions* options = nullptr);

// Use output parameters for multiple returns
bool capture(PacketInfo* outPacket, size_t* outSize);
```

### Memory Management

```cpp
// PREFER: Stack allocation when possible
NetworkInfo network;

// PREFER: Smart pointers for heap allocation
std::unique_ptr<PacketBuffer> buffer = std::make_unique<PacketBuffer>();

// AVOID: Raw new/delete
PacketBuffer* buffer = new PacketBuffer();  // NO!

// For ESP32 memory constraints, use RAII patterns
class ScopedBuffer {
public:
    ScopedBuffer(size_t size) : m_data(static_cast<uint8_t*>(ps_malloc(size))) {}
    ~ScopedBuffer() { if (m_data) free(m_data); }
    uint8_t* data() { return m_data; }
private:
    uint8_t* m_data;
};
```

---

## 🧪 Testing Standards

### Unit Test Structure
```cpp
// test/test_mac_utils.cpp
#include <unity.h>
#include "utils/mac_utils.h"

void setUp() {
    // Runs before each test
}

void tearDown() {
    // Runs after each test
}

void test_parseMacString_validInput() {
    MacAddress result = parseMacString("AA:BB:CC:DD:EE:FF");
    
    TEST_ASSERT_EQUAL_UINT8(0xAA, result.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, result.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, result.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, result.bytes[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, result.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, result.bytes[5]);
}

void test_parseMacString_invalidInput() {
    MacAddress result = parseMacString("invalid");
    
    TEST_ASSERT_TRUE(result.isZero());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parseMacString_validInput);
    RUN_TEST(test_parseMacString_invalidInput);
    return UNITY_END();
}
```

### Mock Objects
```cpp
// test/mocks/mock_display.h
#pragma once
#include "hal/display/display_hal.h"

class MockDisplay : public IDisplay {
public:
    void init() override { initCalled = true; }
    void clear() override { clearCalled = true; }
    void drawText(int x, int y, const char* text) override {
        lastTextX = x;
        lastTextY = y;
        lastText = text;
    }
    // ... other methods
    
    // Test inspection
    bool initCalled = false;
    bool clearCalled = false;
    int lastTextX = 0;
    int lastTextY = 0;
    std::string lastText;
};
```

### Test Coverage Requirements
- **Utils**: 90%+ coverage (pure functions, easy to test)
- **Core**: 80%+ coverage (state machine, event bus)
- **Modules**: 70%+ coverage (business logic)
- **HAL**: Mock testing only (hardware dependent)
- **UI**: Integration tests where possible

---

## 🔧 Build & Development

### PlatformIO Commands
```bash
# Build for Cardputer
pio run -e cardputer

# Build for M5StickC Plus2
pio run -e m5stick

# Upload to connected device
pio run -e cardputer -t upload

# Run tests (native)
pio test -e native

# Clean build
pio run -t clean

# Monitor serial output
pio device monitor -b 115200
```

### Environment Configuration
```ini
; platformio.ini
[env]
framework = arduino
monitor_speed = 115200
build_flags = 
    -std=gnu++17
    -DCORE_DEBUG_LEVEL=3
lib_deps = 
    bblanchon/ArduinoJson@^6.21.0

[env:cardputer]
platform = espressif32
board = m5stack-stamps3
build_flags = 
    ${env.build_flags}
    -DTARGET_CARDPUTER

[env:m5stick]
platform = espressif32
board = m5stick-c
build_flags = 
    ${env.build_flags}
    -DTARGET_M5STICK

[env:native]
platform = native
build_flags = 
    -std=c++17
    -DUNIT_TEST
```

---

## 📦 Module Implementation Checklist

When implementing a new module:

- [ ] Create header with interface in `src/modules/<name>/<name>.h`
- [ ] Implement in `src/modules/<name>/<name>.cpp`
- [ ] Add any utility functions to `src/utils/`
- [ ] Create unit tests in `test/test_<name>.cpp`
- [ ] Add mock objects if needed in `test/mocks/`
- [ ] Update state machine if new states needed
- [ ] Create UI screen in `src/ui/screens/`
- [ ] Document public API with comments
- [ ] Add to module registry/factory

---

## 🎨 UI Development

### Screen Lifecycle
```cpp
class IScreen {
public:
    virtual void onEnter() = 0;    // Called when screen becomes active
    virtual void onExit() = 0;     // Called when leaving screen
    virtual void update() = 0;     // Called each frame
    virtual void render() = 0;     // Draw to display
    virtual void handleInput(InputEvent event) = 0;
    virtual ~IScreen() = default;
};
```

### Component System
```cpp
// Reusable UI components
class ProgressBar : public IComponent {
public:
    void setProgress(float percent);  // 0.0 - 1.0
    void setColors(uint16_t bg, uint16_t fill);
    void render(IDisplay* display, int x, int y, int width, int height);
};

class ListView : public IComponent {
public:
    void setItems(const std::vector<std::string>& items);
    void setSelectedIndex(int index);
    int getSelectedIndex() const;
    void render(IDisplay* display, int x, int y, int width, int height);
};
```

---

## 🚨 Common Pitfalls

### ESP32 Specific
1. **Stack Overflow**: Default task stack is 8KB, increase for complex operations
2. **No PSRAM (invariant)**: The deployment device (Cardputer ADV) and the StampS3-based
   original Cardputer have **no PSRAM chip**. `ps_malloc()` buys no external RAM — the
   ~205 KB internal SRAM is the entire budget; upload/TLS headroom comes from the canvas
   purge (`SystemManager::prepareForMemoryIntensiveTask()`), not PSRAM. **Do not** set
   `board_build.arduino.memory_type = qio_opi` in `platformio.ini` — it forces octal-PSRAM
   boot init that fails on a chip-less board. (Re-derived twice before it was recorded; see
   slices/0008.)
3. **WiFi Promiscuous**: Must call `esp_wifi_set_promiscuous(true)` before sniffing
4. **Watchdog**: Long operations need `vTaskDelay()` or `yield()`

### Cardputer Expansion-Header Pins ⟷ Keyboard Matrix (CRITICAL)

**Scope: original Cardputer (StampS3) only.** This applies to `board_M5Cardputer`,
whose keyboard is the GPIO matrix (`IOMatrixKeyboardReader`). The **Cardputer ADV**
(`board_M5CardputerADV`) moves the keyboard to a TCA8418 I2C expander
(`TCA8418KeyboardReader`), freeing these GPIOs — so the conflict below does **not**
apply there. The community's dual-radio caps target the ADV for exactly this reason.
Our build target is `m5stack-stamps3` (the original), so the conflict is live.

The Cardputer's top-side expansion header (where the LoRa cap and the CC1101/NRF24
cap attach) breaks out GPIOs that are **also the keyboard matrix**. The M5Cardputer
library scans the matrix continuously using (deployment note: the **actual device
is a Cardputer ADV**, whose I2C keyboard frees these pins, so on real hardware the
conflict is **dormant** — this section governs the original-Cardputer path only. The
build env is still `m5stack-stamps3`; adding a proper `cardputer_adv` target is a
tracked follow-up):

- **Demux/column outputs**: `G8, G9, G11`
- **Row inputs** (`INPUT_PULLUP`): `G13, G15, G3, G4, G5, G6, G7`

The **SPI data bus is clear** (`G40 SCK / G39 MISO / G14 MOSI` are not matrix pins),
but **every cap chip-select / control line lands on a matrix ROW**:

| Cap | Pins on matrix rows |
|-----|---------------------|
| CC1101/NRF24 cap | CC1101 CS=`G15`, GDO0=`G13`, NRF24 CS=`G4`, CE=`G3`, IR-TX=`G6`, IR-RX=`G5` |
| LoRa 1262 cap | NSS=`G5`, RST=`G3`, BUSY=`G6`, IRQ=`G4`, GPS UART=`G13/G15` |

**Invariant**: a radio and the keyboard cannot drive these shared pins in the same
instant. Rules for any code touching an expansion-header peripheral:

1. **De-assert every cap chip-select before SD mount** — a floating CS drives MISO
   and corrupts SD init. Do it *before* `M5Cardputer.begin()` (see `setup()` in
   `main.cpp`) and in `SDManager::init()`. Use a driven `OUTPUT HIGH` only on a pin
   unique to the attached cap; use `INPUT_PULLUP` on pins that the *other* cap drives
   (e.g. G13/G15 = LoRa GPS UART) so you never fight a real driver.
2. **Radio transactions must be mutually exclusive with the keyboard scan** — repurpose
   a row pin as CS/control only under a mutex, in short bursts, then restore it to
   `INPUT_PULLUP`. Reading a row you've driven mid-scan yields phantom keypresses.
3. **`G7` (row) and `G11` (demux) are the only matrix pins no cap uses** — keys on the
   `G7` row stay scannable during radio activity. Route the always-available
   **cancel/ESC** to a `G7`-row key so the operator can abort even mid-transmit.
4. **Timing-critical raw RF** (bit-banged OOK replay, jamming) monopolizes its pins for
   the waveform duration; chunk it and poll `G7` between chunks, or run RF on the second
   core with the UI/keyboard on the other.

### Memory
1. **String Fragmentation**: Prefer `char[]` over `String` for frequent operations
2. **Buffer Sizes**: Pre-allocate packet buffers, don't create in callbacks
3. **Global State**: Minimize, use dependency injection instead

### Concurrency
1. **ISR Safety**: Callbacks from WiFi are in ISR context - defer work to task
2. **Mutex Protection**: Use `SemaphoreHandle_t` for shared resources
3. **Queue Communication**: Use FreeRTOS queues for task communication

---

## 📋 Quick Reference

### Key Files
| Purpose | File |
|---------|------|
| Entry point | `src/main.cpp` |
| Global config | `src/config/config.h` |
| State machine | `src/core/state_machine.cpp` |
| SD operations | `src/hal/storage/sd_manager.cpp` |
| WiFi scanning | `src/modules/scanner/wifi_scanner.cpp` |
| PCAP writing | `src/modules/pcap/pcap_writer.cpp` |

### Important Constants
```cpp
// src/config/config.h
constexpr uint32_t SCAN_TIMEOUT_MS = 5000;
constexpr uint32_t DEAUTH_INTERVAL_MS = 100;
constexpr uint32_t MAX_NETWORKS = 50;
constexpr uint32_t MAX_CLIENTS = 100;
constexpr uint32_t PACKET_BUFFER_SIZE = 2048;
constexpr const char* SD_BASE_PATH = "/adversary";
```

### ESC Key Behavior
- **In IDLE/Menu**: Navigate/select menu items
- **In ACTIVE state**: Cancel current operation, return to IDLE
- **Double ESC**: Force return to main menu from any screen

---

## 🔄 Development Workflow

1. **Pick a task** from the issue tracker or the `slices/` backlog
2. **Create branch**: `feature/<name>` or `fix/<name>`
3. **Write tests first** (TDD encouraged)
4. **Implement feature** following code standards
5. **Run tests**: `pio test -e native`
6. **Test on hardware**: Upload and verify
7. **Update documentation** if needed
8. **Create PR** with description of changes

---

## 📝 Commit Message Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Examples:
```
feat(scanner): add channel hopping support

Implements automatic channel switching during network scan.
Configurable hop interval via settings.

Closes #12
```

```
fix(sd): handle missing SD card gracefully

- Add SD presence check on init
- Show error screen if SD missing
- Allow retry without reboot
```

---

*Last Updated: January 3, 2026*  
*Project Version: 0.1.0-alpha*
