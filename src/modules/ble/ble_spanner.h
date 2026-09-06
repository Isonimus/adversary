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

namespace adversary {

enum class BleSpamProvider {
    APPLE,
    ANDROID,
    WINDOWS,
    SAMSUNG,
    CAROUSEL,
    HID_KEYBOARD, // BadBLE targeting Linux/Everything
    CLONE         // Identity spoofing of a specific target
};

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
    uint16_t appearance = 0; // Optional override (0 = use default/HID)
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
     * @brief Send a keystroke (requires connection)
     */
    void sendKey(uint8_t keycode, uint8_t modifiers = 0);
    void typeString(const std::string& text);

private:
    BLESpanner();
    ~BLESpanner() = default;

    void updatePayload();
    std::vector<uint8_t> buildPayload();

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
};

} // namespace adversary
