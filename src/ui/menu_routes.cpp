/**
 * @file menu_routes.cpp
 * @brief The menu action → {screen, state} route table (slice-0029).
 */

#include "ui/menu_routes.h"

namespace adversary {

namespace {

// One row per selectable menu action, copied 1:1 from the old handleMenuAction() switch.
// Order is immaterial (lookup is by action id). Scan/attack screens carry an AppState
// transition; browsers and tools navigate with transitions=false, leaving the state machine
// where the top-of-handler stopAllAttacks() left it — exactly as the switch did by omitting
// transitionTo() for those arms. The IDLE in no-transition rows is an unused filler.
constexpr MenuRoute MENU_ROUTES[] = {
    // Wireless
    {ACTION_SCAN_NETWORKS,     ScreenId::SCANNER,          true,  AppState::SCANNING},
    {ACTION_PACKET_SNIFFER,    ScreenId::SNIFFER,          true,  AppState::SCANNING},
    {ACTION_DEAUTH_ATTACK,     ScreenId::DEAUTH,           true,  AppState::ATTACKING},
    {ACTION_BEACON_SPAM,       ScreenId::BEACON_SPAM,      true,  AppState::ATTACKING},
    {ACTION_PROBE_FLOOD,       ScreenId::PROBE_FLOOD,      true,  AppState::ATTACKING},
    {ACTION_HANDSHAKE_CAPTURE, ScreenId::HANDSHAKE,        true,  AppState::ATTACKING},
    {ACTION_EVIL_TWIN,         ScreenId::EVIL_TWIN,        true,  AppState::ATTACKING},
    {ACTION_KARMA_AP,          ScreenId::KARMA,            true,  AppState::ATTACKING},
    {ACTION_CAPTURES,          ScreenId::CAPTURES,         false, AppState::IDLE},
    {ACTION_WARDRIVING,        ScreenId::WARDRIVING,       true,  AppState::SCANNING},
    // BLE
    {ACTION_BLE_SCAN,          ScreenId::BLE_SCANNER,      true,  AppState::SCANNING},
    {ACTION_BLE_APPLE_ATTACK,  ScreenId::BLE_APPLE_ATTACK, false, AppState::IDLE},
    {ACTION_BLE_BAD_BLE,       ScreenId::BLE_BAD_BLE,      false, AppState::IDLE},
    {ACTION_BLE_SPOOF,         ScreenId::BLE_SPOOF,        false, AppState::IDLE},
    {ACTION_BLE_SPAM,          ScreenId::BLE_SPAM,         false, AppState::IDLE},
    // HID
    {ACTION_USB_BADUSB,        ScreenId::USB_BADUSB,       false, AppState::IDLE},
    {ACTION_MOUSE_JIGGLER,     ScreenId::HID_MOUSE_JIGGLER,false, AppState::IDLE},
    // Infrared
    {ACTION_IR_TVB_GONE,       ScreenId::INFRARED_TVB_GONE,false, AppState::IDLE},
    {ACTION_IR_RECORD,         ScreenId::INFRARED_RECORD,  false, AppState::IDLE},
    // Settings / info
    {ACTION_SETTINGS,          ScreenId::SETTINGS,         false, AppState::IDLE},
    {ACTION_ABOUT,             ScreenId::ABOUT,            false, AppState::IDLE},
    {ACTION_WHITELIST,         ScreenId::WHITELIST,        false, AppState::IDLE},
    // Cap / modules / rfid / server
    {ACTION_RADIO,             ScreenId::RADIO,            false, AppState::IDLE},
    {ACTION_MODULES,           ScreenId::MODULES,          false, AppState::IDLE},
    {ACTION_RFID_DASHBOARD,    ScreenId::RFID,             false, AppState::IDLE},
    {ACTION_SERVER,            ScreenId::SERVER_MENU,      false, AppState::IDLE},
};

} // namespace

const MenuRoute* findMenuRoute(int actionId) {
    for (const MenuRoute& route : MENU_ROUTES) {
        if (route.action == actionId) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace adversary
