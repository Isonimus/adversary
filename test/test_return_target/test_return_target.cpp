/**
 * @file test_return_target.cpp
 * @brief Regression tests for the one-shot return breadcrumb (slice-0031).
 *
 * Guards the consume-once semantics that make list->attack->ESC return to the list: the danger is
 * a stale breadcrumb bouncing the *next* normal exit back into a list, so the default, the
 * single-consume, the reset-after-consume, and clear() are all asserted. Fails to compile before
 * this slice (the type does not exist) and passes after.
 */

#include <unity.h>
#include "ui/return_target_slot.h"

using namespace adversary;

void setUp(void) {}
void tearDown(void) {}

// An untouched slot means "no drill-down": exit goes to the root menu in IDLE.
void test_default_is_menu_idle() {
    ReturnTargetSlot slot;
    ReturnTarget t = slot.consume();
    TEST_ASSERT_EQUAL_INT((int)ScreenId::MENU, (int)t.screen);
    TEST_ASSERT_EQUAL_INT((int)AppState::IDLE, (int)t.state);
}

// An armed slot returns exactly the screen+state that was set.
void test_set_then_consume_returns_target() {
    ReturnTargetSlot slot;
    slot.set(ScreenId::SCANNER, AppState::SCANNING);
    ReturnTarget t = slot.consume();
    TEST_ASSERT_EQUAL_INT((int)ScreenId::SCANNER, (int)t.screen);
    TEST_ASSERT_EQUAL_INT((int)AppState::SCANNING, (int)t.state);
}

// consume() is one-shot: the second exit falls back to {MENU, IDLE}, never replays the list.
void test_consume_is_one_shot() {
    ReturnTargetSlot slot;
    slot.set(ScreenId::SNIFFER, AppState::SCANNING);
    slot.consume();  // first exit: returns to the sniffer
    ReturnTarget second = slot.consume();
    TEST_ASSERT_EQUAL_INT((int)ScreenId::MENU, (int)second.screen);
    TEST_ASSERT_EQUAL_INT((int)AppState::IDLE, (int)second.state);
}

// clear() disarms an armed slot without a consume (e.g. a flow abandoned before exit).
void test_clear_disarms() {
    ReturnTargetSlot slot;
    slot.set(ScreenId::SCANNER, AppState::SCANNING);
    slot.clear();
    ReturnTarget t = slot.consume();
    TEST_ASSERT_EQUAL_INT((int)ScreenId::MENU, (int)t.screen);
    TEST_ASSERT_EQUAL_INT((int)AppState::IDLE, (int)t.state);
}

// A second set() overwrites the first: the most recent drill-down wins.
void test_set_overwrites() {
    ReturnTargetSlot slot;
    slot.set(ScreenId::SCANNER, AppState::SCANNING);
    slot.set(ScreenId::SNIFFER, AppState::SCANNING);
    ReturnTarget t = slot.consume();
    TEST_ASSERT_EQUAL_INT((int)ScreenId::SNIFFER, (int)t.screen);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_default_is_menu_idle);
    RUN_TEST(test_set_then_consume_returns_target);
    RUN_TEST(test_consume_is_one_shot);
    RUN_TEST(test_clear_disarms);
    RUN_TEST(test_set_overwrites);
    return UNITY_END();
}
