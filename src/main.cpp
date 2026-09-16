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
// main.cpp holds no concrete screen types: the full screen->factory catalogue lives in
// screen_registry.cpp (slice-0023), and screen->attack navigation now flows through the
// ATTACK_TARGET_SELECTED EventBus event (slice-0028), not concrete-type downcasts.
#include "modules/server/server_manager.h"
#include "ui/screens/splash_screen.h"      // global splashScreen instance
#include "ui/screen_registry.h"
#include "ui/screen_manager.h"
#include "ui/components/menu.h"
#include "modules/storage/capture_registry.h"
#include "modules/storage/settings_manager.h"
#include "modules/notification/notification_manager.h"
#include "modules/wifi/wifi_connection.h"
#include "ui/components/toast_manager.h"
#include "modules/gps/gps_manager.h"
#include "modules/gps/gps_probe.h"
#include "modules/rfid/rfid_manager.h"
#include "modules/system/system_manager.h"
#include "modules/system/time_manager.h"
#include "modules/ble/ble_scanner.h"
#include "modules/ble/ble_spanner.h"   // stopAllAttacks() -> BLESpanner::forceRelease()
#include "ui/screens/ble_scanner_screen.h"

#include "hal/input/input_manager.h"
#include "hal/input/input_keymap.h"    // normalizeCardputerKey()/inputActionToKey() (slice-0030)
#include "hal/expansion/expansion_cap.h"
#include "ui/components/carousel_menu.h"
#include "ui/menu_actions.h"    // MenuActionId (shared with menu_factory.cpp, slice-0024)
#include "ui/menu_factory.h"    // buildRootMenuItems() / buildCarouselItems() (slice-0024)
#include "ui/menu_routes.h"     // findMenuRoute() dispatch table (slice-0029)
#include "ui/screens/radio_screen.h"
#include "ui/screens/modules_screen.h"
#include "core/module_detection.h"
#include "assets/generated_icons.h"
#include "utils/bitmap_remapper.h"
#include "utils/screenshot.h"

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


// Menu action IDs (MenuActionId) live in ui/menu_actions.h — shared between the menu
// catalogue in menu_factory.cpp and handleMenuAction() below (slice-0024).

// Forward declarations
void initHardware();
void showMenu();
void handleInput();
void handleMenuAction(int actionId);
// adversary::initializeMenu() is declared in core/module_detection.h (a namespaced
// composition-root callback like redetectModules(), so screens can trigger a rebuild).
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
// Every screen is lazy-loaded via a factory; the factory table is registered by
// adversary::registerAllScreens() (screen_registry.cpp, slice-0023).
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
    
    // Tear down whatever screen is active through its polymorphic lifecycle hook.
    // hide() is each screen's full teardown (stops its radio, releases its heap,
    // unsubscribes its EventBus handlers) and is idempotent — the subsequent screen
    // transition calls hide() again via ScreenManager::setActiveScreen(). No concrete
    // screen type is named here: the four former downcasts (Scanner/Sniffer setActive,
    // Evil Twin/Karma stop+forceStopPortal) are now subsumed by their own hide().
    adversary::IScreen* activeScr = adversary::ScreenManager::getInstance().getActiveScreen();
    if (activeScr) {
        activeScr->hide();
    }

    // AGGRESSIVE CLEANUP: Force full BLE radio release
    // Screens preserve BLE state in hide() for quick restart,
    // but when switching to a different module we must reclaim heap
    adversary::BLESpanner::getInstance().forceRelease();
    adversary::BLEScanner::getInstance().deinit();

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
 * @brief Navigate to the attack chosen on a scanned network / sniffed packet.
 *
 * Handles ATTACK_TARGET_SELECTED, published by ScannerScreen/SnifferScreen (slice-0028).
 * Deliberately screen-agnostic: the target attack screen and the victim identity both
 * arrive in the event, so this launches any attack without naming one — the shape the
 * future MenuController/ScreenManager push API wants. stopAllAttacks() runs first and,
 * because the scanner/sniffer is still the active screen at this point, tears it down via
 * its own hide() before the attack screen inits.
 */
