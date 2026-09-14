/**
 * @file screen_registry.cpp
 * @brief The single home for the screen→factory table (slice-0023).
 *
 * This is the one translation unit that #includes every concrete screen header.
 * Keeping it here — rather than in main.cpp — is the whole point: the entry point
 * stays decoupled from the screen catalogue and only names the few screens it
 * downcasts directly.
 */

#include "ui/screen_registry.h"

#include <Arduino.h>

#include "ui/screen_manager.h"

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
#include "ui/screens/radio_screen.h"
#include "ui/screens/modules_screen.h"

namespace adversary {

void registerAllScreens(ScreenManager& manager) {
    // Scanner / Sniffer — closures use getActiveScreen() so no captured pointer.
    manager.registerFactory(ScreenId::SCANNER,
        []() -> IScreen* { return new ScannerScreen(); });
    manager.registerFactory(ScreenId::SNIFFER,
        []() -> IScreen* { return new SnifferScreen(); });

    manager.registerFactory(ScreenId::BLE_SCANNER,
        []() -> IScreen* { return new BleScannerScreen(); });

    // Attack screens (setParams override for cross-screen target hand-off).
    manager.registerFactory(ScreenId::DEAUTH,
        []() -> IScreen* { return new DeauthScreen(); });
    manager.registerFactory(ScreenId::HANDSHAKE,
        []() -> IScreen* { return new HandshakeScreen(); });
    manager.registerFactory(ScreenId::EVIL_TWIN,
        []() -> IScreen* { return new EvilTwinScreen(); });
    manager.registerFactory(ScreenId::PROBE_FLOOD,
        []() -> IScreen* { return new ProbeFloodScreen(); });
    manager.registerFactory(ScreenId::KARMA,
        []() -> IScreen* { return new KarmaScreen(); });
    manager.registerFactory(ScreenId::BEACON_SPAM,
        []() -> IScreen* { return new BeaconSpamScreen(); });
    manager.registerFactory(ScreenId::ABOUT,
        []() -> IScreen* { return new AboutScreen(); });
    manager.registerFactory(ScreenId::RADIO,
        []() -> IScreen* { return new RadioScreen(); });
    manager.registerFactory(ScreenId::MODULES,
        []() -> IScreen* { return new ModulesScreen(); });
    manager.registerFactory(ScreenId::WHITELIST,
        []() -> IScreen* { return new WhitelistScreen(); });
    manager.registerFactory(ScreenId::CAPTURES,
        []() -> IScreen* { return new CapturesScreen(); });
    manager.registerFactory(ScreenId::RFID,
        []() -> IScreen* { return new RfidScreen(); });
    manager.registerFactory(ScreenId::INFRARED_TVB_GONE,
        []() -> IScreen* { return new IrTvBGoneScreen(); });
    manager.registerFactory(ScreenId::INFRARED_RECORD,
        []() -> IScreen* { return new IrRecordScreen(); });
    manager.registerFactory(ScreenId::USB_BADUSB,
        []() -> IScreen* { return new BadUsbScreen(); });
    manager.registerFactory(ScreenId::HID_MOUSE_JIGGLER,
        []() -> IScreen* { return new MouseJigglerScreen(); });
    manager.registerFactory(ScreenId::BLE_BAD_BLE,
        []() -> IScreen* { return new BleBadBleScreen(); });
    manager.registerFactory(ScreenId::BLE_SPOOF,
        []() -> IScreen* { return new BleSpoofScreen(); });
    manager.registerFactory(ScreenId::BLE_APPLE_ATTACK,
        []() -> IScreen* { return new BleAppleAttackScreen(); });

    // Settings owns a cross-screen jump to Saved Networks. The active screen is
    // re-synced from the manager at the top of loop(), so setActiveScreen() here
    // matches the former navigateToScreen() call exactly, without depending on
    // that main.cpp free function.
    manager.registerFactory(ScreenId::SETTINGS,
        []() -> IScreen* {
            auto* s = new SettingsScreen();
            s->setOnSavedNetworksRequested([]() {
                Serial.println("[Settings] Navigating to Saved Networks");
                ScreenManager::getInstance().setActiveScreen(ScreenId::SAVED_NETWORKS);
            });
            return s;
        });
    manager.registerFactory(ScreenId::SAVED_NETWORKS,
        []() -> IScreen* { return new SavedNetworksScreen(); });
    manager.registerFactory(ScreenId::WARDRIVING,
        []() -> IScreen* { return new WardrivingScreen(); });
    manager.registerFactory(ScreenId::BLE_SPAM,
        []() -> IScreen* { return new BleSpamScreen(); });
    manager.registerFactory(ScreenId::SERVER_MENU,
        []() -> IScreen* { return new ServerMenuScreen(); });
    manager.registerFactory(ScreenId::SERVER_STATUS,
        []() -> IScreen* { return new ServerStatusScreen(); });

    Serial.println("[Init] All screens registered with adversary::ScreenManager");
}

} // namespace adversary
