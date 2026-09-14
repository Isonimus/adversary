#pragma once

// Menu action IDs — the integer contract between the menu catalogue (which tags each
// MenuItem/CarouselItem with one) and main.cpp's handleMenuAction() (which switches on
// them). Kept unscoped and at global namespace so relocating it out of main.cpp
// (slice-0024) is a pure move: the enumerators are only ever used implicitly as int, so
// no call site changes. Namespacing / enum-class is a deliberate non-goal here — it would
// touch every case label and MenuItem::action() call and bury the move in noise.
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
    ACTION_RADIO = 700,

    // Module inventory / hot-swap re-detection (slice-0018)
    ACTION_MODULES = 701
};
