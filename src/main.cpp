/**
 * @file main.cpp
 * @brief The Adversary - Main entry point
 * 
 * Red-team wireless pentesting tool for M5Stack Cardputer/M5StickC Plus2
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include "config/config.h"
#include "config/pins.h"
#include "core/state_machine.h"
#include "core/event_bus.h"
#include "hal/storage/sd_manager.h"
#include "ui/theme.h"
#include "modules/wifi/wifi_scanner.h"
#include "ui/screens/scanner_screen.h"
#include "ui/screens/sniffer_screen.h"
#include "ui/screens/deauth_screen.h"
#include "ui/screens/handshake_screen.h"
#include "ui/screens/evil_twin_screen.h"
#include "ui/screens/karma_screen.h"
#include "ui/screens/beacon_spam_screen.h"
#include "ui/screens/probe_flood_screen.h"
#include "ui/screens/captures_screen.h"

#include "ui/screens/settings_screen.h"
#include "ui/screens/saved_networks_screen.h"
#include "ui/screens/about_screen.h"
#include "ui/screens/whitelist_screen.h"

#include "ui/screens/wardriving_screen.h"
#include "ui/screens/ble_scanner_screen.h"

#include "ui/screens/rfid_screen.h"
#include "ui/screens/ir_tvbgone_screen.h"
#include "ui/screens/ir_record_screen.h"
#include "ui/screens/ble_spam_screen.h"
#include "ui/screens/ble_apple_attack_screen.h"
#include "ui/screens/ble_bad_ble_screen.h"
#include "ui/screens/badusb_screen.h"
#include "ui/screens/mouse_jiggler_screen.h"
#include "ui/screens/ble_spoof_screen.h"
#include "ui/screens/server_menu_screen.h"
#include "ui/screens/server_status_screen.h"
#include "modules/server/server_manager.h"
#include "ui/screens/splash_screen.h"
#include "ui/screen_manager.h"
#include "ui/components/menu.h"
#include "modules/storage/capture_registry.h"
#include "modules/storage/settings_manager.h"
#include "modules/notification/notification_manager.h"
#include "modules/wifi/wifi_connection.h"
#include "ui/components/toast_manager.h"
#include "modules/gps/gps_manager.h"
#include "modules/gps/gps_probe.h"
#include "modules/system/system_manager.h"
#include "modules/system/time_manager.h"
#include "modules/ble/ble_scanner.h"
#include "ui/screens/ble_scanner_screen.h"

#include "hal/input/input_manager.h"
#include "hal/expansion/expansion_cap.h"
#include "ui/components/carousel_menu.h"
#include "ui/screens/radio_screen.h"
#include "assets/generated_icons.h"
#include "utils/bitmap_remapper.h"

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif

// Key definitions for Cardputer
#if defined(TARGET_CARDPUTER)
#define KEY_ESC_CHAR '`'
// KEY_ENTER from Keyboard_def.h is 0x28, but keysState().enter flag is more reliable
#endif

namespace adversary {
namespace ui {
bool g_gpsDetected = false;
bool g_rfidDetected = false;
// Resolved top-side expansion cap (slice-0002). Gates the Radio carousel entry
// and the cap-GPS probe; set once at boot after the SD mount.
hal::ExpansionCap g_expansionCap = hal::ExpansionCap::None;
}
}


// adversary::Menu action IDs
enum MenuActionId {
    // Wireless submenu
    ACTION_SCAN_NETWORKS = 100,
    ACTION_PACKET_SNIFFER = 101,
    ACTION_DEAUTH_ATTACK = 102,
    ACTION_HANDSHAKE_CAPTURE = 103,
    ACTION_EVIL_TWIN = 104,
    ACTION_KARMA_AP = 105,
    ACTION_BEACON_SPAM = 106,
    ACTION_PROBE_FLOOD = 107,
    ACTION_CAPTURES = 108,
    ACTION_WARDRIVING = 111,
    
    // BLE submenu
    ACTION_BLE_SCAN = 200,
    ACTION_BLE_SPAM = 202,
    ACTION_BLE_APPLE_ATTACK = 203,
    ACTION_BLE_BAD_BLE = 204,
    ACTION_BLE_SPOOF = 205,

    // HID submenu
    ACTION_USB_BADUSB = 210,
    ACTION_MOUSE_JIGGLER = 211,

    ACTION_IR_TVB_GONE = 302,
    ACTION_IR_RECORD = 303,

    // Settings
    ACTION_SETTINGS = 400,
    ACTION_ABOUT = 401,
    ACTION_WHITELIST = 402,
    
    // RFID submenu
    ACTION_RFID_DASHBOARD = 500,
    
    // Server Mode
    ACTION_SERVER = 600,

    // Multi-radio cap (slice-0002)
    ACTION_RADIO = 700
};

// Forward declarations
void initHardware();
void showMenu();
void handleInput();
void handleMenuAction(int actionId);
void initializeMenu();
void stopAllAttacks();  // Clean up all running attacks/modules
bool routeInputToActiveScreen(char key);  // Route input via adversary::ScreenManager
void navigateToScreen(adversary::ScreenId screen);  // Navigate and sync adversary::ScreenManager
void adversary_ui_purge_canvas();      // Force delete 64KB sprite to free contiguous RAM
void adversary_ui_restore_canvas();    // Recreate the sprite for UI rendering
void adversary_ui_render_forced();     // Force manual render during blocking tasks

// Global state
static adversary::StateMachine& stateMachine = adversary::StateMachine::getInstance();
static adversary::SDManager& sdManager = adversary::SDManager::getInstance();
static adversary::WiFiScanner& wifiScanner = adversary::WiFiScanner::getInstance();
// All 22 screens are now lazy-loaded via adversary::ScreenManager::registerFactory().
// adversary::ScannerScreen and adversary::SnifferScreen were the last holdouts (closure captures)
// and are now also factory-migrated by using getActiveScreen() inside lambdas.
// NOTE: Phase 3 screens now lazy-loaded: adversary::SettingsScreen, adversary::SavedNetworksScreen,
//       adversary::WardrivingScreen, adversary::BleSpamScreen, adversary::ServerMenuScreen, adversary::ServerStatusScreen
static adversary::SplashScreen splashScreen;
static adversary::Menu mainMenu;
static adversary::CarouselMenu carouselMenu;
static adversary::ScreenManager& screenMgr = adversary::ScreenManager::getInstance();
static adversary::ScreenId currentScreen = adversary::ScreenId::MENU;

// Global canvas for flicker-free rendering
static M5Canvas* globalCanvas = nullptr;

/**
 * @brief Stop all running attacks and modules to ensure clean WiFi state
 */
