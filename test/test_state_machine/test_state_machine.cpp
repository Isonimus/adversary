/**
 * @file test_state_machine.cpp
 * @brief Unit tests for state machine
 */

#include <unity.h>
#include "core/state_machine.h"

using namespace adversary;

void setUp() {
    // Reset state machine before each test
    StateMachine::getInstance().reset();
}

void tearDown() {
    // Cleanup after each test
}

// ===========================================
// stateToString tests
// ===========================================

void test_stateToString_allStates() {
    TEST_ASSERT_EQUAL_STRING("BOOT", stateToString(AppState::BOOT));
    TEST_ASSERT_EQUAL_STRING("SPLASH", stateToString(AppState::SPLASH));
    TEST_ASSERT_EQUAL_STRING("IDLE", stateToString(AppState::IDLE));
    TEST_ASSERT_EQUAL_STRING("SCANNING", stateToString(AppState::SCANNING));
    TEST_ASSERT_EQUAL_STRING("SNIFFING", stateToString(AppState::SNIFFING));
    TEST_ASSERT_EQUAL_STRING("ATTACKING", stateToString(AppState::ATTACKING));
    TEST_ASSERT_EQUAL_STRING("CAPTURING", stateToString(AppState::CAPTURING));
    TEST_ASSERT_EQUAL_STRING("AP_RUNNING", stateToString(AppState::AP_RUNNING));
    TEST_ASSERT_EQUAL_STRING("ERROR", stateToString(AppState::ERROR));
    TEST_ASSERT_EQUAL_STRING("SHUTDOWN", stateToString(AppState::SHUTDOWN));
}

// ===========================================
// Initial state tests
// ===========================================

void test_initialState_isBoot() {
    StateMachine& sm = StateMachine::getInstance();
    TEST_ASSERT_EQUAL(AppState::BOOT, sm.getState());
}

// ===========================================
// Valid transition tests
// ===========================================

void test_transitionTo_bootToSplash() {
    StateMachine& sm = StateMachine::getInstance();
    
    bool result = sm.transitionTo(AppState::SPLASH);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::SPLASH, sm.getState());
    TEST_ASSERT_EQUAL(AppState::BOOT, sm.getPreviousState());
}

void test_transitionTo_splashToIdle() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    
    bool result = sm.transitionTo(AppState::IDLE);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::IDLE, sm.getState());
}

void test_transitionTo_idleToScanning() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    
    bool result = sm.transitionTo(AppState::SCANNING);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::SCANNING, sm.getState());
}

void test_transitionTo_idleToAllActiveStates() {
    StateMachine& sm = StateMachine::getInstance();
    
    // Test each active state transition from IDLE
    AppState activeStates[] = {
        AppState::SCANNING,
        AppState::SNIFFING,
        AppState::ATTACKING,
        AppState::CAPTURING,
        AppState::AP_RUNNING
    };
    
    for (AppState state : activeStates) {
        sm.reset();
        sm.transitionTo(AppState::SPLASH);
        sm.transitionTo(AppState::IDLE);
        
        bool result = sm.transitionTo(state);
        
        TEST_ASSERT_TRUE(result);
        TEST_ASSERT_EQUAL(state, sm.getState());
    }
}

void test_transitionTo_activeToIdle() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.transitionTo(AppState::SCANNING);
    
    bool result = sm.transitionTo(AppState::IDLE);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::IDLE, sm.getState());
}

// ===========================================
// Invalid transition tests
// ===========================================

void test_transitionTo_bootToIdle_invalid() {
    StateMachine& sm = StateMachine::getInstance();
    
    bool result = sm.transitionTo(AppState::IDLE);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(AppState::BOOT, sm.getState());  // Should remain in BOOT
}

void test_transitionTo_scanningToScanning_invalid() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.transitionTo(AppState::SCANNING);
    
    bool result = sm.transitionTo(AppState::ATTACKING);  // Can't go from active to active
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(AppState::SCANNING, sm.getState());
}

void test_transitionTo_shutdownToAnything_invalid() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.transitionTo(AppState::SHUTDOWN);
    
    bool result = sm.transitionTo(AppState::IDLE);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(AppState::SHUTDOWN, sm.getState());
}

// ===========================================
// isActiveState tests
// ===========================================

void test_isActiveState_activeStates() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    
    AppState activeStates[] = {
        AppState::SCANNING,
        AppState::SNIFFING,
        AppState::ATTACKING,
        AppState::CAPTURING,
        AppState::AP_RUNNING
    };
    
    for (AppState state : activeStates) {
        sm.transitionTo(state);
        TEST_ASSERT_TRUE(sm.isActiveState());
        sm.transitionTo(AppState::IDLE);  // Reset
    }
}

void test_isActiveState_inactiveStates() {
    StateMachine& sm = StateMachine::getInstance();
    
    TEST_ASSERT_FALSE(sm.isActiveState());  // BOOT
    
    sm.transitionTo(AppState::SPLASH);
    TEST_ASSERT_FALSE(sm.isActiveState());  // SPLASH
    
    sm.transitionTo(AppState::IDLE);
    TEST_ASSERT_FALSE(sm.isActiveState());  // IDLE
}

