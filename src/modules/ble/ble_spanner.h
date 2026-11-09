/**
 * @file ble_spanner.h
 * @brief Singleton for BLE advertising and interaction detection
 */

#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <vector>
#include <string>
#include "modules/ble/ble_scanner.h" // For BLEDeviceInfo
#include "utils/ble_reconnect_guard.h" // Reconnect-storm loop-breaker (ADR-0134)

namespace adversary {

enum class BleSpamProvider {
    APPLE,
    ANDROID,
    WINDOWS,
    SAMSUNG,
    CAROUSEL,
    HID,   // BadBLE: a real HID device (class chosen via BleHidClass)
    CLONE  // Identity spoofing of a specific target
};

// The HID device class BadBLE presents (ADR-0150). The class selects the report map and
// the GAP appearance at attack start; the device enumerates as exactly this class. Joystick
// is folded into GAMEPAD (same axes+buttons model), so there is no separate joystick class.
enum class BleHidClass {
    KEYBOARD,
    MOUSE,
    GAMEPAD
};

// GAP appearance values (Bluetooth SIG) for each HID class. The appearance is derived from
// the class so the advertised icon always matches the real device (ADR-0150).
constexpr uint16_t BLE_APPEARANCE_KEYBOARD = 0x03C1;
constexpr uint16_t BLE_APPEARANCE_MOUSE    = 0x03C2;
constexpr uint16_t BLE_APPEARANCE_JOYSTICK = 0x03C3; // legacy persisted value -> GAMEPAD
constexpr uint16_t BLE_APPEARANCE_GAMEPAD  = 0x03C4;

// Map a HID class to its GAP appearance value, and back. hidClassForAppearance treats the
// legacy joystick appearance as GAMEPAD and any unknown value as KEYBOARD. Inline so the pure
// mapping is reachable from the native test suite (ble_spanner.cpp is device-only).
inline uint16_t appearanceForHidClass(BleHidClass hidClass) {
    switch (hidClass) {
        case BleHidClass::MOUSE:    return BLE_APPEARANCE_MOUSE;
        case BleHidClass::GAMEPAD:  return BLE_APPEARANCE_GAMEPAD;
        case BleHidClass::KEYBOARD: return BLE_APPEARANCE_KEYBOARD;
    }
    return BLE_APPEARANCE_KEYBOARD; // unreachable; keeps the compiler happy without a default
}

inline BleHidClass hidClassForAppearance(uint16_t appearance) {
    switch (appearance) {
        case BLE_APPEARANCE_MOUSE:    return BleHidClass::MOUSE;
        case BLE_APPEARANCE_JOYSTICK: return BleHidClass::GAMEPAD; // joystick folded into gamepad
        case BLE_APPEARANCE_GAMEPAD:  return BleHidClass::GAMEPAD;
        default:                      return BleHidClass::KEYBOARD;
    }
}

enum class BleSpamPrompt {
    PAIRING,
    ACTION_REQUIRED,
    APPLE_ID_MODAL,      // SourApple specific
    SOFTWARE_UPDATE,     // SourApple specific
    TRANSFER_PHONE,      // SourApple specific
    GUIDED_ACCESS,       // SourApple specific
    BATTERY_LOW,
    LINK_PROMPT
};

enum class KeyboardLayout {
    US,
    ES
};

struct BleSpamConfig {
    BleSpamProvider provider = BleSpamProvider::APPLE;
    BleSpamPrompt prompt = BleSpamPrompt::PAIRING;
    uint8_t intensity = 5; // 1-10
    char name[33] = ""; // Optional override
    BleHidClass hidClass = BleHidClass::KEYBOARD; // HID device class (ADR-0150)
};

class BLESpanner : public NimBLEServerCallbacks {
public:
    static BLESpanner& getInstance() {
        static BLESpanner instance;
        return instance;
    }

    /**
     * @brief Start advertising with specified config
     */
    void start(const BleSpamConfig& config);

    /**
     * @brief Stop advertising
     */
    void stop();

    /**
     * @brief Force full NimBLE teardown (even in HID mode)
     * 
     * Called during screen transitions to reclaim ~100KB heap.
     * Unlike stop(), this unconditionally deinits the BLE stack.
     */
    void forceRelease();
    
