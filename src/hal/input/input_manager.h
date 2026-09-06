/**
 * @file input_manager.h
 * @brief Hardware Abstraction Layer for input devices
 * 
 * Provides unified input handling across M5Cardputer (keyboard) 
 * and M5StickC Plus 2 (buttons with gestures).
 */

#pragma once

#include <cstdint>

#ifdef ESP32
#include <Arduino.h>
#endif

#if defined(TARGET_CARDPUTER)
#include <M5Cardputer.h>
#elif defined(TARGET_M5STICK)
#include <M5StickCPlus2.h>
#else
#include <M5Unified.h>
#endif

namespace adversary {

// =============================================================================
// Input Actions - Platform-agnostic commands
// =============================================================================

/**
 * @brief Logical input actions abstracted from hardware
 */
enum class InputAction : uint8_t {
    NONE = 0,
    
    // Navigation
    UP,         ///< Navigate up in list/menu
    DOWN,       ///< Navigate down in list/menu
    LEFT,       ///< Navigate left or decrease value
    RIGHT,      ///< Navigate right or increase value
    
    // Primary actions
    SELECT,     ///< Confirm/enter/select item
    BACK,       ///< Cancel/escape/go back
    MENU,       ///< Open context menu
    
    // Quick actions (Cardputer letter shortcuts)
    ACTION_A,   ///< Letter 'a' or action 1
    ACTION_B,   ///< Letter 'b' or action 2
    ACTION_C,   ///< Letter 'c' or action 3
    ACTION_D,   ///< Letter 'd' or action 4
    ACTION_S,   ///< Letter 's' - scan/start
    ACTION_W,   ///< Letter 'w' - whitelist
    ACTION_DEL, ///< Delete/backspace
    
    // Raw character (for text input on Cardputer)
    CHAR,       ///< Character input available via getChar()
};

// =============================================================================
// Navigation Context - Affects button behavior on M5Stick
// =============================================================================

/**
 * @brief Context for M5Stick button interpretation
 */
enum class NavContext : uint8_t {
    LIST,       ///< Vertical list: A=up, B=down
    VALUE,      ///< Value editing: A=increase, B=decrease
    TEXT,       ///< Text input mode
};

// =============================================================================
// Gesture Timing Constants
// =============================================================================

constexpr uint32_t LONG_PRESS_MS = 400;        ///< Long press threshold
constexpr uint32_t REPEAT_DELAY_MS = 500;      ///< Initial repeat delay
constexpr uint32_t REPEAT_RATE_MS = 100;       ///< Subsequent repeat rate
constexpr uint32_t DOUBLE_PRESS_MS = 300;      ///< Double press window

// =============================================================================
// InputManager Singleton
// =============================================================================

/**
 * @brief Unified input manager for all M5 devices
 * 
 * Abstracts keyboard (Cardputer) and button (M5Stick) input into
 * logical InputAction events with gesture detection.
 */
class InputManager {
public:
    static InputManager& getInstance() {
        static InputManager instance;
        return instance;
    }
    
    // Prevent copying
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    
    /**
     * @brief Update input state - call once per loop iteration
     */
    void update() {
#ifdef ESP32
#if defined(TARGET_CARDPUTER)
        updateKeyboard();
#elif defined(TARGET_M5STICK)
        updateButtons();
#endif
#endif
    }
    
    /**
     * @brief Get current action (consumes it)
     */
    InputAction getAction() {
        InputAction action = currentAction_;
        currentAction_ = InputAction::NONE;
        return action;
    }
    
    /**
     * @brief Peek at current action without consuming
     */
    InputAction peekAction() const { return currentAction_; }
    
    /**
     * @brief Check if any input is active
     */
    bool hasInput() const { return currentAction_ != InputAction::NONE; }
    
    /**
     * @brief Get raw character for text input (Cardputer only)
     */
    char getChar() const { return lastChar_; }
    
    /**
     * @brief Check if raw character is available
     */
    bool hasChar() const { return lastChar_ != '\0'; }
    
    /**
     * @brief Set navigation context (affects M5Stick button behavior)
     */
    void setContext(NavContext ctx) { context_ = ctx; }
    NavContext getContext() const { return context_; }

private:
    InputManager() : currentAction_(InputAction::NONE), lastChar_('\0'),
                     context_(NavContext::LIST) {
#ifdef ESP32
#if defined(TARGET_M5STICK)
        btnAState_ = {false, false, 0, 0};
        btnBState_ = {false, false, 0, 0};
#endif
#endif
    }
    
