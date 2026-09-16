/**
 * @file test_menu_routes.cpp
 * @brief Regression tests for the menu dispatch route table (slice-0029).
 *
 * Guards the {action -> screen, transition, state} mapping that replaced handleMenuAction()'s
 * switch. Covers both transition classes (scan/attack), a no-transition browser, a former
 * redundant-stopAllAttacks arm (to guard the boyscout removal not perturbing routing), and the
 * unknown-id path. Fails before this slice (findMenuRoute does not exist) and passes after.
 */

#include <unity.h>
#include "ui/menu_routes.h"
#include "ui/menu_actions.h"

using namespace adversary;

void setUp(void) {}
void tearDown(void) {}

// A scan action routes to its screen and moves the app into SCANNING.
void test_route_scan_networks() {
    const MenuRoute* r = findMenuRoute(ACTION_SCAN_NETWORKS);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::SCANNER), static_cast<int>(r->screen));
    TEST_ASSERT_TRUE(r->transitions);
    TEST_ASSERT_EQUAL(static_cast<int>(AppState::SCANNING), static_cast<int>(r->state));
}

// An attack action routes to its screen and moves the app into ATTACKING.
void test_route_deauth_attacks() {
    const MenuRoute* r = findMenuRoute(ACTION_DEAUTH_ATTACK);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::DEAUTH), static_cast<int>(r->screen));
    TEST_ASSERT_TRUE(r->transitions);
    TEST_ASSERT_EQUAL(static_cast<int>(AppState::ATTACKING), static_cast<int>(r->state));
}

// A browser action navigates without a state transition (the old switch omitted transitionTo
// for these — the state machine stays where stopAllAttacks() left it).
void test_route_captures_has_no_transition() {
    const MenuRoute* r = findMenuRoute(ACTION_CAPTURES);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::CAPTURES), static_cast<int>(r->screen));
    TEST_ASSERT_FALSE(r->transitions);
}

// BadBLE used to carry a redundant second stopAllAttacks() call; it still routes to its
// screen with no transition — guards the boyscout removal not changing routing.
void test_route_bad_ble_has_no_transition() {
    const MenuRoute* r = findMenuRoute(ACTION_BLE_BAD_BLE);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::BLE_BAD_BLE), static_cast<int>(r->screen));
    TEST_ASSERT_FALSE(r->transitions);
}

// The server action (another former redundant-stopAllAttacks arm) routes to the server menu
// without a transition.
void test_route_server_menu() {
    const MenuRoute* r = findMenuRoute(ACTION_SERVER);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::SERVER_MENU), static_cast<int>(r->screen));
    TEST_ASSERT_FALSE(r->transitions);
}

// An unknown action id resolves to nullptr (handleMenuAction then logs "Unknown action").
void test_route_unknown_returns_null() {
    TEST_ASSERT_NULL(findMenuRoute(9999));
    TEST_ASSERT_NULL(findMenuRoute(-1));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_route_scan_networks);
    RUN_TEST(test_route_deauth_attacks);
    RUN_TEST(test_route_captures_has_no_transition);
    RUN_TEST(test_route_bad_ble_has_no_transition);
    RUN_TEST(test_route_server_menu);
    RUN_TEST(test_route_unknown_returns_null);
    return UNITY_END();
}
