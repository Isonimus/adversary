/**
 * @file footer_hints.h
 * @brief Unified footer hints component with interactive navigation
 * 
 * Provides consistent action hints across all screens with:
 * - Platform-aware formatting (Cardputer: "K:Label", M5Stick: "Label")
 * - Disabled state (grayed out, skipped during navigation)
 * - Interactive navigation (TAB focus, arrows to select, Enter to trigger)
 */

#pragma once

#include <cstdint>
#include <vector>
#include <initializer_list>
#include "../theme.h"

namespace adversary {
namespace ui {

/**
 * @brief Single footer hint item
 */
struct FooterHint {
    char key;           ///< Keyboard shortcut ('I', 'U', etc.)
    const char* label;  ///< Display label ("Info", "Upload", etc.)
    bool enabled;       ///< false = grayed out and skipped during nav
    
    FooterHint(char k, const char* l, bool e = true) 
        : key(k), label(l), enabled(e) {}
};

/**
 * @brief Footer height in pixels
 */
constexpr int16_t FOOTER_HEIGHT = 16;

/**
 * @brief Interactive footer hints component
 * 
 * Navigation:
 * - SPACE: Toggle focus between list and footer
 * - ,/;: Previous enabled hint (when focused)
 * - ./: Next enabled hint (when focused)
 * - Enter: Trigger selected action
 */
class FooterHints {
public:
    FooterHints() = default;
    
    /**
     * @brief Set the available hints for this screen state
     * Preserves current selection if still valid
     */
    void setHints(std::initializer_list<FooterHint> hints) {
        // Save current selection
        int prevIndex = selectedIndex_;
        char prevKey = (prevIndex >= 0 && prevIndex < (int)hints_.size()) 
                       ? hints_[prevIndex].key : 0;
        
        hints_.clear();
        hints_.reserve(hints.size());
        for (const auto& h : hints) {
            hints_.push_back(h);
        }
        
        // Try to restore selection by key match
        selectedIndex_ = -1;
        if (prevKey != 0) {
            for (size_t i = 0; i < hints_.size(); i++) {
                if (hints_[i].key == prevKey && hints_[i].enabled) {
                    selectedIndex_ = static_cast<int>(i);
                    break;
                }
            }
        }
        
        // If not found, select first enabled
        if (selectedIndex_ < 0) {
            selectedIndex_ = findNextEnabled(-1);
        }
    }
    
    /**
     * @brief Clear all hints
     */
    void clear() {
        hints_.clear();
        selectedIndex_ = -1;
        hasFocus_ = false;
    }
    
    /**
     * @brief Check if footer has input focus
     */
    bool hasFocus() const { return hasFocus_; }
    
    /**
     * @brief Set footer focus state
     */
    void setFocus(bool focus) { 
        hasFocus_ = focus;
        if (focus && selectedIndex_ < 0) {
            selectedIndex_ = findNextEnabled(-1);
        }
    }
    
    /**
     * @brief Toggle focus state
     * @return New focus state
     */
    bool toggleFocus() {
        setFocus(!hasFocus_);
        return hasFocus_;
    }
    
    /**
     * @brief Handle input key
     * @return true if key was handled
     */
    bool handleInput(char key) {
        if (hints_.empty()) return false;
        
        // SPACE toggles focus
        if (key == ' ') {
            toggleFocus();
            return true;
        }
        
        // If no focus, only handle direct key presses
        if (!hasFocus_) {
            // Check for direct key press (e.g., 'i' for Info)
            for (size_t i = 0; i < hints_.size(); i++) {
                const auto& hint = hints_[i];
                if (hint.enabled && 
                    (key == hint.key || key == (hint.key + 32) || key == (hint.key - 32))) {
                    // Trigger action directly
                    selectedIndex_ = static_cast<int>(i);
                    return false; // Let caller handle the action
                }
            }
            return false;
        }
        
        // Navigation when focused
        switch (key) {
            case ',':  // Left - previous
            case ';':
                navigatePrev();
                return true;
                
            case '/':  // Right - next
            case '.':
                navigateNext();
                return true;
                
            case '\n':  // Enter - trigger selected
            case '\r':
                // Selection made, caller should check getSelectedAction()
                return true;
                
            default:
                break;
        }
        
        return false;
    }
    
    /**
     * @brief Get the action key of currently selected hint
     * @return Key char or 0 if no selection
     */
    char getSelectedAction() const {
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(hints_.size())) {
            return hints_[selectedIndex_].key;
        }
        return 0;
    }
    
    /**
     * @brief Handle input with automatic dispatch logic
     * 
     * Encapsulates the common footer input pattern:
     * - Space toggles focus between screen content and footer
     * - When focused: ;/. cycles through hints, Enter dispatches
     * - Returns action to dispatch (if any) for screen to handle
     * 
     * @param key Input key
     * @param outAction Set to action key to dispatch (0 if none)
     * @return true if input was consumed (caller should return true)
     * 
     * Usage in screen handleInput():
     * @code
     *   char action = 0;
     *   if (footerHints_.handleInputWithDispatch(key, action)) {
     *       if (action) return handleInput(action);  // Dispatch action recursively
     *       return true;  // Consumed (navigation/toggle)
     *   }
     *   // Continue with normal screen input handling...
     * @endcode
     */
    bool handleInputWithDispatch(char key, char& outAction) {
        outAction = 0;
        
        // Space toggles focus between screen content and footer
        if (key == ' ') {
            toggleFocus();
            return true;
        }
        
        // When footer has focus, handle navigation and dispatch
        if (hasFocus_) {
            switch (key) {
                case ',':  // Left - previous
                case ';':
                    navigatePrev();
                    return true;
                    
                case '/':  // Right - next
                case '.':
                    navigateNext();
                    return true;
                    
                case '\n':  // Enter - dispatch selected action
                case '\r':
                    outAction = getSelectedAction();
                    setFocus(false);  // Return focus to screen content
                    return true;
                    
                default:
                    break;
            }
        }
        
        return false;
    }
    