// ===========================================
// cancelCurrentOperation tests
// ===========================================

void test_cancelCurrentOperation_fromActiveState() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.transitionTo(AppState::SCANNING);
    
    bool result = sm.cancelCurrentOperation();
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::IDLE, sm.getState());
}

void test_cancelCurrentOperation_fromIdleState() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    
    bool result = sm.cancelCurrentOperation();
    
    TEST_ASSERT_FALSE(result);  // Nothing to cancel
    TEST_ASSERT_EQUAL(AppState::IDLE, sm.getState());
}

// ===========================================
// Error state tests
// ===========================================

void test_setError_transitionsToError() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    
    sm.setError("Test error message");
    
    TEST_ASSERT_EQUAL(AppState::ERROR, sm.getState());
    TEST_ASSERT_EQUAL_STRING("Test error message", sm.getErrorMessage());
}

void test_error_canTransitionToIdle() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.setError("Test error");
    
    bool result = sm.transitionTo(AppState::IDLE);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(AppState::IDLE, sm.getState());
    TEST_ASSERT_NULL(sm.getErrorMessage());  // Error cleared
}

// ===========================================
// Callback tests
// ===========================================

static AppState callbackOldState;
static AppState callbackNewState;
static int callbackCount;

void testCallback(AppState oldState, AppState newState) {
    callbackOldState = oldState;
    callbackNewState = newState;
    callbackCount++;
}

void test_onStateChange_callbackInvoked() {
    StateMachine& sm = StateMachine::getInstance();
    callbackCount = 0;
    
    sm.onStateChange(testCallback);
    sm.transitionTo(AppState::SPLASH);
    
    TEST_ASSERT_EQUAL(1, callbackCount);
    TEST_ASSERT_EQUAL(AppState::BOOT, callbackOldState);
    TEST_ASSERT_EQUAL(AppState::SPLASH, callbackNewState);
}

void test_onStateChange_notCalledOnInvalidTransition() {
    StateMachine& sm = StateMachine::getInstance();
    callbackCount = 0;
    
    sm.onStateChange(testCallback);
    sm.transitionTo(AppState::IDLE);  // Invalid from BOOT
    
    TEST_ASSERT_EQUAL(0, callbackCount);
}

// ===========================================
// canTransitionTo tests
// ===========================================

void test_canTransitionTo_valid() {
    StateMachine& sm = StateMachine::getInstance();
    
    TEST_ASSERT_TRUE(sm.canTransitionTo(AppState::SPLASH));
    TEST_ASSERT_TRUE(sm.canTransitionTo(AppState::ERROR));
}

void test_canTransitionTo_invalid() {
    StateMachine& sm = StateMachine::getInstance();
    
    TEST_ASSERT_FALSE(sm.canTransitionTo(AppState::IDLE));
    TEST_ASSERT_FALSE(sm.canTransitionTo(AppState::SCANNING));
}

// ===========================================
// Reset tests
// ===========================================

void test_reset_returnsToBootState() {
    StateMachine& sm = StateMachine::getInstance();
    sm.transitionTo(AppState::SPLASH);
    sm.transitionTo(AppState::IDLE);
    sm.transitionTo(AppState::SCANNING);
    
    sm.reset();
    
    TEST_ASSERT_EQUAL(AppState::BOOT, sm.getState());
    TEST_ASSERT_EQUAL(AppState::BOOT, sm.getPreviousState());
}

// ===========================================
// Test Runner
// ===========================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // stateToString tests
    RUN_TEST(test_stateToString_allStates);
    
    // Initial state tests
    RUN_TEST(test_initialState_isBoot);
    
    // Valid transition tests
    RUN_TEST(test_transitionTo_bootToSplash);
    RUN_TEST(test_transitionTo_splashToIdle);
    RUN_TEST(test_transitionTo_idleToScanning);
    RUN_TEST(test_transitionTo_idleToAllActiveStates);
    RUN_TEST(test_transitionTo_activeToIdle);
    
    // Invalid transition tests
    RUN_TEST(test_transitionTo_bootToIdle_invalid);
    RUN_TEST(test_transitionTo_scanningToScanning_invalid);
    RUN_TEST(test_transitionTo_shutdownToAnything_invalid);
    
    // isActiveState tests
    RUN_TEST(test_isActiveState_activeStates);
    RUN_TEST(test_isActiveState_inactiveStates);
    
    // cancelCurrentOperation tests
    RUN_TEST(test_cancelCurrentOperation_fromActiveState);
    RUN_TEST(test_cancelCurrentOperation_fromIdleState);
    
    // Error state tests
    RUN_TEST(test_setError_transitionsToError);
    RUN_TEST(test_error_canTransitionToIdle);
    
    // Callback tests
    RUN_TEST(test_onStateChange_callbackInvoked);
    RUN_TEST(test_onStateChange_notCalledOnInvalidTransition);
    
    // canTransitionTo tests
    RUN_TEST(test_canTransitionTo_valid);
    RUN_TEST(test_canTransitionTo_invalid);
    
    // Reset tests
    RUN_TEST(test_reset_returnsToBootState);
    
    return UNITY_END();
}