    InputAction currentAction_;
    char lastChar_;
    NavContext context_;
    
#ifdef ESP32

#if defined(TARGET_CARDPUTER)
    // =========================================================================
    // Cardputer Keyboard Handling
    // =========================================================================
    
    void updateKeyboard() {
        M5Cardputer.update();
        
        currentAction_ = InputAction::NONE;
        lastChar_ = '\0';
        
        if (!M5Cardputer.Keyboard.isChange()) return;
        if (!M5Cardputer.Keyboard.isPressed()) return;
        
        auto state = M5Cardputer.Keyboard.keysState();
        
        // Check special keys first
        if (state.enter) {
            currentAction_ = InputAction::SELECT;
            return;
        }
        
        // Get the pressed key character
        char key = '\0';
        for (int i = 0; i < state.hid_keys.size() && i < 6; i++) {
            uint8_t hid = state.hid_keys[i];
            if (hid == 0) continue;
            
            // Convert HID to ASCII (simplified)
            if (hid >= 4 && hid <= 29) {
                // Letters a-z
                key = 'a' + (hid - 4);
                if (state.shift) {
                    key = 'A' + (hid - 4);
                }
            } else if (hid >= 30 && hid <= 38) {
                // Numbers 1-9
                key = '1' + (hid - 30);
            } else if (hid == 39) {
                key = '0';
            } else if (hid == 40) {
                // Enter (already handled above)
                currentAction_ = InputAction::SELECT;
                return;
            } else if (hid == 41) {
                // Escape
                currentAction_ = InputAction::BACK;
                return;
            } else if (hid == 42) {
                // Backspace - set char for text input, action for navigation
                key = 0x08;  // ASCII backspace
                currentAction_ = InputAction::ACTION_DEL;
            } else if (hid == 43) {
                // Tab - treat as down
                currentAction_ = InputAction::DOWN;
                return;
            } else if (hid == 44) {
                // Space
                key = ' ';
            } else if (hid == 45) {
                key = '-';
            } else if (hid == 46) {
                key = '=';
            } else if (hid == 51) {
                key = ';';  // Semicolon - often used for left/decrease
            } else if (hid == 52) {
                key = '\'';
            } else if (hid == 53) {
                // Backtick - escape
                currentAction_ = InputAction::BACK;
                return;
            } else if (hid == 54) {
                key = ',';
            } else if (hid == 55) {
                key = '.';  // Period - often used for right/increase
            } else if (hid == 56) {
                key = '/';
            } else if (hid == 82) {
                currentAction_ = InputAction::UP;
                return;
            } else if (hid == 81) {
                currentAction_ = InputAction::DOWN;
                return;
            } else if (hid == 80) {
                currentAction_ = InputAction::LEFT;
                return;
            } else if (hid == 79) {
                currentAction_ = InputAction::RIGHT;
                return;
            }
            
            if (key != '\0') break;
        }
        
        if (key == '\0') return;
        
        // Store for text input
        lastChar_ = key;
        
        // Map common keys to actions
        switch (key) {
            case ';':
                currentAction_ = InputAction::LEFT;
                break;
            case '.':
                currentAction_ = InputAction::RIGHT;
                break;
            case '/':
                currentAction_ = InputAction::UP;
                break;
            case '\'':
                currentAction_ = InputAction::DOWN;
                break;
            case 'a':
            case 'A':
                currentAction_ = InputAction::ACTION_A;
                break;
            case 'b':
            case 'B':
                currentAction_ = InputAction::ACTION_B;
                break;
            case 'c':
            case 'C':
                currentAction_ = InputAction::ACTION_C;
                break;
            case 'd':
            case 'D':
                currentAction_ = InputAction::ACTION_D;
                break;
            case 's':
            case 'S':
                currentAction_ = InputAction::ACTION_S;
                break;
            case 'w':
            case 'W':
                currentAction_ = InputAction::ACTION_W;
                break;
            default:
                // Any other character - available via getChar() for text input
                currentAction_ = InputAction::CHAR;
                break;
        }
    }

#elif defined(TARGET_M5STICK)
    // =========================================================================
    // M5Stick Button Handling with Gesture Detection
    // =========================================================================
    
    struct ButtonState {
        bool wasPressed;
        bool isHeld;
        uint32_t pressStart;
        uint32_t lastRepeat;
    };
    
    // M5StickCPlus2 button GPIO pins
    static constexpr int BTN_A_PIN = 37;  // Front button
    static constexpr int BTN_B_PIN = 39;  // Side button
    