    /**
     * @brief Set right-side content (e.g., "1/10")
     */
    void setRightContent(const char* content) {
        if (content) {
            strncpy(rightContent_, content, sizeof(rightContent_) - 1);
            rightContent_[sizeof(rightContent_) - 1] = '\0';
        } else {
            rightContent_[0] = '\0';
        }
    }
    
    /**
     * @brief Render the footer with hints left-aligned and optional right content
     */
    template<typename Canvas>
    void render(Canvas& canvas) {
        int16_t screenWidth = canvas.width();
        int16_t screenHeight = canvas.height();
        int16_t y = screenHeight - FOOTER_HEIGHT;
        
        // Background
        canvas.fillRect(0, y, screenWidth, FOOTER_HEIGHT, theme::BG_SECONDARY());
        
        // Left-aligned hints starting at x=4
        int16_t x = 4;
        
        canvas.setTextSize(1);
        
        for (size_t i = 0; i < hints_.size(); i++) {
            const auto& hint = hints_[i];
            int16_t hintWidth = getHintWidth(hint);
            
            bool isSelected = hasFocus_ && static_cast<int>(i) == selectedIndex_;
            
            // Draw selection badge background
            if (isSelected) {
                canvas.fillRoundRect(x - 2, y + 2, hintWidth + 4, FOOTER_HEIGHT - 4, 
                                     2, theme::ACCENT());
            }
            
            // Choose text color
            uint16_t textColor;
            if (!hint.enabled) {
                textColor = theme::TEXT_DISABLED();
            } else if (isSelected) {
                textColor = theme::BG_PRIMARY();  // Contrast with accent
            } else {
                textColor = theme::TEXT_SECONDARY();
            }
            
            canvas.setTextColor(textColor);
            canvas.setCursor(x, y + 4);
            
            // Platform-specific formatting
#if defined(TARGET_M5STICK)
            // M5Stick: Label only (no keyboard)
            canvas.print(hint.label);
#else
            // Cardputer: Key:Label format
            canvas.print(getKeyLabel(hint.key));
            canvas.print(':');
            canvas.print(hint.label);
#endif
            
            x += hintWidth + 8;
        }
        
        // Right-side content (e.g., item count "1/10")
        if (rightContent_[0] != '\0') {
            int16_t rightWidth = strlen(rightContent_) * 6;
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(screenWidth - rightWidth - 4, y + 4);
            canvas.print(rightContent_);
        }
    }
    
private:
    std::vector<FooterHint> hints_;
    int selectedIndex_ = -1;
    bool hasFocus_ = false;
    char rightContent_[16] = {0};  // Right-side content (e.g., "1/10")
    
    /**
     * @brief Calculate pixel width of a hint
     */
    int16_t getHintWidth(const FooterHint& hint) const {
#if defined(TARGET_M5STICK)
        // Label only
        return strlen(hint.label) * 6;
#else
        // Key:Label format - use formatted key label length
        return (strlen(getKeyLabel(hint.key)) + 1 + strlen(hint.label)) * 6;
#endif
    }
    
    /**
     * @brief Get display label for a key character
     * @param key Key character
     * @return Human-readable key name
     */
    static const char* getKeyLabel(char key) {
        switch (key) {
            case '\n':
            case '\r': return "Enter";
            case '`': return "Esc";
            case ' ': return "Space";
            case '\t': return "Tab";
            default: {
                // Return uppercase single character as static buffer
                static char buf[2] = {0, 0};
                buf[0] = (key >= 'a' && key <= 'z') ? (key - 32) : key;
                return buf;
            }
        }
    }
    
    /**
     * @brief Find next enabled hint index
     * @param from Start searching after this index (-1 for start)
     * @return Index of next enabled hint, or -1 if none
     */
    int findNextEnabled(int from) const {
        for (size_t i = from + 1; i < hints_.size(); i++) {
            if (hints_[i].enabled) return static_cast<int>(i);
        }
        return -1;
    }
    
    /**
     * @brief Find previous enabled hint index
     * @param from Start searching before this index
     * @return Index of previous enabled hint, or -1 if none
     */
    int findPrevEnabled(int from) const {
        for (int i = from - 1; i >= 0; i--) {
            if (hints_[i].enabled) return i;
        }
        return -1;
    }
    
    /**
     * @brief Navigate to next enabled hint (wraps around)
     */
    void navigateNext() {
        if (hints_.empty()) return;
        
        int next = findNextEnabled(selectedIndex_);
        if (next < 0) {
            // Wrap to beginning
            next = findNextEnabled(-1);
        }
        if (next >= 0) {
            selectedIndex_ = next;
        }
    }
    
    /**
     * @brief Navigate to previous enabled hint (wraps around)
     */
    void navigatePrev() {
        if (hints_.empty()) return;
        
        int prev = findPrevEnabled(selectedIndex_);
        if (prev < 0) {
            // Wrap to end - find last enabled
            prev = findPrevEnabled(static_cast<int>(hints_.size()));
        }
        if (prev >= 0) {
            selectedIndex_ = prev;
        }
    }
};

} // namespace ui
} // namespace adversary
