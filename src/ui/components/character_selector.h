/**
 * @file character_selector.h
 * @brief Scrolling character selector for text input on button-only devices
 * 
 * Provides a character wheel interface for M5Stick and other devices
 * without keyboards. Users scroll through characters and select them
 * to build text input.
 */

#pragma once

#include <cstdint>
#include <cstring>

#ifdef ESP32
#include <Arduino.h>
#endif

#include "../theme.h"
#include "../../hal/input/input_manager.h"

namespace adversary {
namespace ui {

/**
 * @brief Character selector modes
 */
enum class CharSelectorMode : uint8_t {
    LOWERCASE,      ///< a-z
    UPPERCASE,      ///< A-Z
    NUMBERS,        ///< 0-9
    SYMBOLS,        ///< Common symbols
};

/**
 * @brief Scrolling character selector for text input
 * 
 * Designed for devices with only 2 buttons (like M5StickC Plus 2).
 * User scrolls through character sets and selects characters to build input.
 */
class CharacterSelector {
public:
    // Character sets (including hyphen and underscore as requested)
    static constexpr const char* CHARS_LOWER = "abcdefghijklmnopqrstuvwxyz";
    static constexpr const char* CHARS_UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr const char* CHARS_NUMBERS = "0123456789";
    static constexpr const char* CHARS_SYMBOLS = "!@#$%^&*()-_=+[]{}|;:',.<>/?`~\\\" ";
    
    static constexpr size_t MAX_INPUT_LENGTH = 64;
    
    CharacterSelector() 
        : mode_(CharSelectorMode::LOWERCASE)
        , charIndex_(0)
        , inputLength_(0)
        , visible_(false)
        , needsRedraw_(true) {
        input_[0] = '\0';
        updateCharset();
    }
    
    /**
     * @brief Show the selector with optional initial text
     */
    void show(const char* title = "Enter Text", const char* initialValue = "") {
        strncpy(title_, title, sizeof(title_) - 1);
        title_[sizeof(title_) - 1] = '\0';
        
        strncpy(input_, initialValue, MAX_INPUT_LENGTH - 1);
        input_[MAX_INPUT_LENGTH - 1] = '\0';
        inputLength_ = strlen(input_);
        
        mode_ = CharSelectorMode::LOWERCASE;
        charIndex_ = 0;
        visible_ = true;
        confirmed_ = false;
        cancelled_ = false;
        needsRedraw_ = true;
        updateCharset();
    }
    
    /**
     * @brief Hide the selector
     */
    void hide() {
        visible_ = false;
    }
    
    bool isVisible() const { return visible_; }
    bool isConfirmed() const { return confirmed_; }
    bool isCancelled() const { return cancelled_; }
    
    /**
     * @brief Get the current input buffer
     */
    const char* getInput() const { return input_; }
    
    /**
     * @brief Handle input action
     * @return true if action was handled
     */
    bool handleAction(InputAction action) {
        if (!visible_) return false;
        
        needsRedraw_ = true;
        
        switch (action) {
            case InputAction::UP:
                // Previous character in current charset
                if (charIndex_ > 0) {
                    charIndex_--;
                } else {
                    charIndex_ = strlen(currentCharset_) - 1;
                }
                return true;
                
            case InputAction::DOWN:
                // Next character in current charset
                charIndex_++;
                if (charIndex_ >= strlen(currentCharset_)) {
                    charIndex_ = 0;
                }
                return true;
                
            case InputAction::LEFT:
                // Delete last character
                if (inputLength_ > 0) {
                    inputLength_--;
                    input_[inputLength_] = '\0';
                }
                return true;
                
            case InputAction::RIGHT:
                // Switch to next character mode
                cycleMode();
                return true;
                
            case InputAction::SELECT:
                // Add current character to input
                if (inputLength_ < MAX_INPUT_LENGTH - 1) {
                    input_[inputLength_] = currentCharset_[charIndex_];
                    inputLength_++;
                    input_[inputLength_] = '\0';
                }
                return true;
                
            case InputAction::BACK:
                // Cancel input
                cancelled_ = true;
                visible_ = false;
                return true;
                
            case InputAction::MENU:
                // Confirm input
                confirmed_ = true;
                visible_ = false;
                return true;
                
            default:
                return false;
        }
    }
    