    bool lastBtnA_ = false;
    bool lastBtnB_ = false;
    
    ButtonState btnAState_;
    ButtonState btnBState_;
    
    void updateButtons() {
        currentAction_ = InputAction::NONE;
        lastChar_ = '\0';
        
        uint32_t now = millis();
        
        // Read buttons directly from GPIO (LOW = pressed, pull-up)
        bool btnAPressed = (digitalRead(BTN_A_PIN) == LOW);
        bool btnBPressed = (digitalRead(BTN_B_PIN) == LOW);
        
        // Detect press events (was not pressed, now pressed)  
        bool btnAWasPressed = btnAPressed && !lastBtnA_;
        bool btnBWasPressed = btnBPressed && !lastBtnB_;
        
        // Detect release events
        bool btnAWasReleased = !btnAPressed && lastBtnA_;
        bool btnBWasReleased = !btnBPressed && lastBtnB_;
        
        // Update last state
        lastBtnA_ = btnAPressed;
        lastBtnB_ = btnBPressed;
        
        // Debug output for button detection
        if (btnAWasPressed || btnBWasPressed) {
            Serial.printf("[InputMgr] GPIO Btn: A(37)=%d B(39)=%d\n", 
                btnAPressed, btnBPressed);
        }
        
        // Check for simultaneous press (A+B = MENU)
        if (btnAPressed && btnBPressed) {
            if (btnAWasPressed || btnBWasPressed) {
                currentAction_ = InputAction::MENU;
                btnAState_.wasPressed = true;
                btnBState_.wasPressed = true;
            }
            return;
        }
        
        // Process Button A
        if (btnAWasPressed) {
            btnAState_.pressStart = now;
            btnAState_.wasPressed = true;
            btnAState_.isHeld = false;
        }
        
        if (btnAPressed && btnAState_.wasPressed) {
            uint32_t pressDuration = now - btnAState_.pressStart;
            
            if (pressDuration >= LONG_PRESS_MS && !btnAState_.isHeld) {
                // Long press detected
                btnAState_.isHeld = true;
                currentAction_ = InputAction::SELECT;  // A long = SELECT
            } else if (btnAState_.isHeld) {
                // Repeat for held button
                if (now - btnAState_.lastRepeat >= REPEAT_RATE_MS) {
                    btnAState_.lastRepeat = now;
                    // Could add repeat action here
                }
            }
        }
        
        if (btnAWasReleased && btnAState_.wasPressed) {
            uint32_t pressDuration = now - btnAState_.pressStart;
            
            if (!btnAState_.isHeld && pressDuration < LONG_PRESS_MS) {
                // Short press
                switch (context_) {
                    case NavContext::LIST:
                        currentAction_ = InputAction::UP;
                        break;
                    case NavContext::VALUE:
                        currentAction_ = InputAction::RIGHT;  // Increase
                        break;
                    case NavContext::TEXT:
                        currentAction_ = InputAction::UP;
                        break;
                }
            }
            btnAState_.wasPressed = false;
            btnAState_.isHeld = false;
        }
        
        // Process Button B
        if (btnBWasPressed) {
            btnBState_.pressStart = now;
            btnBState_.wasPressed = true;
            btnBState_.isHeld = false;
        }
        
        if (btnBPressed && btnBState_.wasPressed) {
            uint32_t pressDuration = now - btnBState_.pressStart;
            
            if (pressDuration >= LONG_PRESS_MS && !btnBState_.isHeld) {
                // Long press detected
                btnBState_.isHeld = true;
                currentAction_ = InputAction::BACK;  // B long = BACK
            }
        }
        
        if (btnBWasReleased && btnBState_.wasPressed) {
            uint32_t pressDuration = now - btnBState_.pressStart;
            
            if (!btnBState_.isHeld && pressDuration < LONG_PRESS_MS) {
                // Short press
                switch (context_) {
                    case NavContext::LIST:
                        currentAction_ = InputAction::DOWN;
                        break;
                    case NavContext::VALUE:
                        currentAction_ = InputAction::LEFT;  // Decrease
                        break;
                    case NavContext::TEXT:
                        currentAction_ = InputAction::DOWN;
                        break;
                }
            }
            btnBState_.wasPressed = false;
            btnBState_.isHeld = false;
        }
    }

#endif // TARGET_M5STICK

#endif // ESP32
};

// =============================================================================
// Convenience Function
// =============================================================================

/**
 * @brief Get the global InputManager instance
 */
inline InputManager& Input() {
    return InputManager::getInstance();
}

} // namespace adversary