void stopAllAttacks() {
    Serial.println("[Main] Stopping all attacks/modules...");
    
    // Single active-screen reference for all lazy-loaded screen cleanup
    auto& sm = adversary::ScreenManager::getInstance();
    auto aid = sm.getActiveScreenId();
    adversary::IScreen* activeScr = sm.getActiveScreen();
    
    // Stop scanner / sniffer if active
    if (aid == adversary::ScreenId::SCANNER && activeScr) {
        static_cast<adversary::ScannerScreen*>(activeScr)->setActive(false);
    } else if (aid == adversary::ScreenId::SNIFFER && activeScr) {
        static_cast<adversary::SnifferScreen*>(activeScr)->setActive(false);
    }
    
    // Stop attack screens
    if (activeScr) {
        if (aid == adversary::ScreenId::DEAUTH || aid == adversary::ScreenId::HANDSHAKE ||
            aid == adversary::ScreenId::BEACON_SPAM || aid == adversary::ScreenId::PROBE_FLOOD) {
            activeScr->hide();
        } else if (aid == adversary::ScreenId::EVIL_TWIN) {
            auto* et = static_cast<adversary::EvilTwinScreen*>(activeScr);
            et->stop();
            et->forceStopPortal();
        } else if (aid == adversary::ScreenId::KARMA) {
            auto* ks = static_cast<adversary::KarmaScreen*>(activeScr);
            ks->stop();
            ks->forceStopPortal();
        }
    }


    // Stop Other Modules (IR, RFID, BLE peripherals) — lazy-loaded
    if (activeScr && (
        aid == adversary::ScreenId::INFRARED_TVB_GONE ||
        aid == adversary::ScreenId::RFID ||
        aid == adversary::ScreenId::BLE_BAD_BLE ||
        aid == adversary::ScreenId::USB_BADUSB ||
        aid == adversary::ScreenId::HID_MOUSE_JIGGLER ||
        aid == adversary::ScreenId::BLE_SPOOF ||
        aid == adversary::ScreenId::BLE_APPLE_ATTACK)) {
        activeScr->hide();
    }

    // AGGRESSIVE CLEANUP: Force full BLE radio release
    // Screens preserve BLE state in hide() for quick restart,
    // but when switching to a different module we must reclaim heap
    adversary::BLESpanner::getInstance().forceRelease();
    adversary::BLEScanner::getInstance().deinit();
    
    // Server & Wardriving — lazy-loaded; cleanup via active screen
    if (activeScr && (
        aid == adversary::ScreenId::SERVER_MENU || aid == adversary::ScreenId::SERVER_STATUS ||
        aid == adversary::ScreenId::WARDRIVING)) {
        activeScr->hide();
    }

    // GLOBAL WIFI RESET
    // Only manipulate WiFi if the driver was actually initialized.
    // Turn WiFi OFF (not STA) to free ~30-40KB heap for BLE-intensive screens.
    // Screens that need WiFi will re-enable it in their show() method.
#ifdef ESP32
    wifi_mode_t currentMode;
    if (esp_wifi_get_mode(&currentMode) == ESP_OK) {
        Serial.println("[Main] Resetting WiFi state...");
        if (currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA) {
            WiFi.softAPdisconnect(true);
            delay(50);
        }
        WiFi.disconnect(true);  // true = turn off WiFi radio
        WiFi.mode(WIFI_OFF);
    } else {
        Serial.println("[Main] WiFi not initialized, skipping reset");
    }
#endif

    // Give time for WiFi resources to be fully released
    delay(100);
    
    Serial.println("[Main] All attacks stopped");
}

/**
 * @brief Log reset reason to SD card if it was a crash
 * 
 * Checks esp_reset_reason() on boot and appends crash info to 
 * /adversary/logs/crash.log if it was an abnormal reset.
 * 
 */
// Arduino forward declarations (for setup/loop orchestration)
void initHardware();

/**
 * @brief Arduino setup function
 */
void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    Serial.println("\n\n");
    Serial.println("================================");
    Serial.println("  THE ADVERSARY");
    Serial.println("  Red Team Wireless Tool");
    Serial.printf("  Version: %s\n", adversary::config::VERSION);
    Serial.println("================================\n");

#if defined(TARGET_CARDPUTER)
    // De-select every SPI peripheral that can share the SD bus BEFORE
    // M5Cardputer.begin() mounts the SD card. A chip-select left floating drives
    // MISO during SD init and causes CRC errors that corrupt the SD state machine.
    //
    // Two different caps use this same top-side header; only one is attached at a
    // time, and we must be safe for whichever it is:
    //  - Cap LoRa 1262:    NSS on G5  -> driven HIGH (field-proven de-select).
    //  - CC1101/NRF24 cap: CC1101 CS on G15, NRF24 CS on G4.
    // The new cap's CS lines use INPUT_PULLUP rather than a driven HIGH: a weak
    // pull-up holds a floating radio CS de-asserted, but yields to the LoRa cap's
    // push-pull drivers on those same pins (G15 = GPS-UART TX, G4 = SX1262 IRQ),
    // so we don't fight them or break GPS when the LoRa cap is the one attached.
    pinMode(adversary::pins::LORA_NSS, OUTPUT);
    digitalWrite(adversary::pins::LORA_NSS, HIGH);
    pinMode(adversary::pins::CC1101_CS, INPUT_PULLUP);
    pinMode(adversary::pins::NRF24_CS, INPUT_PULLUP);
#endif

    // Initialize hardware
    initHardware();
    
    // Create global canvas ASAP to reserve memory before WiFi/BLE start
    globalCanvas = new M5Canvas(&M5.Display);
    // SPIKE (8-bit canvas): halve the canvas from 16bpp (64800 B) to 8bpp
    // RGB332 (32400 B) so it stops competing with the ~62KB TLS record buffers
    // in internal DRAM (no PSRAM on this board). setColorDepth persists across
    // deleteSprite/createSprite, so this one call covers every restore path too.
    // RGB332 auto-quantizes RGB565 draws; direct-to-display draws (splash) stay
    // true 16bpp. Trade-off: neutral grays gain a slight olive tint, dark
    // blues/purples darken — evaluate on-device; escalate to an 8-bit palette
    // only if the shift is objectionable.
    globalCanvas->setColorDepth(8);
#ifdef BOARD_HAS_PSRAM
    globalCanvas->setPsram(true); // Must be called before createSprite
