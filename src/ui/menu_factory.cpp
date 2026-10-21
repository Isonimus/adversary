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

    // Handshake has two peer flows (ADR-0094): Auto Hunt (self-targeting) and Manual
    // (scanner-targeted). A nested submenu surfaces Auto Hunt as a first-class entry
    // instead of hiding it behind an in-screen mode toggle.
    std::vector<MenuItem> handshakeMenu = {
        MenuItem::action("Auto Hunt", ACTION_HANDSHAKE_AUTO, 'a'),
        MenuItem::action("Manual Capture", ACTION_HANDSHAKE_MANUAL, 'm'),
        MenuItem::separator(),
        MenuItem::back()
    };

    // Wireless submenu, grouped Recon / Attacks / Data with a separator between bands so the
    // ~12 entries read as three intents instead of one flat wall (Back last: entry lands the
    // cursor on a real action, and with wrap-around nav Back is one Up-press away from the top).
    std::vector<MenuItem> wirelessMenu = {
        // Recon: passive discovery, nothing transmitted at the target.
        MenuItem::action("Scan Networks", ACTION_SCAN_NETWORKS, 's'),
        MenuItem::action("Packet Sniffer", ACTION_PACKET_SNIFFER, 'p'),
        // Live-gated (slice-0042): a GPS attached after boot un-greys Wardriving on the next
        // frame after a Modules Re-scan refreshes g_gpsDetected, with no menu rebuild. Keyed
        // 'w', not 'd': 'd' collided with Deauth below and was a dead shortcut (the first
        // 'd' match won, so Wardriving's key never fired).
        MenuItem::gated("Wardriving", ACTION_WARDRIVING, 'w', "Wardriving (No GPS)",
                        []{ return adversary::ui::g_gpsDetected; }),

        MenuItem::separator(),
        // Attacks: active transmission. Handshake leads as the headline WPA-capture flow.
        MenuItem::submenu("Handshake", handshakeMenu, 'h'),
        MenuItem::action("Deauth Attack", ACTION_DEAUTH_ATTACK, 'd'),
        MenuItem::action("Beacon Spam", ACTION_BEACON_SPAM, 'b'),
        MenuItem::action("Probe Flood", ACTION_PROBE_FLOOD, 'f'),
        MenuItem::action("Evil Twin AP", ACTION_EVIL_TWIN, 'e'),
        MenuItem::action("Karma AP", ACTION_KARMA_AP, 'k'),

        MenuItem::separator(),
        // Data: browse/manage what the recon and attacks produced. Captures is a pure browser
        // over SD capture files — nothing to show without a card, so gate it on the cached
        // isReady() bool live (slice-0042): mounting a card (boot or Retry SD Mount) un-greys
        // it on the next frame. Safe to poll per-frame because isReady() is a flag read, not
        // SD I/O (unlike the SERVER tile's fileExists gate).
        MenuItem::gated("Captures", ACTION_CAPTURES, 'c', "Captures (No SD)",
                        []{ return adversary::SDManager::getInstance().isReady(); }),
        MenuItem::action("Whitelist", ACTION_WHITELIST, 'l'),

        MenuItem::separator(),
        MenuItem::back()
    };

    // BLE submenu
    std::vector<MenuItem> bleMenu = {
        MenuItem::action("BLE Scanner", ACTION_BLE_SCAN, 's'),
        MenuItem::action("Apple Attack", ACTION_BLE_APPLE_ATTACK, 'a'),
        // BadBLE has no built-in scripts — it only runs SD .txt scripts, so it is useless
        // without a card (slice-0021). Live-gated on the cached isReady() bool (slice-0042).
        MenuItem::gated("BadBLE (HID)", ACTION_BLE_BAD_BLE, 'h', "BadBLE (No SD)",
                        []{ return adversary::SDManager::getInstance().isReady(); }),
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
        // Live-gated (slice-0042): an RFID module hot-inserted and picked up by the Modules
        // Re-scan un-greys this on the next frame, matching the carousel RFID tile, with no
        // reboot or menu rebuild. This is the submenu-greying desync slice-0042 fixes.
        MenuItem::gated("RFID Dashboard", ACTION_RFID_DASHBOARD, 'd', "RFID (Module missing)",
                        []{ return adversary::ui::g_rfidDetected; }),
        // RFID Write (slice-0043): restore a .nfc dump onto a card. Same live gate as the
        // dashboard (needs the module); opens the RFID screen straight into its write flow.
        MenuItem::gated("RFID Write", ACTION_RFID_WRITE, 'w', "RFID Write (Module missing)",
                        []{ return adversary::ui::g_rfidDetected; }),
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
        {"WIRELESS", adversary::assets::ICON_WIRELESS, -1, true, "", nullptr}, // -1 = navigate to submenu in mainMenu
        {"BLE", adversary::assets::ICON_BLE, -2, true, "", nullptr},
        {"INFRARED", adversary::assets::ICON_INFRARED, -3, true, "", nullptr},
        {"RFID", adversary::assets::ICON_RFID, -4, false, "RFID module not found",
         []{ return adversary::ui::g_rfidDetected; }},
        {"HID", adversary::assets::ICON_HID, -5, true, "", nullptr}
    };

    // Radio entry — always present, greyed out when the multi-radio cap is not
    // detected, matching the RFID entry's optional-hardware convention (slice-0002).
    carouselItems.push_back({"RADIO", adversary::assets::ICON_RADIO, ACTION_RADIO,
                             false, "Multi-radio cap not found",
                             []{ return adversary::hal::resolvedExpansionCap() ==
                                        adversary::hal::ExpansionCap::MultiRadio; }});

    // Modules entry — live peripheral inventory + hot-swap Re-scan (slice-0018).
    carouselItems.push_back({"MODULES", adversary::assets::ICON_MODULES, ACTION_MODULES,
                             true, "", nullptr});

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
    carouselItems.push_back({"SERVER", adversary::assets::ICON_SERVER, ACTION_SERVER, serverEnabled, serverDisabledReason, nullptr});

    carouselItems.push_back({"SETTINGS", adversary::assets::ICON_SETTINGS, ACTION_SETTINGS, true, "", nullptr});
    carouselItems.push_back({"ABOUT", adversary::assets::ICON_ABOUT, ACTION_ABOUT, true, "", nullptr});

    return carouselItems;
}

} // namespace adversary
