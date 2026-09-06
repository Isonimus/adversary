/**
 * @file state_machine.cpp
 * @brief Application state machine implementation
 */

#include "state_machine.h"

namespace adversary {

const char* stateToString(AppState state) {
    switch (state) {
        case AppState::BOOT:       return "BOOT";
        case AppState::SPLASH:     return "SPLASH";
        case AppState::IDLE:       return "IDLE";
        case AppState::SCANNING:   return "SCANNING";
        case AppState::SNIFFING:   return "SNIFFING";
        case AppState::ATTACKING:  return "ATTACKING";
        case AppState::CAPTURING:  return "CAPTURING";
        case AppState::AP_RUNNING: return "AP_RUNNING";
        case AppState::ERROR:      return "ERROR";
        case AppState::SHUTDOWN:   return "SHUTDOWN";
        default:                   return "UNKNOWN";
    }
}

StateMachine& StateMachine::getInstance() {
    static StateMachine instance;
    return instance;
}

StateMachine::StateMachine()
    : m_currentState(AppState::BOOT)
    , m_previousState(AppState::BOOT)
    , m_stateCallback(nullptr)
    , m_errorMessage(nullptr)
{
}

bool StateMachine::transitionTo(AppState newState) {
    if (!isValidTransition(m_currentState, newState)) {
        return false;
    }

    AppState oldState = m_currentState;
    m_previousState = m_currentState;
    m_currentState = newState;

    // Clear error message if leaving error state
    if (oldState == AppState::ERROR) {
        m_errorMessage = nullptr;
    }

    // Invoke callback if registered
    if (m_stateCallback) {
        m_stateCallback(oldState, newState);
    }

    return true;
}

bool StateMachine::canTransitionTo(AppState newState) const {
    return isValidTransition(m_currentState, newState);
}

bool StateMachine::isActiveState() const {
    switch (m_currentState) {
        case AppState::SCANNING:
        case AppState::SNIFFING:
        case AppState::ATTACKING:
        case AppState::CAPTURING:
        case AppState::AP_RUNNING:
            return true;
        default:
            return false;
    }
}

void StateMachine::onStateChange(StateCallback callback) {
    m_stateCallback = callback;
}

bool StateMachine::cancelCurrentOperation() {
    if (!isActiveState()) {
        return false;
    }
    return transitionTo(AppState::IDLE);
}

void StateMachine::reset() {
    m_currentState = AppState::BOOT;
    m_previousState = AppState::BOOT;
    m_errorMessage = nullptr;
}

void StateMachine::setError(const char* message) {
    m_errorMessage = message;
    transitionTo(AppState::ERROR);
}

bool StateMachine::isValidTransition(AppState from, AppState to) const {
    // Define valid state transitions
    switch (from) {
        case AppState::BOOT:
            return to == AppState::SPLASH || to == AppState::ERROR;
            
        case AppState::SPLASH:
            return to == AppState::IDLE || to == AppState::ERROR;
            
        case AppState::IDLE:
            return to == AppState::SCANNING ||
                   to == AppState::SNIFFING ||
                   to == AppState::ATTACKING ||
                   to == AppState::CAPTURING ||
                   to == AppState::AP_RUNNING ||
                   to == AppState::SHUTDOWN ||
                   to == AppState::ERROR;
                   
        case AppState::SCANNING:
        case AppState::SNIFFING:
        case AppState::ATTACKING:
        case AppState::CAPTURING:
        case AppState::AP_RUNNING:
            // Active states can go back to IDLE (cancelled) or to ERROR
            return to == AppState::IDLE || to == AppState::ERROR;
            
        case AppState::ERROR:
            // From error, can retry (go to IDLE) or shutdown
            return to == AppState::IDLE || to == AppState::SHUTDOWN;
            
        case AppState::SHUTDOWN:
            // No transitions from shutdown
            return false;
            
        default:
            return false;
    }
}

} // namespace adversary
