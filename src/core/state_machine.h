#pragma once

/**
 * @file state_machine.h
 * @brief Application state machine for controlling app flow
 */

#include <functional>

namespace adversary {

/**
 * @brief Application states
 */
enum class AppState {
    BOOT,           // Hardware initialization
    SPLASH,         // Splash screen with progress
    IDLE,           // Main menu, waiting for input
    SCANNING,       // WiFi network scanning
    SNIFFING,       // Packet capture active
    ATTACKING,      // Attack in progress (deauth, beacon, etc.)
    CAPTURING,      // Handshake capture mode
    AP_RUNNING,     // Evil Twin or Karma AP active
    ERROR,          // Error state with message
    SHUTDOWN        // Graceful shutdown
};

/**
 * @brief State transition callback type
 */
using StateCallback = std::function<void(AppState oldState, AppState newState)>;

/**
 * @brief Get string representation of state
 */
const char* stateToString(AppState state);

/**
 * @brief State machine class
 */
class StateMachine {
public:
    /**
     * @brief Get singleton instance
     */
    static StateMachine& getInstance();

    // Delete copy/move constructors
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    /**
     * @brief Get current state
     */
    AppState getState() const { return m_currentState; }

    /**
     * @brief Get previous state
     */
    AppState getPreviousState() const { return m_previousState; }

    /**
     * @brief Transition to a new state
     * @param newState Target state
     * @return true if transition was valid and executed
     */
    bool transitionTo(AppState newState);

    /**
     * @brief Check if transition to state is valid
     */
    bool canTransitionTo(AppState newState) const;

    /**
     * @brief Check if current state is an active operation
     */
    bool isActiveState() const;

    /**
     * @brief Register callback for state transitions
     */
    void onStateChange(StateCallback callback);

    /**
     * @brief Cancel current operation (ESC pressed during active state)
     * @return true if operation was cancelled
     */
    bool cancelCurrentOperation();

    /**
     * @brief Reset to initial state
     */
    void reset();

    /**
     * @brief Set error state with message
     */
    void setError(const char* message);

    /**
     * @brief Get current error message (if in ERROR state)
     */
    const char* getErrorMessage() const { return m_errorMessage; }

private:
    StateMachine();
    ~StateMachine() = default;

    bool isValidTransition(AppState from, AppState to) const;

    AppState m_currentState;
    AppState m_previousState;
    StateCallback m_stateCallback;
    const char* m_errorMessage;
};

} // namespace adversary
