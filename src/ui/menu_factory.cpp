#include "ui/menu_factory.h"

#include "ui/menu_actions.h"
#include "ui/components/status_bar.h"        // ui::g_gpsDetected / ui::g_rfidDetected
#include "hal/storage/sd_manager.h"          // SDManager::getInstance()
#include "hal/expansion/expansion_cap.h"     // hal::resolvedExpansionCap()
#include "assets/generated_icons.h"          // assets::ICON_*

namespace adversary {

std::vector<MenuItem> buildRootMenuItems() {
    using adversary::MenuItem;
    using adversary::MenuItemType;

    auto& sdManager = adversary::SDManager::getInstance();

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
        // Captures is a pure browser over SD capture files — nothing to show
        // without a card, so grey it out (slice-0021). Un-greys on the next menu
        // rebuild, which the Settings > Retry SD Mount handler triggers.
        sdManager.isReady() ?
            MenuItem::action("Captures", ACTION_CAPTURES, 'c') :
            MenuItem::disabled("Captures (No SD)"),
        MenuItem::action("Whitelist", ACTION_WHITELIST, 'l'),
        MenuItem::separator(),
        MenuItem::back()
    };

    // BLE submenu
    std::vector<MenuItem> bleMenu = {
        MenuItem::action("BLE Scanner", ACTION_BLE_SCAN, 's'),
        MenuItem::action("Apple Attack", ACTION_BLE_APPLE_ATTACK, 'a'),
        // BadBLE has no built-in scripts — it only runs SD .txt scripts, so it is
        // useless without a card (slice-0021). Un-greys on the next menu rebuild.
        sdManager.isReady() ?
            MenuItem::action("BadBLE (HID)", ACTION_BLE_BAD_BLE, 'h') :
            MenuItem::disabled("BadBLE (No SD)"),
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

    return rootMenu;
}

std::vector<CarouselItem> buildCarouselItems() {
    auto& sdManager = adversary::SDManager::getInstance();

    // Initialize Carousel adversary::Menu for Root icons.
    // Hardware-gated tiles (RFID, RADIO) carry a live enabledFn instead of a
    // boot-frozen bool so the Modules dashboard's hot-swap Re-scan un-greys them
    // without a reboot (slice-0018); always-on tiles keep the plain bool.
    std::vector<adversary::CarouselItem> carouselItems = {
        {"WIRELESS", adversary::assets::ICON_WIRELESS, -1, true, ""}, // -1 = navigate to submenu in mainMenu
        {"BLE", adversary::assets::ICON_BLE, -2, true, ""},
        {"INFRARED", adversary::assets::ICON_INFRARED, -3, true, ""},
        {"RFID", adversary::assets::ICON_RFID, -4, false, "RFID module not found",
         []{ return adversary::ui::g_rfidDetected; }},
        {"HID", adversary::assets::ICON_HID, -5, true, ""}
    };

    // Radio entry — always present, greyed out when the multi-radio cap is not
    // detected, matching the RFID entry's optional-hardware convention (slice-0002).
    carouselItems.push_back({"RADIO", adversary::assets::ICON_RADIO, ACTION_RADIO,
                             false, "Multi-radio cap not found",
                             []{ return adversary::hal::resolvedExpansionCap() ==
                                        adversary::hal::ExpansionCap::MultiRadio; }});

    // Modules entry — live peripheral inventory + hot-swap Re-scan (slice-0018).
    carouselItems.push_back({"MODULES", adversary::assets::ICON_MODULES, ACTION_MODULES,
                             true, ""});

    // Server entry - serves the dashboard straight off SD, so it needs both a
    // mounted card and the dashboard files. Distinguish the two so the greyed-tile
    // reason is honest instead of always blaming a missing index (slice-0021). The
    // fileExists() check stays build-time (SD I/O must not run per-frame in the
    // carousel's live enabledFn); a menu rebuild on Retry SD Mount refreshes it.
    const bool sdReadyForServer = sdManager.isReady();
    const bool serverEnabled = sdReadyForServer &&
        sdManager.fileExists("/adversary/dashboard/index.html");
    const char* serverDisabledReason = sdReadyForServer ? "Dashboard index missing"
                                                        : "No SD card";
    carouselItems.push_back({"SERVER", adversary::assets::ICON_SERVER, ACTION_SERVER, serverEnabled, serverDisabledReason});

    carouselItems.push_back({"SETTINGS", adversary::assets::ICON_SETTINGS, ACTION_SETTINGS, true, ""});
    carouselItems.push_back({"ABOUT", adversary::assets::ICON_ABOUT, ACTION_ABOUT, true, ""});

    return carouselItems;
}

} // namespace adversary