static void launchAttackTarget(const adversary::EventData& evt) {
    stopAllAttacks();
    adversary::IScreen::ScreenParams p;
    memcpy(p.bssid, evt.payload.attackTarget.bssid, 6);
    strncpy(p.ssid, evt.payload.attackTarget.ssid, 32);
    p.ssid[32] = '\0';
    p.channel = evt.payload.attackTarget.channel;
    screenMgr.navigateWithParams(
        static_cast<adversary::ScreenId>(evt.payload.attackTarget.targetScreen), p);
    stateMachine.transitionTo(adversary::AppState::ATTACKING);
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

namespace adversary {

// One arbitration-correct detection pass over every hot-swappable peripheral
// (slice-0018). Declared in core/module_detection.h and defined here — the native
// build compiles core/ but excludes expansion_cap.cpp, so this cannot live in a
// core TU; main.cpp already owns the detection globals and is native-excluded.
// Called at boot and by the Modules dashboard's Re-scan so the two never drift.
void redetectModules() {
    using namespace adversary::hal;
    SDManager& sd = SDManager::getInstance();
    const CapOverride ov = SettingsManager::getInstance().get().wireless.capOverride;

    // --- Expansion cap (SPI, shared with the SD bus) ---
    // Claim the FSPI bus for an auto-probe if a launcher-chained boot left the card
    // mounted by the launcher (we don't own the SPIClass). A no-op once we own it;
    // a forced override needs no probe so it never pays this cost.
    if (ov == CapOverride::Auto && sd.spiBus() == nullptr) {
        Serial.printf("[Modules] Claiming SD bus for cap probe: %s\n",
                      sd.remount() ? "OK" : "FAILED");
    }
    ui::g_expansionCap = detectExpansionCap(ov);
    // The shared-bus cap probe desyncs the card's SPI state machine; re-mount to
    // re-sync it whenever the probe could have run (any non-forced-None override).
    if (ov != CapOverride::ForceNone) {
        Serial.printf("[Modules] SD re-mount after cap probe: %s\n",
                      sd.remount() ? "OK" : "FAILED");
    }
    Serial.printf("[Modules] Cap: %s\n",
                  ui::g_expansionCap == ExpansionCap::MultiRadio ? "Multi-Radio" : "None");

    // --- GPS (cap-aware: never drive the cap-GPS UART pins when the cap owns them) ---
    const bool probeCapGps = ui::g_expansionCap != ExpansionCap::MultiRadio;
    ui::g_gpsDetected = GPSManager::getInstance().tryRedetect(probeCapGps);

    // --- RFID (skip only on the real Grove-port G1/G2 conflict with a Grove GPS) ---
    const char* gpsSource = GPSManager::getInstance().getDetectedPinSet();
    if (gps::gpsBlocksRfid(ui::g_gpsDetected, gpsSource)) {
        Serial.println("[Modules] RFID skipped - Grove-port GPS owns GPIO 1/2");
        ui::g_rfidDetected = false;
    } else {
        ui::g_rfidDetected = RFIDManager::getInstance().redetect();
    }
    Serial.printf("[Modules] GPS:%s RFID:%s\n",
                  ui::g_gpsDetected ? "detected" : "none",
                  ui::g_rfidDetected ? "detected" : "none");
}

} // namespace adversary

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

        // Expansion cap + GPS + RFID are detected together by the unified
        // redetectModules() pass below (slice-0018), which owns the SD-bus claim
        // and re-mount the shared-bus cap probe needs. Kept out of this SD-mount
        // block so GPS and RFID still probe when no SD card is present.

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

    // Detect every hot-swappable peripheral in one arbitration-correct pass:
    // cap -> GPS (cap-aware pin skip) -> RFID (Grove-conflict aware). The Modules
    // dashboard's Re-scan calls the same redetectModules() so boot and re-scan
    // never drift (slice-0018).
    splashScreen.updateProgress(0.75f, "Detecting modules...");
    adversary::redetectModules();
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
    adversary::initializeMenu();

    // Tell the operator once, at idle, that storage features are unavailable when
    // no card mounted — otherwise a cardless boot degrades silently and SD actions
    // just fail with no explanation. Recover without a reboot via Settings > SD
    // (slice-0020).
    if (sdManager.getStatus() == adversary::SDStatus::NO_CARD) {
        adversary::ToastManager::getInstance().show(
            "No SD card - captures, logs & dashboard disabled",
            adversary::ToastType::WARNING,
            adversary::ToastPriority::PRIORITY_MEDIUM,
            4000);
    }

    // The screen->factory table lives in screen_registry.cpp so this entry point
    // no longer #includes the ~25 concrete screen headers it never otherwise names
    // (slice-0023).
    adversary::registerAllScreens(screenMgr);

    // Route Scanner/Sniffer attack-target selections to the navigator. Subscribed once for
    // the program's lifetime — the handler is stateless (slice-0028).
    adversary::EventBus::getInstance().subscribe(
        adversary::EventType::ATTACK_TARGET_SELECTED,
        [](const adversary::EventData& evt) { launchAttackTarget(evt); });

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
void adversary::initializeMenu() {
    // The menu/carousel *catalogue* (items, greying, enabledFn gates) is built by the
    // menu factory (menu_factory.cpp, slice-0024); initializeMenu() only wires it up. The
    // onAction callbacks stay here because they reference main.cpp state — handleMenuAction()
    // for dispatch, and the carousel's negative-actionId path drives mainMenu + showMenu().
    mainMenu.setTitle("THE ADVERSARY");
    mainMenu.setItems(adversary::buildRootMenuItems());
    mainMenu.setOnAction([](int actionId) {
        handleMenuAction(actionId);
    });

    carouselMenu.setItems(adversary::buildCarouselItems());
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
        
        // Normalize modifier state + pressed key into the routing char (slice-0030).
        char key = adversary::normalizeCardputerKey(state.enter, state.del, pressedKey);
        if (key == 0) return;  // No valid key

        // Global hotkey: Fn+S captures the canvas to SD from any screen. Hooked
        // here (not per-screen) because Cardputer input is dispatched centrally
        // and Fn is otherwise unused; Fn does not remap the character, so this
        // never collides with text entry. Intercept before routing so no screen
        // sees the key.
        if (state.fn && (key == 's' || key == 'S')) {
            if (globalCanvas) {
                char shotPath[64] = {0};
                if (adversary::saveScreenshot(*globalCanvas, sdManager, shotPath, sizeof(shotPath))) {
                    adversary::showSuccessToast(shotPath);
                } else {
                    adversary::showErrorToast("Screenshot failed");
                }
            }
            return;
        }

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
        
        // Map the logical action to the routing char (slice-0030).
        char pressedKey = adversary::inputActionToKey(action);
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
    Serial.printf("[Main] handleMenuAction: %d\n", actionId);

    // Stop any running attacks/modules before starting the next one. Runs once for every
    // action — the six arms that used to repeat this call (BadBLE/BadUSB/etc.) were redundant.
    stopAllAttacks();

    // Dispatch is data: findMenuRoute() maps the action to its target screen and, for
    // scan/attack screens, an AppState transition. Browsers and tools navigate without one
    // (route->transitions == false). The table lives beside the menu catalogue in
    // ui/menu_routes.{h,cpp} (slice-0029).
    const adversary::MenuRoute* route = adversary::findMenuRoute(actionId);
    if (!route) {
        Serial.printf("[Main] Unknown action: %d\n", actionId);
        return;
    }
    navigateToScreen(route->screen);
    if (route->transitions) {
        stateMachine.transitionTo(route->state);
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
