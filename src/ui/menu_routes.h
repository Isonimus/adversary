/**
 * @file menu_routes.h
 * @brief Data-driven navigation table behind handleMenuAction().
 *
 * Each selectable menu action maps to a target screen and, optionally, an AppState
 * transition. This is the data half of what main.cpp's handleMenuAction() used to hold as a
 * 26-arm switch — every arm was navigateToScreen(screen) + optional transitionTo(state)
 * (slice-0029). Kept beside the menu catalogue (slice-0024 put the action IDs in
 * menu_actions.h and the item builders in menu_factory.cpp) but in its own display-free TU so
 * it compiles in the native build and the mapping is unit-testable — menu_factory.cpp is
 * display-coupled and native-excluded.
 */

#pragma once

#include "core/state_machine.h"           // AppState
#include "ui/menu_actions.h"              // MenuActionId
#include "ui/screens/screen_interface.h" // ScreenId

namespace adversary {

/**
 * @brief One menu action's navigation target.
 *
 * @var transitions  Whether selecting this action changes the AppState. Browsers and tools
 *                    (Captures, Settings, Radio, …) navigate without a transition — the old
 *                    switch simply omitted transitionTo() for them, leaving the state where
 *                    stopAllAttacks() left it. When false, @c state is unused.
 */
struct MenuRoute {
    MenuActionId action;       ///< The MenuActionId this route dispatches
    ScreenId     screen;       ///< Screen to navigate to
    bool         transitions;  ///< Whether to change AppState (false = leave it unchanged)
    AppState     state;        ///< Target state, applied only when transitions == true
};

/**
 * @brief Look up the navigation route for a menu action.
 * @param actionId a MenuActionId value (int to match handleMenuAction's signature)
 * @return the matching MenuRoute, or nullptr if the action is unknown
 */
const MenuRoute* findMenuRoute(int actionId);

} // namespace adversary
