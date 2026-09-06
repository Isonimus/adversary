/**
 * @file text_input_popup.h
 * @brief Modal popup for text/password input
 * 
 * Uses keyboard input on Cardputer and CharacterSelector on M5Stick.
 */

#pragma once

#include "ui/theme.h"
#include "character_selector.h"
#include "../../hal/input/input_manager.h"
#include <cstdint>
#include <cstring>
#include <functional>

namespace adversary {

/**
 * @brief Text Input Popup
 * 
 * Displays a modal dialog with a title and text input field.
 * Supports masked input for passwords.
 * On M5Stick, uses CharacterSelector for character-by-character input.
 */
class TextInputPopup {
public:
    using SubmitCallback = std::function<void(const char* text)>;
    using CancelCallback = std::function<void()>;
    
    TextInputPopup() : visible_(false), masked_(false), maxLength_(32) {
        buffer_[0] = '\0';
    }
    
    /**
     * @brief Show the popup
     * @param title Title text
     * @param initialValue Initial text buffer content
     * @param isPassword If true, masks characters with '*'
     * @param maxLen Maximum characters (max 64)
     */
    void show(const char* title, const char* initialValue = "", bool isPassword = false, uint8_t maxLen = 32) {
        strncpy(title_, title, 31);
        title_[31] = '\0';
        
        maxLength_ = (maxLen > 129) ? 129 : maxLen;
        
        // Safe copy with length clamping
        strncpy(buffer_, initialValue, maxLength_);
        buffer_[maxLength_] = '\0';
        
        visible_ = true;
        masked_ = isPassword;
        cursorPos_ = strlen(buffer_);
        
#if defined(TARGET_M5STICK)
        // On M5Stick, show the CharacterSelector instead
        charSelector_.show(title, initialValue);
#endif
    }
    
    void hide() {
        visible_ = false;
#if defined(TARGET_M5STICK)
        charSelector_.hide();
#endif
    }
    
    bool isVisible() const { return visible_; }
    
    void setOnSubmit(SubmitCallback callback) { onSubmit_ = callback; }
    void setOnCancel(CancelCallback callback) { onCancel_ = callback; }
    
    /**
     * @brief Handle InputAction (for M5Stick)
     * @return true if input was handled
     */
    bool handleAction(InputAction action) {
        if (!visible_) return false;
        
#if defined(TARGET_M5STICK)
        // Route to CharacterSelector
        bool handled = charSelector_.handleAction(action);
        
        // Check for completion
        if (charSelector_.isConfirmed()) {
            strncpy(buffer_, charSelector_.getInput(), 129);
            buffer_[129] = '\0';
            if (onSubmit_) onSubmit_(buffer_);
            hide();
        } else if (charSelector_.isCancelled()) {
            if (onCancel_) onCancel_();
            hide();
        }
        
        return handled;
#else
        (void)action;
        return false;
#endif
    }
    
    /**
     * @brief Handle character input (for Cardputer keyboard)
     * @return true if input was handled
     */
    bool handleInput(char key) {
        if (!visible_) return false;
        
#if defined(TARGET_CARDPUTER)
        if (key == '\r' || key == '\n') {
            if (onSubmit_) onSubmit_(buffer_);
            hide();
            return true;
        }
        
        if (key == 27 || key == '`') { // ESC or Backtick
            if (onCancel_) onCancel_();
            hide();
            return true;
        }
        
        if (key == 0x08 || key == 0x7F) { // Backspace
            if (cursorPos_ > 0) {
                cursorPos_--;
                buffer_[cursorPos_] = '\0';
            }
            return true;
        }
        
        // Printable characters
        if (key >= 32 && key <= 126) {
            if (cursorPos_ < maxLength_) {
                buffer_[cursorPos_++] = key;
                buffer_[cursorPos_] = '\0';
            }
            return true;
        }
#else
        (void)key;
#endif
        
        return false;
    }
    
    template<typename Canvas>
    void render(Canvas& canvas) {
        if (!visible_) return;
        
#if defined(TARGET_M5STICK)
        // On M5Stick, render the CharacterSelector
        charSelector_.render(canvas);
#else
        // On Cardputer (with keyboard), render the traditional text input
        int16_t screenWidth = canvas.width();
        int16_t screenHeight = canvas.height();
        
        // Dimensions
        int16_t boxWidth = screenWidth - 40;
        int16_t boxHeight = 80;
        int16_t x = (screenWidth - boxWidth) / 2;
        int16_t y = (screenHeight - boxHeight) / 2;
        
        // Draw overlay
        canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
        
        // Draw Box
        canvas.fillRoundRect(x, y, boxWidth, boxHeight, 4, theme::BG_SECONDARY());
        canvas.drawRoundRect(x, y, boxWidth, boxHeight, 4, theme::ACCENT());
        
        // Title
        canvas.setTextColor(theme::ACCENT());
        canvas.setTextSize(1);
        int16_t titleWidth = strlen(title_) * 6;
        canvas.setCursor(x + (boxWidth - titleWidth) / 2, y + 10);
        canvas.print(title_);
        
        // Input Field
        int16_t fieldY = y + 35;
        canvas.drawRect(x + 10, fieldY, boxWidth - 20, 20, theme::TEXT_SECONDARY());
        
        // Text (with horizontal scroll for long strings)
        canvas.setTextColor(theme::TEXT_PRIMARY());
        
        int16_t fieldInnerWidth = boxWidth - 28; // padding on each side
        int maxVisibleChars = fieldInnerWidth / 6; // 6px per char at textSize 1
        
        if (masked_) {
            canvas.setCursor(x + 14, fieldY + 6);
            size_t len = strlen(buffer_);
            size_t showLen = (len > (size_t)maxVisibleChars) ? maxVisibleChars : len;
            for (size_t i = 0; i < showLen; i++) {
                canvas.print('*');
            }
        } else {
            size_t len = strlen(buffer_);
            if ((int)len <= maxVisibleChars - 1) {
                // Short string: show all
                canvas.setCursor(x + 14, fieldY + 6);
                canvas.print(buffer_);
            } else {
                // Long string: show last N visible chars (scroll to cursor)
                int startIdx = (int)len - (maxVisibleChars - 1);
                if (startIdx < 0) startIdx = 0;
                canvas.setCursor(x + 14, fieldY + 6);
                canvas.print(&buffer_[startIdx]);
            }
        }
        
        // Cursor blink
        if ((millis() / 500) % 2 == 0) {
            canvas.print('_');
        }
        
        // Footer hint
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x + 10, y + boxHeight - 15);
        canvas.print("Enter:Ok  ESC:Cancel");
#endif
    }

private:
    char title_[32];
    char buffer_[130];
    bool visible_;
    bool masked_;
    uint8_t maxLength_;
    size_t cursorPos_;
    
    SubmitCallback onSubmit_;
    CancelCallback onCancel_;
    
#if defined(TARGET_M5STICK)
    ui::CharacterSelector charSelector_;
#endif
};

} // namespace adversary
