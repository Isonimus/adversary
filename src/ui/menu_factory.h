#pragma once

#include <vector>

#include "ui/components/menu.h"
#include "ui/components/carousel_menu.h"

namespace adversary {

/**
 * @brief Build the hierarchical root menu tree (submenus + top-level actions).
 *
 * Returns the root MenuItem vector — five submenus (Wireless/BLE/Infrared/RFID/HID) plus
 * Settings/About — with each entry greyed per live detection state read from the same
 * singletons/flags main.cpp reads: SDManager::getInstance(), ui::g_gpsDetected,
 * ui::g_rfidDetected. The caller sets these on the Menu and attaches the onAction callback.
 *
 * Extracted from main.cpp's initializeMenu() so the entry point wires the menu rather than
 * enumerating it (slice-0024). The onAction dispatch (handleMenuAction) stays in main.cpp.
 */
std::vector<MenuItem> buildRootMenuItems();

/**
 * @brief Build the root carousel tiles.
 *
 * Hardware-gated tiles (RFID/RADIO/MODULES/SERVER) carry a live enabledFn (or a computed
 * enabled bool) so the Modules dashboard's hot-swap Re-scan reflects the current detection
 * state without a reboot (slice-0018); always-on tiles use a plain bool. Reads the same
 * detection state as buildRootMenuItems(); the caller sets the items and attaches onAction.
 */
std::vector<CarouselItem> buildCarouselItems();

} // namespace adversary
