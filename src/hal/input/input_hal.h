#pragma once

/**
 * @file input_hal.h
 * @brief Input Hardware Abstraction Layer interface
 */

#include <stdint.h>

namespace adversary {
namespace hal {

/**
 * @brief Key codes for input events
 */
enum class KeyCode : uint8_t {
    NONE = 0,
    
    // Navigation
    UP,
    DOWN,
    LEFT,
    RIGHT,
    
    // Actions
    ENTER,
    ESC,
    BACK,
    
    // Function keys (Cardputer keyboard)
    FN,
    ALT,
    CTRL,
    SHIFT,
    
    // Special
    POWER,
    
    // Alphanumeric (simplified - can be extended)
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    
    KEY_SPACE,
    KEY_TAB,
    KEY_DELETE
};

/**
 * @brief Input event types
 */
enum class InputEventType : uint8_t {
    NONE,
    KEY_DOWN,
    KEY_UP,
    KEY_REPEAT,
    LONG_PRESS
};

/**
 * @brief Input event structure
 */
struct InputEvent {
    InputEventType type;
    KeyCode key;
    uint32_t timestamp;
    bool hasModifier;
    bool fnPressed;
    bool shiftPressed;
    bool ctrlPressed;
    bool altPressed;
};

/**
 * @brief Input callback type
 */
using InputCallback = void(*)(const InputEvent& event);

/**
 * @brief Input interface - abstract base for all input implementations
 */
class IInput {
public:
    virtual ~IInput() = default;

    /**
     * @brief Initialize input system
     */
    virtual void init() = 0;

    /**
     * @brief Update input state (call in loop)
     */
    virtual void update() = 0;

    /**
     * @brief Check if a key is currently pressed
     */
    virtual bool isKeyPressed(KeyCode key) const = 0;

    /**
     * @brief Check if any key is pressed
     */
    virtual bool anyKeyPressed() const = 0;

    /**
     * @brief Get last input event (polling mode)
     */
    virtual InputEvent getLastEvent() = 0;

    /**
     * @brief Check if there's a pending event
     */
    virtual bool hasEvent() const = 0;

    /**
     * @brief Clear pending events
     */
    virtual void clearEvents() = 0;

    /**
     * @brief Register callback for input events
     */
    virtual void setCallback(InputCallback callback) = 0;

    /**
     * @brief Set key repeat delay
     * @param initialDelay Delay before repeat starts (ms)
     * @param repeatRate Delay between repeats (ms)
     */
    virtual void setRepeatRate(uint32_t initialDelay, uint32_t repeatRate) = 0;

    /**
     * @brief Set long press threshold
     * @param threshold Time in ms to trigger long press
     */
    virtual void setLongPressThreshold(uint32_t threshold) = 0;
};

/**
 * @brief Factory function to create platform-specific input handler
 */
IInput* createInput();

} // namespace hal
} // namespace adversary