    /**
     * @brief Render the character selector
     */
    template<typename Canvas>
    void render(Canvas& canvas) {
        if (!visible_) return;
        
        const int16_t screenWidth = canvas.width();
        const int16_t screenHeight = canvas.height();
        
        // Semi-transparent overlay effect (draw dark rectangle)
        canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
        
        // Title bar
        canvas.fillRect(0, 0, screenWidth, 20, theme::BG_SECONDARY());
        canvas.setTextColor(theme::ACCENT());
        canvas.setTextSize(1);
        canvas.drawString(title_, 5, 5);
        
        // Current input (with cursor)
        int16_t inputY = 30;
        canvas.fillRect(5, inputY, screenWidth - 10, 20, theme::BG_SECONDARY());
        canvas.setTextColor(theme::TEXT_PRIMARY());
        
        // Show input with blinking cursor
        char displayBuf[MAX_INPUT_LENGTH + 2];
        snprintf(displayBuf, sizeof(displayBuf), "%s_", input_);
        canvas.drawString(displayBuf, 10, inputY + 5);
        
        // Character wheel display
        int16_t wheelY = 60;
        int16_t wheelCenterX = screenWidth / 2;
        
        // Show 5 characters centered on current selection
        canvas.setTextSize(2);
        size_t charsetLen = strlen(currentCharset_);
        
        for (int i = -2; i <= 2; i++) {
            int idx = (charIndex_ + i + charsetLen) % charsetLen;
            char c = currentCharset_[idx];
            
            int16_t x = wheelCenterX + (i * 25);
            
            if (i == 0) {
                // Highlight current character
                canvas.fillRect(x - 12, wheelY - 2, 24, 24, theme::ACCENT());
                canvas.setTextColor(theme::BG_PRIMARY());
            } else {
                canvas.setTextColor(theme::TEXT_SECONDARY());
            }
            
            char ch[2] = {c, '\0'};
            canvas.drawString(ch, x - 6, wheelY + 2);
        }
        
        // Mode indicator
        int16_t modeY = 95;
        canvas.setTextSize(1);
        canvas.setTextColor(theme::TEXT_SECONDARY());
        
        const char* modeStr = "?";
        switch (mode_) {
            case CharSelectorMode::LOWERCASE: modeStr = "[abc]"; break;
            case CharSelectorMode::UPPERCASE: modeStr = "[ABC]"; break;
            case CharSelectorMode::NUMBERS:   modeStr = "[123]"; break;
            case CharSelectorMode::SYMBOLS:   modeStr = "[!@#]"; break;
        }
        canvas.drawString(modeStr, 5, modeY);
        
        // Help text
        int16_t helpY = screenHeight - 15;
        canvas.setTextColor(theme::TEXT_SECONDARY());
        
#if defined(TARGET_M5STICK)
        canvas.drawString("A:Prev B:Next  A+B:OK", 5, helpY);
#else
        canvas.drawString("/':Scroll Enter:Add Esc:Cancel", 5, helpY);
#endif
        
        needsRedraw_ = false;
    }
    
    bool needsRedraw() const { return needsRedraw_; }
    void setNeedsRedraw() { needsRedraw_ = true; }

private:
    void updateCharset() {
        switch (mode_) {
            case CharSelectorMode::LOWERCASE:
                currentCharset_ = CHARS_LOWER;
                break;
            case CharSelectorMode::UPPERCASE:
                currentCharset_ = CHARS_UPPER;
                break;
            case CharSelectorMode::NUMBERS:
                currentCharset_ = CHARS_NUMBERS;
                break;
            case CharSelectorMode::SYMBOLS:
                currentCharset_ = CHARS_SYMBOLS;
                break;
        }
        
        // Reset index if out of bounds
        if (charIndex_ >= strlen(currentCharset_)) {
            charIndex_ = 0;
        }
    }
    
    void cycleMode() {
        switch (mode_) {
            case CharSelectorMode::LOWERCASE:
                mode_ = CharSelectorMode::UPPERCASE;
                break;
            case CharSelectorMode::UPPERCASE:
                mode_ = CharSelectorMode::NUMBERS;
                break;
            case CharSelectorMode::NUMBERS:
                mode_ = CharSelectorMode::SYMBOLS;
                break;
            case CharSelectorMode::SYMBOLS:
                mode_ = CharSelectorMode::LOWERCASE;
                break;
        }
        updateCharset();
    }
    
    CharSelectorMode mode_;
    const char* currentCharset_;
    size_t charIndex_;
    
    char title_[32];
    char input_[MAX_INPUT_LENGTH];
    size_t inputLength_;
    
    bool visible_;
    bool confirmed_;
    bool cancelled_;
    bool needsRedraw_;
};

} // namespace ui
} // namespace adversary