    /**
     * @brief Set target for identity spoofing
     */
    void setSpoofTarget(const BLEDeviceInfo& target) { m_spoofTarget = target; }

    /**
     * @brief Periodic update
     */
    void update();

    /**
     * @brief Get telemetry
     */
    uint32_t getPacketCount() const { return m_packetCount; }
    uint32_t getInteractionCount() const { return m_interactionCount; }
    float getPPS() const { return m_pps; }
    const std::string& getLastPayload() const { return m_lastPayload; }

    /**
     * @brief Set intensity level (1-10)
     */
    void setIntensity(uint8_t level);

    // NimBLE Server Callback
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;

    /**
     * @brief Report an SMP auth result for the current HID link (ADR-0134).
     *
     * Called from the security callback. A successful (encrypted) auth clears the reconnect-storm
     * guard; a failure leaves the link marked unencrypted so onDisconnect counts it as churn.
     */
    void notifyAuthResult(bool encrypted);

    /**
     * @brief Check if a device is currently paired/bonded (for HID)
     */
    bool isConnected() const { return m_connectedCount > 0; }
    
    /**
     * @brief Set keyboard layout
     */
    void setLayout(KeyboardLayout layout);

    /**
     * @brief Get current keyboard layout
     */
    KeyboardLayout getLayout() const { return m_layout; }

    /**
     * @brief Send a keystroke (requires a connection and KEYBOARD class)
     */
    void sendKey(uint8_t keycode, uint8_t modifiers = 0);
    void typeString(const std::string& text);

    /**
     * @brief Send one mouse report (requires a connection and MOUSE class).
     *
     * dx/dy/wheel are relative, signed (-127..127); buttons is a bitmask of the
     * hid_report::MOUSE_BUTTON_* constants. The caller composes press/release and movement
     * from successive reports.
     */
    void sendMouseMove(int8_t dx, int8_t dy, uint8_t buttons = 0, int8_t wheel = 0);

    /**
     * @brief Send one gamepad report (requires a connection and GAMEPAD class).
     *
     * x/y are absolute axis positions, signed (-127..127); buttons is a bitmask of buttons
     * 1..8 (bit 0 = button 1).
     */
    void sendGamepad(int8_t x, int8_t y, uint8_t buttons = 0);

private:
    BLESpanner();
    ~BLESpanner() = default;

    void updatePayload();
    std::vector<uint8_t> buildPayload();

    // Shared precondition for every HID input send: a live input characteristic, at least one
    // connection, and the active class matching the report map built at start. Fails loud (logs
    // and returns false) on a mismatch, because a report that does not match the report map is a
    // silent protocol error on the host (ADR-0150).
    bool hidInputReady(BleHidClass expected, const char* op);

    BleSpamConfig m_config;
    volatile bool m_active;
    volatile bool m_taskRunning;
    volatile uint32_t m_packetCount;
    volatile uint32_t m_interactionCount;
    volatile uint32_t m_connectedCount;
    volatile float m_pps;
    uint32_t m_lastUpdate;
    std::string m_lastPayload;
    KeyboardLayout m_layout;
    
    NimBLEAdvertising* m_pAdvertising;
    NimBLEServer* m_pServer;
    NimBLECharacteristic* m_pHidInput; // For keystrokes
    
    BLEDeviceInfo m_spoofTarget; // Target for CLONE mode

    void generateRandomMac();
    uint8_t m_originalMac[6];

    TaskHandle_t m_taskHandle;
    static void spamTask(void* pvParameters);
    void runSpamCycle();
    int m_lastModelIdx;

    // BadBLE reconnect-storm loop-breaker (ADR-0134). m_hidLinkEncrypted tracks whether the
    // current link reached an encrypted state; a drop while still unencrypted is a failed-auth
    // reconnect and feeds the guard. Once the guard trips, m_hidAdvertiseSuppressed stops the
    // auto-re-advertise so the device no longer spins against a host's stale bond.
    utils::ReconnectChurnGuard m_churnGuard;
    volatile bool m_hidLinkEncrypted;
    volatile bool m_hidAdvertiseSuppressed;
};

} // namespace adversary
