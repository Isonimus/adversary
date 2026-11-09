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

// Cycle the class selector forward/backward over the three real classes, wrapping at the ends
// (ADR-0150). Joystick is not in the cycle: it has no separate class. Inline so the pure cycle
// is reachable from the native test suite (the UI screen that drives it is device-only).
inline BleHidClass nextHidClass(BleHidClass hidClass) {
    switch (hidClass) {
        case BleHidClass::KEYBOARD: return BleHidClass::MOUSE;
        case BleHidClass::MOUSE:    return BleHidClass::GAMEPAD;
        case BleHidClass::GAMEPAD:  return BleHidClass::KEYBOARD;
    }
    return BleHidClass::KEYBOARD;
}

inline BleHidClass prevHidClass(BleHidClass hidClass) {
    switch (hidClass) {
        case BleHidClass::KEYBOARD: return BleHidClass::GAMEPAD;
        case BleHidClass::MOUSE:    return BleHidClass::KEYBOARD;
        case BleHidClass::GAMEPAD:  return BleHidClass::MOUSE;
    }
    return BleHidClass::KEYBOARD;
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

// Per-provider/prompt effectiveness tag (ADR-0151). Advisory copy for the operator, NOT a gate:
// every provider stays selectable (old devices exist, and the field operator decides). The tag is
// derived from the 2026-11-09 current-OS survey in ADR-0151, not bench-confirmed in-repo.
//   LIVE      - current OS still fires the prompt reliably
//   THROTTLED - current OS rate-limits / cooldowns apply, but rotation defeats them per-device
//   LEGACY    - mitigated in a recent OS (iOS 17.2 for the SourApple vectors); kept for older devices
enum class BleSpamEffectiveness { LIVE, THROTTLED, LEGACY };

inline BleSpamEffectiveness effectivenessFor(BleSpamProvider provider, BleSpamPrompt prompt) {
    switch (provider) {
        case BleSpamProvider::WINDOWS: return BleSpamEffectiveness::LIVE;      // Swift Pair still fires
        case BleSpamProvider::ANDROID: return BleSpamEffectiveness::THROTTLED; // Fast Pair cooldowns
        case BleSpamProvider::APPLE:
            switch (prompt) {
                case BleSpamPrompt::APPLE_ID_MODAL:
                case BleSpamPrompt::SOFTWARE_UPDATE:
                case BleSpamPrompt::TRANSFER_PHONE:
                case BleSpamPrompt::GUIDED_ACCESS:
                    return BleSpamEffectiveness::LEGACY;                       // patched iOS 17.2
                default:
                    return BleSpamEffectiveness::THROTTLED;                    // proximity pairing, cooldowns
            }
        case BleSpamProvider::SAMSUNG:  return BleSpamEffectiveness::THROTTLED; // EasySetup, unconfirmed on current One UI
        case BleSpamProvider::CAROUSEL: return BleSpamEffectiveness::THROTTLED; // mixed bag; worst-case tag
        case BleSpamProvider::HID:      return BleSpamEffectiveness::LIVE;      // a real HID device, not a popup
        case BleSpamProvider::CLONE:    return BleSpamEffectiveness::LIVE;      // depends on the cloned target
    }
    return BleSpamEffectiveness::THROTTLED;
}

inline const char* effectivenessLabel(BleSpamEffectiveness e) {
    switch (e) {
        case BleSpamEffectiveness::LIVE:      return "LIVE";
        case BleSpamEffectiveness::THROTTLED: return "THROTTLED";
        case BleSpamEffectiveness::LEGACY:    return "LEGACY";
    }
    return "?";
}

// --- Pacing (ADR-0151) --------------------------------------------------------------------------
// Intensity (1..10) drives exactly one knob: the rotation period (how often the MAC + payload
// swap). The radio advertising interval is now a fixed constant (set once at start()) because a
// tighter adv interval re-sends the SAME device faster and does not create new popups: rotation is
// what produces distinct popups. This replaces the two fighting formulas that disagreed at the
// extremes (200 ms rotation vs 20 ms adv interval at intensity 10).
constexpr uint32_t BLE_SPAM_ROTATION_MIN_MS = 200;
constexpr uint32_t BLE_SPAM_ROTATION_MAX_MS = 1000;
constexpr uint16_t BLE_SPAM_ADV_INTERVAL_UNITS_MIN = 0x20; // 20 ms in 0.625 ms units
constexpr uint16_t BLE_SPAM_ADV_INTERVAL_UNITS_MAX = 0x40; // 40 ms

// Map intensity 1..10 to the rotation period in ms. Clamped at both ends so an out-of-range
// knob never breaks the loop timing. Pure; native-tested.
inline uint32_t rotationPeriodForIntensity(uint8_t level) {
    if (level < 1)  level = 1;
    if (level > 10) level = 10;
    uint32_t period = 1100u - (uint32_t)level * 100u; // 1 -> 1000, 10 -> 100 (then clamped to 200)
    if (period < BLE_SPAM_ROTATION_MIN_MS) period = BLE_SPAM_ROTATION_MIN_MS;
    if (period > BLE_SPAM_ROTATION_MAX_MS) period = BLE_SPAM_ROTATION_MAX_MS;
    return period;
}

// --- Fast Pair model pool (ADR-0151) ------------------------------------------------------------
// A vetted pool of well-known Fast Pair model IDs, drawn from the community-documented GFPS
// advertisement lists. At session start, a random subset of BLE_FAST_PAIR_SUBSET_SIZE is picked;
// rotation within the subset stays sequential. This beats Android's cache-bloat limiting (one
// session advertises few distinct IDs) while varying which IDs appear across sessions (beats
// staleness). The pool is pure data; the device build and the native tests share it.
struct GfpsProfile { uint32_t id; const char* name; };

constexpr size_t BLE_FAST_PAIR_POOL_SIZE   = 7;
constexpr size_t BLE_FAST_PAIR_SUBSET_SIZE = 4;

// Pool content policy (field-informed 2026-11-09, fully re-validated across 5 clean-cache
// sessions): every entry below is CONFIRMED to pop on current Android GMS. There are no
// CANDIDATE slots anymore — a mixed pool is a liability (a dead ID consumes a subset slot
// silently and starves the session, which was the regression that forced the first-pass
// ADR-0151 pool to shrink). Names match what GMS *actually* displays on the half-sheet,
// which does not always match community spam-list labels (community lists carry a lot of
// mis-attributions — two of the labels below (0xCD8256, 0x821F66) are different products
// than community tooling claims; phone-visible name is the ground truth).
//
// Dropped during validation (silent dead on current GMS):
//   - 0x92BBBD Sony WF-1000XM4 (was in pre-ADR baseline; Google retired it server-side)
//   - any ID whose registered name couldn't be observed on the handset
// Google updates the DB server-side without notice, so this list decays. Periodic
// re-validation is in LEDGER; add new IDs through the same clean-cache evidence protocol.
//
// Pool-size / subset-size math: POOL=7 / SUBSET=4, every session draws 4 of 7 CONFIRMED IDs.
// Each ID appears in a given session with probability C(6,3)/C(7,4) = 20/35 = 57%. The
// expected number of fresh-cache popups per session is 4 (minus whatever Android's opaque
// per-ID cooldown suppresses from recent sessions; we cannot tune for that).
inline constexpr GfpsProfile kBleFastPairPool[BLE_FAST_PAIR_POOL_SIZE] = {
    // All CONFIRMED on 2026-11-09 clean-cache sessions (observed by the handset's popup name):
    {0x00B26E, "Pixel Buds"},               // pre-ADR baseline; re-confirmed
    {0x72EF8D, "Razer Hammerhead TWS X"},   // pre-ADR baseline; re-confirmed (popped 3/3 sessions)
    {0x9ADB11, "Pixel Buds Pro"},           // community-sourced; re-confirmed multiple sessions
    {0xF52494, "JBL Live Pro+ TWS"},        // pre-ADR baseline; re-confirmed
    {0x2D08D8, "Pixel Buds A-Series"},      // initially dropped as dead; re-confirmed under
                                            //   a non-overlapping subset (earlier misread was
                                            //   Android cooldown, not a dead ID)
    {0xCD8256, "Bose NC 700 Headphones"},   // community-labelled "Bose QC 35 II" was wrong;
                                            //   handset shows "Bose NC 700 Headphones"
    {0x821F66, "JBL Flip 6"},               // community-labelled "Samsung Galaxy Buds Live"
                                            //   was wrong; handset shows "JBL Flip 6"
};

// Draw K distinct indices in [0, poolSize) into out[0..K-1] by partial Fisher-Yates shuffle on an
// index array. Uniform and deterministic from the RNG seed. Pure; native-tested. The RNG is a
// callable returning uint32_t so the test can inject a seeded LCG and the device code can pass
// esp_random. poolSize must be <= 64 (bounded local array; the pool above is 12).
template<typename Rng>
inline void drawFastPairSubset(size_t poolSize, size_t k, uint8_t* out, Rng&& rng) {
    uint8_t idx[64];
    if (poolSize > sizeof(idx)) poolSize = sizeof(idx);
    if (k > poolSize) k = poolSize;
    for (size_t i = 0; i < poolSize; i++) idx[i] = (uint8_t)i;
    for (size_t i = 0; i < k; i++) {
        uint32_t span = (uint32_t)(poolSize - i);
        size_t j = i + (size_t)(rng() % span);
        uint8_t tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
        out[i] = idx[i];
    }
}

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

    // Active Fast Pair subset for the current session (ADR-0151). Drawn at start() from
    // kBleFastPairPool; updatePayload() rotates sequentially through these indices for the ANDROID
    // provider. Re-rolled on every start() so sessions vary across time.
    uint8_t m_fastPairSubset[BLE_FAST_PAIR_SUBSET_SIZE];

    // BadBLE reconnect-storm loop-breaker (ADR-0134). m_hidLinkEncrypted tracks whether the
    // current link reached an encrypted state; a drop while still unencrypted is a failed-auth
    // reconnect and feeds the guard. Once the guard trips, m_hidAdvertiseSuppressed stops the
    // auto-re-advertise so the device no longer spins against a host's stale bond.
    utils::ReconnectChurnGuard m_churnGuard;
    volatile bool m_hidLinkEncrypted;
    volatile bool m_hidAdvertiseSuppressed;
};

} // namespace adversary