#endif
    if (globalCanvas->createSprite(adversary::config::SCREEN_WIDTH, adversary::config::SCREEN_HEIGHT) == nullptr) {
        Serial.println("[Init] ERROR: Failed to create global canvas! Heap too low.");
    } else {
        Serial.printf("[Init] Created global canvas %dx%d (Internal: %u, PSRAM: %u)\n", 
                      (int)adversary::config::SCREEN_WIDTH, (int)adversary::config::SCREEN_HEIGHT,
                      (unsigned int)ESP.getFreeHeap(),
                      (unsigned int)ESP.getFreePsram());
    }
    
    // Transition to splash screen
    stateMachine.transitionTo(adversary::AppState::SPLASH);
    splashScreen.show();
    splashScreen.startTimer();

    // Small delay to let hardware settle after M5Launcher handoff
    splashScreen.updateProgress(0.1f, "Initializing...");
    delay(500);

    // Initialize SD Card (first priority)
    splashScreen.updateProgress(0.2f, "Mounting SD card...");
    delay(100);
    
    if (!sdManager.init()) {
        Serial.printf("SD Card Error: %s\n", adversary::sdStatusToString(sdManager.getStatus()));
        splashScreen.updateProgress(0.25f, "SD: NOT FOUND!");
        delay(1500);
        // Continue anyway - some features won't work
    } else {
        adversary::SDCardInfo info = sdManager.getCardInfo();
        Serial.printf("SD Card: %s, %.2f MB free\n", 
                      info.type, 
                      info.freeBytes / (1024.0 * 1024.0));
        splashScreen.updateProgress(0.3f, "SD card ready");
        
        // Check and log if previous boot was a crash
        adversary::SystemManager::getInstance().logResetReason();
        
        // Load settings from config file (second priority - for theme)
        splashScreen.updateProgress(0.35f, "Loading settings...");
        adversary::SettingsManager::getInstance().load();
        
        // Apply saved theme as early as possible so splash uses it
        const auto& settings = adversary::SettingsManager::getInstance().get();
        adversary::ThemeManager::getInstance().setTheme(static_cast<adversary::ThemePreset>(settings.display.themePreset));
        
        // Apply brightness
        adversary::SystemManager::getInstance().setDisplayBrightness(settings.display.brightness);
        
        // Redraw splash screen with new theme colors
        splashScreen.show();
        splashScreen.updateProgress(0.4f, "Settings loaded");

        // Scan capture directories for H/C indicators
        splashScreen.updateProgress(0.45f, "Loading captures...");
        
        // Use progress callback for capture registry to show counter
        adversary::CaptureRegistry::getInstance().setProgressCallback([](int count, const char* label) {
            char fullLabel[64];
            snprintf(fullLabel, sizeof(fullLabel), "Loading captures: %d", count);
            splashScreen.updateProgress(0.45f, fullLabel);
        });
        
        adversary::CaptureRegistry::getInstance().scanAll();
        // Clear callback after use to avoid lambda lifecycle issues
        adversary::CaptureRegistry::getInstance().setProgressCallback(nullptr);
        
        adversary::SettingsManager::getInstance().loadWhitelist();
        adversary::SettingsManager::getInstance().checkApiKeyFile();
        
        // Initialize notification system
        adversary::NotificationManager::getInstance().init();
        
        // Apply notification settings from config
        adversary::NotificationManager::getInstance().setAudioEnabled(settings.system.notifySounds);
        adversary::NotificationManager::getInstance().setLedEnabled(settings.system.notifyLeds);
        
        // Apply toast position setting
        adversary::ToastManager::getInstance().setPosition(static_cast<adversary::ToastPosition>(settings.display.toastPosition));

        // Auto-detect can only probe if we own the FSPI bus. A launcher-chained
        // boot leaves the card mounted by the launcher (SDManager Method 1), so
        // spiBus() is null and the probe would be skipped — Auto then silently
        // resolves None with a cap seated (the slice-0002 launcher-boot gap).
        // Force a re-mount onto our own SPIClass first (Method 1 torn down ->
        // init() re-runs via Method 2 and owns sdSPI), but only when it would
        // change the outcome: Auto and not already owning the bus. A forced
        // override needs no probe, so it never pays this cost.
        if (settings.wireless.capOverride == adversary::hal::CapOverride::Auto &&
            sdManager.spiBus() == nullptr) {
            Serial.printf("[Cap] Claiming SD bus for auto-detect: %s\n",
                sdManager.remount() ? "OK" : "FAILED");
        }

        // Detect the top-side expansion cap now the SD bus is mounted and the
        // override is loaded. The probes run on the SD-shared SPI, leave every
        // chip-select SD-safe, and drive the multi-radio cap's radios to idle
        // so nothing emits at boot (slice-0002 / ADR-0001).
        splashScreen.updateProgress(0.48f, "Detecting cap...");
        adversary::ui::g_expansionCap =
            adversary::hal::detectExpansionCap(settings.wireless.capOverride);
        Serial.printf("[Cap] Resolved: %s\n",
            adversary::ui::g_expansionCap == adversary::hal::ExpansionCap::MultiRadio
                ? "Multi-Radio" : "None");
        // Sharing the SD bus with the cap probe desyncs the card's SPI state
        // machine (CRC errors on the next access), so re-mount to re-sync it
        // whenever the probe could have run — any non-forced-None override,
        // i.e. every Auto boot regardless of result (slice-0002).
        if (settings.wireless.capOverride != adversary::hal::CapOverride::ForceNone) {
            Serial.printf("[Cap] SD re-mount after probe: %s\n",
                sdManager.remount() ? "OK" : "FAILED");
        }

        // Initialize time management (load rough estimate from last sync)
        adversary::TimeManager::getInstance().loadFromSD();
    }
    delay(200);

    // Leave WiFi OFF at boot. The ~34KB STA driver is unused by the idle menu and
    // was previously reclaimed only on the first navigation ("Resetting WiFi
    // state"), so boot sat ~34KB lower than necessary (measured: H79 -> H113 on
    // first nav). Screens that need WiFi enable it on demand (Scanner, WPA-SEC, AP
    // attacks); no boot-window code reads STA mode. The first-nav reset now no-ops.
    splashScreen.updateProgress(0.5f, "WiFi on-demand mode");
    WiFi.mode(WIFI_OFF);
    delay(100);
    splashScreen.updateProgress(0.7f, "WiFi ready (off until needed)");

    // Prepare BLE (Init deferred to usage to save early heap)
    splashScreen.updateProgress(0.72f, "Preparing BLE...");

    // Initialize GPS module (if present)
    splashScreen.updateProgress(0.75f, "Detecting GPS...");
    Serial.println("[GPS] Detecting AT6668 GPS module...");
    // Skip the cap-GPS pin set when the multi-radio cap owns G13/G15 (slice-0002).
    const bool probeCapGps =
        adversary::ui::g_expansionCap != adversary::hal::ExpansionCap::MultiRadio;
    if (adversary::GPSManager::getInstance().init(probeCapGps)) {
        Serial.println("[GPS] AT6668 GPS module detected");
        adversary::ui::g_gpsDetected = true;
    } else {
        Serial.println("[GPS] No GPS module found (will retry on GPS-dependent screens)");
        adversary::ui::g_gpsDetected = false;
    }
    delay(100);

    // Initialize RFID module (if present).
    // RFID (MFRC522) speaks I2C on the Grove pins (G1/G2). Only a *Grove-port*
    // GPS contends for those pins; a cap GPS on G13/G15 shares nothing, so RFID
    // and a cap GPS coexist. Skip RFID only on the real Grove-port conflict.
    const char* gpsSource = adversary::GPSManager::getInstance().getDetectedPinSet();
    if (adversary::gps::gpsBlocksRfid(adversary::ui::g_gpsDetected, gpsSource)) {
        splashScreen.updateProgress(0.78f, "RFID skipped (Grove GPS)");
        Serial.println("[RFID] Skipped - Grove-port GPS owns GPIO 1/2 (pin conflict)");
        adversary::ui::g_rfidDetected = false;
    } else {
        splashScreen.updateProgress(0.78f, "Detecting RFID...");
        Serial.println("[RFID] Detecting RFID module...");
        if (adversary::RFIDManager::getInstance().init()) {
            Serial.println("[RFID] RFID module detected");
            adversary::ui::g_rfidDetected = true;
        } else {
            Serial.println("[RFID] No RFID module found");
            adversary::ui::g_rfidDetected = false;
        }
    }
    delay(100);

    // Settings loaded above
    splashScreen.updateProgress(0.8f, "Ready...");
    delay(200);

    // Finalize initialization
    splashScreen.updateProgress(1.0f, "Ready!");
    
    // Ensure minimum splash duration
    splashScreen.ensureMinDuration();

    // Transition to idle (menu) state
    stateMachine.transitionTo(adversary::AppState::IDLE);
    
    // Initialize the hierarchical menu
    initializeMenu();
    
    // All screens are now lazy-loaded via registerFactory()
    // Lazy-loaded screens — Phase 5 (Scanner/Sniffer — closures now use getActiveScreen())
    screenMgr.registerFactory(adversary::ScreenId::SCANNER,
        []() -> adversary::IScreen* { return new adversary::ScannerScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::SNIFFER,
        []() -> adversary::IScreen* { return new adversary::SnifferScreen(); });

    // Lazy-loaded screens — Phase 5 (BLE scanner)
    screenMgr.registerFactory(adversary::ScreenId::BLE_SCANNER,
        []() -> adversary::IScreen* { return new adversary::BleScannerScreen(); });

    // Lazy-loaded screens — Phase 4 (attack screens with setParams override)
    screenMgr.registerFactory(adversary::ScreenId::DEAUTH,
        []() -> adversary::IScreen* { return new adversary::DeauthScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::HANDSHAKE,
        []() -> adversary::IScreen* { return new adversary::HandshakeScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::EVIL_TWIN,
        []() -> adversary::IScreen* { return new adversary::EvilTwinScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::PROBE_FLOOD,
        []() -> adversary::IScreen* { return new adversary::ProbeFloodScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::KARMA,
        []() -> adversary::IScreen* { return new adversary::KarmaScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::BEACON_SPAM,
        []() -> adversary::IScreen* { return new adversary::BeaconSpamScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::ABOUT,
        []() -> adversary::IScreen* { return new adversary::AboutScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::RADIO,
        []() -> adversary::IScreen* { return new adversary::RadioScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::WHITELIST,
        []() -> adversary::IScreen* { return new adversary::WhitelistScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::CAPTURES,
        []() -> adversary::IScreen* { return new adversary::CapturesScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::RFID,
        []() -> adversary::IScreen* { return new adversary::RfidScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::INFRARED_TVB_GONE,
        []() -> adversary::IScreen* { return new adversary::IrTvBGoneScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::INFRARED_RECORD,
        []() -> adversary::IScreen* { return new adversary::IrRecordScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::USB_BADUSB,
        []() -> adversary::IScreen* { return new adversary::BadUsbScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::HID_MOUSE_JIGGLER,
        []() -> adversary::IScreen* { return new adversary::MouseJigglerScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::BLE_BAD_BLE,
        []() -> adversary::IScreen* { return new adversary::BleBadBleScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::BLE_SPOOF,
        []() -> adversary::IScreen* { return new adversary::BleSpoofScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::BLE_APPLE_ATTACK,
        []() -> adversary::IScreen* { return new adversary::BleAppleAttackScreen(); });

    // Lazy-loaded screens — Phase 3 (simple callbacks or standalone with init)
    screenMgr.registerFactory(adversary::ScreenId::SETTINGS,
        []() -> adversary::IScreen* {
            auto* s = new adversary::SettingsScreen();
            s->setOnSavedNetworksRequested([]() {
                Serial.println("[Settings] Navigating to Saved Networks");
                navigateToScreen(adversary::ScreenId::SAVED_NETWORKS);
            });
            return s;
        });
    screenMgr.registerFactory(adversary::ScreenId::SAVED_NETWORKS,
        []() -> adversary::IScreen* { return new adversary::SavedNetworksScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::WARDRIVING,
        []() -> adversary::IScreen* { return new adversary::WardrivingScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::BLE_SPAM,
        []() -> adversary::IScreen* { return new adversary::BleSpamScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::SERVER_MENU,
        []() -> adversary::IScreen* { return new adversary::ServerMenuScreen(); });
    screenMgr.registerFactory(adversary::ScreenId::SERVER_STATUS,
        []() -> adversary::IScreen* { return new adversary::ServerStatusScreen(); });
    Serial.println("[Init] All screens registered with adversary::ScreenManager");

    showMenu();

    Serial.println("Initialization complete. Entering main loop.");
}

/**
 * @brief Navigate to a screen and sync adversary::ScreenManager
 * 
 * Updates both the local currentScreen and adversary::ScreenManager's active screen.
 * This ensures the loop() update/render uses the correct screen.
 */
void navigateToScreen(adversary::ScreenId screen) {
    adversary::ScreenManager::getInstance().setActiveScreen(screen);
    currentScreen = screen;
}

/**
 * @brief Arduino main loop
 */
void loop() {
    // Handle input based on current screen
    // Note: M5Cardputer.update() is called inside handleInput for Cardputer
    // M5.update() is only needed for M5StickC
#if !defined(TARGET_CARDPUTER)
    M5.update();
#endif

    handleInput();
    
    // Update system components
    adversary::NotificationManager::getInstance().update();
    adversary::WiFiConnection::getInstance().update();
    adversary::ToastManager::getInstance().update();
    adversary::ServerManager::getInstance().update();
    adversary::EventBus::getInstance().processQueue();  // Process deferred events
    
    // Update GPS data (sync detection flag for background task detection)
    auto& gps = adversary::GPSManager::getInstance();
    if (gps.isDetected()) {
        // Update global flag (may have been detected by background task)
        if (!adversary::ui::g_gpsDetected) {
            adversary::ui::g_gpsDetected = true;
            Serial.println("[Main] GPS detected by background task - enabling updates");
        }
        gps.update();
    }
    
    // Battery sense not supported on Cardputer
    
    // Heartbeat for diagnostic
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        Serial.printf("[Main] Loop heartbeat. Heap: %u\n", (unsigned int)ESP.getFreeHeap());
    }

    // Screen-specific updates via adversary::ScreenManager
    auto& sm = adversary::ScreenManager::getInstance();
    currentScreen = sm.getActiveScreenId();
    
    // Periodic status bar refresh (GPS, battery, heap badges)
    static uint32_t lastStatusRefresh = 0;
    if (currentScreen != adversary::ScreenId::MENU && millis() - lastStatusRefresh > 2000) {
        lastStatusRefresh = millis();
        sm.requestRedraw();
    }
    
    if (currentScreen != adversary::ScreenId::MENU) {
        // Use adversary::ScreenManager for all non-menu screens
        sm.update();
        if (globalCanvas && globalCanvas->getBuffer()) {
            sm.render(*globalCanvas);
            globalCanvas->pushSprite(0, 0);
        }
        
        // Check if screen wants to return to menu
        if (sm.shouldReturnToMenu()) {
            sm.returnToMenu();
            navigateToScreen(adversary::ScreenId::MENU);
            stateMachine.transitionTo(adversary::AppState::IDLE);
            showMenu();
        }
    } else {
        // adversary::Menu screen - check for animation or active toasts
        if ((mainMenu.isAtRoot() && carouselMenu.isAnimating()) || adversary::ToastManager::getInstance().isVisible()) {
            showMenu();
        }
    }

    // Small delay to prevent watchdog issues (reduced to 1ms for GPS UART compatibility)
    delay(1);
}

/**
 * @brief Initialize hardware (display, input, etc.)
 */
void initHardware() {
#if defined(TARGET_CARDPUTER)
    // Initialize M5Cardputer with keyboard enabled
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);  // true = enable keyboard
    Serial.println("[Init] M5Cardputer initialized with keyboard");
#elif defined(TARGET_M5STICK)
    // Initialize M5Unified for M5StickCPlus2
    auto cfg = M5.config();
    cfg.internal_imu = true;
    M5.begin(cfg);
    Serial.println("[Init] M5StickCPlus2 initialized via M5Unified");
#else
    // Initialize M5 unified library for other targets
    auto cfg = M5.config();
    M5.begin(cfg);
#endif
    
    // Configure display
    M5.Display.setRotation(1);  // Landscape
    M5.Display.fillScreen(adversary::theme::BG_PRIMARY());
    
    // Seed random generator with hardware entropy
    randomSeed(esp_random());
}

/**
 * @brief Initialize the hierarchical menu structure
 */
void initializeMenu() {
    using adversary::MenuItem;
    using adversary::MenuItemType;
    
    // Wireless submenu (Back last: entry lands the cursor on a real action, and
    // with wrap-around nav Back is one Up-press away from the top)
    std::vector<MenuItem> wirelessMenu = {
        MenuItem::action("Scan Networks", ACTION_SCAN_NETWORKS, 's'),
        MenuItem::action("Packet Sniffer", ACTION_PACKET_SNIFFER, 'p'),
        MenuItem::action("Deauth Attack", ACTION_DEAUTH_ATTACK, 'd'),
        MenuItem::action("Beacon Spam", ACTION_BEACON_SPAM, 'b'),
        MenuItem::action("Probe Flood", ACTION_PROBE_FLOOD, 'f'),
        MenuItem::action("Handshake Capture", ACTION_HANDSHAKE_CAPTURE, 'h'),

        MenuItem::separator(),
        MenuItem::action("Evil Twin AP", ACTION_EVIL_TWIN, 'e'),
        MenuItem::action("Karma AP", ACTION_KARMA_AP, 'k'),

        adversary::ui::g_gpsDetected ?
            MenuItem::action("Wardriving", ACTION_WARDRIVING, 'd') :
            MenuItem::disabled("Wardriving (No GPS)"),
        MenuItem::separator(),
        MenuItem::action("Captures", ACTION_CAPTURES, 'c'),
        MenuItem::action("Whitelist", ACTION_WHITELIST, 'l'),
        MenuItem::separator(),
        MenuItem::back()
    };

    // BLE submenu
    std::vector<MenuItem> bleMenu = {
        MenuItem::action("BLE Scanner", ACTION_BLE_SCAN, 's'),
        MenuItem::action("Apple Attack", ACTION_BLE_APPLE_ATTACK, 'a'),
        MenuItem::action("BadBLE (HID)", ACTION_BLE_BAD_BLE, 'h'),
        MenuItem::action("Identity Spoof", ACTION_BLE_SPOOF, 'i'),
        MenuItem::action("BLE Spam", ACTION_BLE_SPAM, 'p'),
        MenuItem::separator(),
        MenuItem::back()
    };

    std::vector<MenuItem> irMenu = {
        MenuItem::action("TV-B-Gone", ACTION_IR_TVB_GONE, 't'),
        MenuItem::action("Record / Replay", ACTION_IR_RECORD, 'r'),
        MenuItem::separator(),
        MenuItem::back()
    };

    // RFID submenu
    std::vector<MenuItem> rfidMenu = {
        adversary::ui::g_rfidDetected ?
            MenuItem::action("RFID Dashboard", ACTION_RFID_DASHBOARD, 'd') :
            MenuItem::disabled("RFID (Module missing)"),
        MenuItem::disabled("RFID Write (Coming Soon)"),
        MenuItem::separator(),
        MenuItem::back()
    };

    // HID submenu (USB HID keystroke/mouse injection)
    std::vector<MenuItem> hidMenu = {
#if defined(TARGET_CARDPUTER)
        MenuItem::action("BadUSB (HID)", ACTION_USB_BADUSB, 'u'),
        MenuItem::action("Mouse Jiggler", ACTION_MOUSE_JIGGLER, 'm'),
#else
        MenuItem::disabled("BadUSB (No USB-OTG)"),
        MenuItem::disabled("Mouse Jiggler (No USB-OTG)"),
#endif
        MenuItem::separator(),
        MenuItem::back()
    };

    // Main menu
    std::vector<MenuItem> rootMenu;
    rootMenu.push_back(MenuItem::submenu("Wireless", wirelessMenu, 'w'));
    rootMenu.push_back(MenuItem::submenu("BLE", bleMenu, 'b'));
    rootMenu.push_back(MenuItem::submenu("Infrared", irMenu, 'i'));
    rootMenu.push_back(MenuItem::submenu("RFID", rfidMenu, 'r'));
    rootMenu.push_back(MenuItem::submenu("HID", hidMenu, 'h'));
    rootMenu.push_back(MenuItem::separator());
    rootMenu.push_back(MenuItem::action("Settings", ACTION_SETTINGS));
    rootMenu.push_back(MenuItem::action("About", ACTION_ABOUT));

    mainMenu.setTitle("THE ADVERSARY");
    mainMenu.setItems(rootMenu);
    mainMenu.setOnAction([](int actionId) {
        handleMenuAction(actionId);
    });

    // Initialize Carousel adversary::Menu for Root icons
    std::vector<adversary::CarouselItem> carouselItems = {
        {"WIRELESS", adversary::assets::ICON_WIRELESS, -1, true, ""}, // -1 = navigate to submenu in mainMenu
        {"BLE", adversary::assets::ICON_BLE, -2, true, ""},
        {"INFRARED", adversary::assets::ICON_INFRARED, -3, true, ""},
        {"RFID", adversary::assets::ICON_RFID, -4, adversary::ui::g_rfidDetected, "RFID module not found"},
        {"HID", adversary::assets::ICON_HID, -5, true, ""}
    };
    
    // Radio entry — always present, greyed out when the multi-radio cap is not
    // detected, matching the RFID entry's optional-hardware convention (slice-0002).
    const bool capPresent =
        adversary::ui::g_expansionCap == adversary::hal::ExpansionCap::MultiRadio;
    carouselItems.push_back({"RADIO", adversary::assets::ICON_RADIO, ACTION_RADIO,
                             capPresent, "Multi-radio cap not found"});

    // Server entry - enabled if dashboard exists
    bool serverEnabled = sdManager.fileExists("/adversary/dashboard/index.html");
    carouselItems.push_back({"SERVER", adversary::assets::ICON_SERVER, ACTION_SERVER, serverEnabled, "Dashboard index missing"});

    carouselItems.push_back({"SETTINGS", adversary::assets::ICON_SETTINGS, ACTION_SETTINGS, true, ""});
    carouselItems.push_back({"ABOUT", adversary::assets::ICON_ABOUT, ACTION_ABOUT, true, ""});
    carouselMenu.setItems(carouselItems);
    carouselMenu.setOnAction([](int actionId) {
        if (actionId < 0) {
            // Negative actionId mapping (direct menu selection)
            mainMenu.reset();
            int moveCount = abs(actionId) - 1;
            for (int i = 0; i < moveCount; i++) mainMenu.navigateDown();
            mainMenu.selectCurrent();
            showMenu();
        } else {
            handleMenuAction(actionId);
        }
    });

    // ... rest of init code ...
}

/**
 * @brief Show main menu
 */
void showMenu() {
    Serial.println("[Main] showMenu() called");
    if (globalCanvas) {
        if (mainMenu.isAtRoot()) {
            carouselMenu.render(*globalCanvas);
        } else {
            mainMenu.render(*globalCanvas);
        }
        
        // Render toast overlay on top
        adversary::ToastManager::getInstance().render(*globalCanvas);
        
        if (globalCanvas->getBuffer()) {
            Serial.println("[Main] Pushing menu to display...");
            globalCanvas->pushSprite(0, 0);
        } else {
            Serial.println("[Main] ERROR: Canvas buffer is NULL (restoration failed)");
        }
    } else {
        Serial.println("[Main] ERROR: globalCanvas is NULL!");
    }
}

/**
 * @brief Route input to active screen via adversary::ScreenManager
 * @param key The key pressed
 * @return true if input was handled by a screen (not menu)
 * 
 * Uses adversary::ScreenManager to route input and handles exit-to-menu transitions.
 * Returns false if on menu screen (caller should handle menu input).
 */
bool routeInputToActiveScreen(char key) {
    auto& sm = adversary::ScreenManager::getInstance();
    
    // If on menu, let caller handle it
    if (sm.isOnMenu()) {
        return false;
    }
    
    // Route input to active screen
    sm.handleInput(key);
    
    // Check if screen wants to return to menu (via exitToMenu_ flag)
    // Screens must explicitly set exitToMenu_ = true when they want to exit
    if (sm.shouldReturnToMenu()) {
        sm.returnToMenu();
        navigateToScreen(adversary::ScreenId::MENU);
        stateMachine.transitionTo(adversary::AppState::IDLE);
        showMenu();
    }
    
    return true;
}

/**
 * @brief Handle input events
 */
void handleInput() {
#if defined(TARGET_CARDPUTER)
    // Cardputer has a full keyboard
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();
        
        // Get the pressed key character (if any printable key)
        char pressedKey = 0;
        if (!state.word.empty()) {
            pressedKey = state.word[0];
        }
        
        // Debug output
        Serial.printf("[Input] Key pressed: '%c' (0x%02X), enter=%d, del=%d, tab=%d\n",
                      (pressedKey >= 32 && pressedKey < 127) ? pressedKey : '?',
                      (uint8_t)pressedKey, state.enter, state.del, state.tab);
        
        // Convert special keys to standard characters
        char key = 0;
        if (state.enter) {
            key = '\n';
        } else if (state.del) {
            key = 0x08;  // Backspace
        } else {
            key = pressedKey;
        }
        if (key == 0) return;  // No valid key
        
        // Route to active screen via adversary::ScreenManager (handles all non-menu screens)
        if (!routeInputToActiveScreen(key)) {
            // On menu screen - handle menu input directly
            if (mainMenu.isAtRoot()) {
                carouselMenu.handleInput(key);
            } else {
                mainMenu.handleInput(key);
            }
            // Only re-render menu if we didn't just transition to a screen
            if (adversary::ScreenManager::getInstance().isOnMenu()) {
                showMenu();
            }
        }
    }
#elif defined(TARGET_M5STICK)
    // M5StickC uses InputManager for gesture-based input
    using adversary::Input;
    using adversary::InputAction;
    Input().update();
    InputAction action = Input().getAction();
    
    if (action != InputAction::NONE) {
        Serial.printf("[M5Stick] InputAction: %d\n", static_cast<int>(action));
        
        // Convert InputAction to char for backward compatibility with handleInput
        char pressedKey = '\0';
        switch (action) {
            case InputAction::UP:     pressedKey = ';';  break;
            case InputAction::DOWN:   pressedKey = '.';  break;
            case InputAction::LEFT:   pressedKey = ';';  break;  // Also decrease
            case InputAction::RIGHT:  pressedKey = '.';  break;  // Also increase
            case InputAction::SELECT: pressedKey = '\n'; break;
            case InputAction::BACK:   pressedKey = '`';  break;
            default: break;
        }
        
        if (pressedKey == '\0') return;
        
        // Route to active screen via adversary::ScreenManager (handles all non-menu screens)
        if (!routeInputToActiveScreen(pressedKey)) {
            // On menu screen - handle menu input with InputAction support
            if (mainMenu.isAtRoot()) {
                if (action == InputAction::BACK) mainMenu.goBack();
                else carouselMenu.handleAction(action);
            } else {
                if (action == InputAction::BACK) {
                    mainMenu.goBack();
                } else {
                    mainMenu.handleAction(action);
                }
            }
            // Only re-render menu if we didn't just transition to a screen
            if (adversary::ScreenManager::getInstance().isOnMenu()) {
                showMenu();
            }
        }
    }
#endif
}

/**
 * @brief Handle menu action selection
 */
void handleMenuAction(int actionId) {
    // CRITICAL: Clear carousel icons to free ~30KB RAM for the target screen
    if (carouselMenu.isAnimating()) {
        // Just in case, but usually animation stops before action
    }
    // carouselMenu.clearCache(); // Removed: Direct rendering no longer uses cache
    
    Serial.printf("[Main] handleMenuAction: %d\n", actionId);
    // Stop any running attacks before starting new ones
    // This also clears the Carousel cache globally
    stopAllAttacks();
    
    switch (actionId) {
        case ACTION_SCAN_NETWORKS:
            Serial.println("Starting WiFi Scanner...");
            navigateToScreen(adversary::ScreenId::SCANNER);  // factory creates + init() + show()
            if (auto* sc = screenMgr.getActiveScreen()) {
                auto* scanner = static_cast<adversary::ScannerScreen*>(sc);
                // Set callback for network action selection
                scanner->setOnNetworkAction([](const adversary::NetworkInfo& network, adversary::NetworkAction action) {
                    Serial.printf("[Scanner] Action on %s: ", network.ssid.c_str());
                    stopAllAttacks();
                    switch (action) {
                        case adversary::NetworkAction::DEAUTH: {
                            Serial.println("DEAUTH");
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, network.bssid, 6);
                            strncpy(p.ssid, network.ssid.c_str(), 32);
                            p.channel = network.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::DEAUTH, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::NetworkAction::HANDSHAKE: {
                            Serial.println("HANDSHAKE");
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, network.bssid, 6);
                            strncpy(p.ssid, network.ssid.c_str(), 32);
                            p.channel = network.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::HANDSHAKE, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::NetworkAction::EVIL_TWIN: {
                            Serial.println("EVIL_TWIN");
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, network.bssid, 6);
                            strncpy(p.ssid, network.ssid.c_str(), 32);
                            p.channel = network.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::EVIL_TWIN, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::NetworkAction::PROBE_FLOOD: {
                            Serial.println("PROBE_FLOOD");
                            adversary::IScreen::ScreenParams p;
                            strncpy(p.ssid, network.ssid.c_str(), 32);
                            p.channel = network.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::PROBE_FLOOD, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::NetworkAction::INFO:
                            Serial.println("INFO");
                            break;
                    }
                });
                scanner->setActive(true);
            }
            stateMachine.transitionTo(adversary::AppState::SCANNING);
            break;
            
        case ACTION_PACKET_SNIFFER:
            Serial.println("Starting Packet Sniffer...");
            navigateToScreen(adversary::ScreenId::SNIFFER);  // factory creates + init() + show()
            if (auto* sf = screenMgr.getActiveScreen()) {
                auto* sniffer = static_cast<adversary::SnifferScreen*>(sf);
                sniffer->show();  // extra show() to reset screen state to STATS view
                sniffer->setOnPacketAction([](const adversary::PacketSummary& packet, adversary::PacketAction action) {
                    Serial.printf("[Sniffer] Action callback: %d\n", static_cast<int>(action));
                    // Stop sniffer before transitioning
                    if (auto* cur = screenMgr.getActiveScreen())
                        static_cast<adversary::SnifferScreen*>(cur)->setActive(false);
                    switch (action) {
                        case adversary::PacketAction::HANDSHAKE_CAPTURE: {
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, packet.srcMac, 6);
                            strncpy(p.ssid, packet.ssid, 32);
                            p.channel = packet.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::HANDSHAKE, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::PacketAction::DEAUTH_ATTACK: {
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, packet.srcMac, 6);
                            strncpy(p.ssid, packet.ssid, 32);
                            p.channel = packet.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::DEAUTH, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::PacketAction::EVIL_TWIN: {
                            adversary::IScreen::ScreenParams p;
                            memcpy(p.bssid, packet.srcMac, 6);
                            strncpy(p.ssid, packet.ssid, 32);
                            p.channel = packet.channel;
                            screenMgr.navigateWithParams(adversary::ScreenId::EVIL_TWIN, p);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        }
                        case adversary::PacketAction::KARMA_ATTACK:
                            navigateToScreen(adversary::ScreenId::KARMA);
                            stateMachine.transitionTo(adversary::AppState::ATTACKING);
                            break;
                        case adversary::PacketAction::COPY_BSSID:
                            Serial.printf("[Sniffer] BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                         packet.srcMac[0], packet.srcMac[1], packet.srcMac[2],
                                         packet.srcMac[3], packet.srcMac[4], packet.srcMac[5]);
                            break;
                        default:
                            break;
                    }
                });
            }
            stateMachine.transitionTo(adversary::AppState::SCANNING);
            break;
            
        case ACTION_DEAUTH_ATTACK:
            Serial.println("Starting Deauth Attack...");
            navigateToScreen(adversary::ScreenId::DEAUTH);  // factory: init() + show()
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_BEACON_SPAM:
            Serial.println("Starting Beacon Spam...");
            navigateToScreen(adversary::ScreenId::BEACON_SPAM);  // factory: init() + show()
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_PROBE_FLOOD:
            Serial.println("Starting Probe Flood...");
            navigateToScreen(adversary::ScreenId::PROBE_FLOOD);  // factory: init() + show()
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_HANDSHAKE_CAPTURE:
            Serial.println("Starting Handshake Capture...");
            navigateToScreen(adversary::ScreenId::HANDSHAKE);  // factory: init() + show()
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_EVIL_TWIN:
            Serial.println("Starting Evil Twin AP...");
            navigateToScreen(adversary::ScreenId::EVIL_TWIN);
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_KARMA_AP:
            Serial.println("Starting Karma AP...");
            navigateToScreen(adversary::ScreenId::KARMA);
            stateMachine.transitionTo(adversary::AppState::ATTACKING);
            break;
            
        case ACTION_CAPTURES:
            Serial.println("Opening Captures Browser...");
            navigateToScreen(adversary::ScreenId::CAPTURES);  // factory: init() + show()
            break;
            

        
        case ACTION_WARDRIVING:
            Serial.println("Starting Wardriving Mode...");
            navigateToScreen(adversary::ScreenId::WARDRIVING);  // factory: init() + show()
            stateMachine.transitionTo(adversary::AppState::SCANNING);
            break;

        case ACTION_BLE_SCAN:
            Serial.println("Starting BLE Scanner...");
            navigateToScreen(adversary::ScreenId::BLE_SCANNER);
            stateMachine.transitionTo(adversary::AppState::SCANNING);
            break;
            

            
        case ACTION_BLE_APPLE_ATTACK:
            Serial.println("Starting Apple Attack...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::BLE_APPLE_ATTACK);
            break;
            
        case ACTION_BLE_BAD_BLE:
            Serial.println("Starting BadBLE...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::BLE_BAD_BLE);
            break;

        case ACTION_USB_BADUSB:
            Serial.println("Starting BadUSB...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::USB_BADUSB);
            break;

        case ACTION_MOUSE_JIGGLER:
            Serial.println("Starting Mouse Jiggler...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::HID_MOUSE_JIGGLER);
            break;

        case ACTION_BLE_SPOOF:
            Serial.println("Starting BLE Spoof...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::BLE_SPOOF);
            break;
            
        case ACTION_BLE_SPAM:
            Serial.println("Starting BLE Spam...");
            navigateToScreen(adversary::ScreenId::BLE_SPAM);  // factory creates + shows
            break;
            
        case ACTION_IR_TVB_GONE:
            Serial.println("Starting IR TV-B-Gone...");
            navigateToScreen(adversary::ScreenId::INFRARED_TVB_GONE);  // factory creates + shows
            break;

        case ACTION_IR_RECORD:
            Serial.println("Opening IR Record/Replay...");
            navigateToScreen(adversary::ScreenId::INFRARED_RECORD);  // factory creates + shows
            break;
            

            
        case ACTION_SETTINGS:
            Serial.println("Opening Settings...");
            navigateToScreen(adversary::ScreenId::SETTINGS);  // factory: init() + callback + show()
            break;
            
        case ACTION_ABOUT:
            Serial.println("Opening About...");
            navigateToScreen(adversary::ScreenId::ABOUT);  // factory creates + shows
            break;

        case ACTION_RADIO:
            Serial.println("Opening Radio...");
            navigateToScreen(adversary::ScreenId::RADIO);  // factory creates + shows
            break;
            
        case ACTION_WHITELIST:
            Serial.println("Opening Whitelist...");
            navigateToScreen(adversary::ScreenId::WHITELIST);  // factory: init() + show()
            break;
            
        case ACTION_RFID_DASHBOARD:
            Serial.println("Opening RFID Dashboard...");
            navigateToScreen(adversary::ScreenId::RFID);  // factory creates + shows
            break;
            
        case ACTION_SERVER:
            Serial.println("Opening Server adversary::Menu...");
            stopAllAttacks();
            navigateToScreen(adversary::ScreenId::SERVER_MENU);  // factory: init() + show()
            break;
            
        default:
            Serial.printf("Unknown action: %d\n", actionId);
            break;
    }
}

void adversary_ui_purge_canvas() {
    if (globalCanvas) {
        Serial.println("[UI] Purging global canvas to free contiguous RAM...");
        globalCanvas->deleteSprite();
    }
}

void adversary_ui_restore_canvas() {
    if (globalCanvas && !globalCanvas->getBuffer()) {
        Serial.println("[UI] Restoring global canvas...");
        
        // Release adversary::EventBus queue memory — the ~2.8KB vector can fragment heap
        adversary::EventBus::getInstance().releaseMemory();
        
        // Stage 1: Basic Restore
        if (globalCanvas->createSprite(adversary::config::SCREEN_WIDTH, adversary::config::SCREEN_HEIGHT) != nullptr) {
            return;
        }

        // Stage 2: WiFi.mode(WIFI_OFF)
        Serial.println("[UI] Stage 2: Releasing WiFi radio...");
        WiFi.mode(WIFI_OFF);
        delay(200);
        if (globalCanvas->createSprite(adversary::config::SCREEN_WIDTH, adversary::config::SCREEN_HEIGHT) != nullptr) {
            Serial.println("[UI] Canvas restored (Stage 2)");
            return;
        }

        // Stage 3: Full WiFi stack deinit
        Serial.println("[UI] Stage 3: FULL WiFi stack deinit...");
        esp_wifi_stop();
        delay(100);
        esp_wifi_deinit();
        delay(300);
        
        if (globalCanvas->createSprite(adversary::config::SCREEN_WIDTH, adversary::config::SCREEN_HEIGHT) != nullptr) {
            Serial.println("[UI] Canvas restored (Stage 3)");
            return;
        }

        // Stage 4: Retry with yields — background tasks may free temporary buffers
        Serial.println("[UI] Stage 4: Retry with yields...");
        for (int i = 0; i < 5; i++) {
            delay(200);
            yield();
            if (globalCanvas->createSprite(adversary::config::SCREEN_WIDTH, adversary::config::SCREEN_HEIGHT) != nullptr) {
                Serial.printf("[UI] Canvas restored (Stage 4, retry %d)\n", i + 1);
                return;
            }
        }

        size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        Serial.println("[UI] ERROR: Failed to restore global canvas after all attempts!");
        Serial.printf("[UI] Fragmentation critical: Free heap: %u, Largest block: %u, Need: %u\n", 
                        (unsigned int)ESP.getFreeHeap(), (unsigned int)largestBlock,
                        (unsigned int)(adversary::config::SCREEN_WIDTH * adversary::config::SCREEN_HEIGHT * 2));
    }
}

void adversary_ui_render_forced() {
    if (globalCanvas && globalCanvas->getBuffer()) {
        adversary::ScreenManager::getInstance().render(*globalCanvas);
        globalCanvas->pushSprite(0, 0);
    }
}
