/**
 * @file action_menu.cpp
 * @brief Popup action menu implementation
 */

#include "action_menu.h"

namespace adversary {
namespace ui {

ActionMenu::ActionMenu()
    : title_("Select Action")
    , subtitle_(nullptr)
    , callback_(nullptr)
    , dismissCallback_(nullptr)
    , selectedIndex_(0)
    , visible_(false)
{
}

void ActionMenu::addItem(char shortcut, const char* label, bool enabled) {
    items_.emplace_back(shortcut, label, enabled);
}

void ActionMenu::show() {
    visible_ = true;
    selectedIndex_ = 0;
    
    // Find first enabled item
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].enabled) {
            selectedIndex_ = static_cast<int>(i);
            break;
        }
    }
}

void ActionMenu::hide() {
    visible_ = false;
    if (dismissCallback_) {
        dismissCallback_();
    }
}

bool ActionMenu::handleInput(char key) {
    if (!visible_) return false;
    
    // ESC / backtick to dismiss
    if (key == '`' || key == 27) {  // 27 = ESC
        hide();
        return true;
    }
    
    // Navigation with ; and .
    if (key == ';') {
        navigateUp();
        return true;
    }
    if (key == '.') {
        navigateDown();
        return true;
    }
    
    // Enter to select current
    if (key == '\n' || key == '\r') {
        selectCurrent();
        return true;
    }
    
    // Check for shortcut keys
    char upperKey = (key >= 'a' && key <= 'z') ? (key - 32) : key;
    for (const auto& item : items_) {
        char upperShortcut = (item.shortcut >= 'a' && item.shortcut <= 'z') 
                            ? (item.shortcut - 32) : item.shortcut;
        if (upperKey == upperShortcut && item.enabled) {
            visible_ = false;
            if (callback_) {
                callback_(item.shortcut);
            }
            return true;
        }
    }
    
    return false;
}

void ActionMenu::navigateUp() {
    if (items_.empty()) return;
    
    int startIndex = selectedIndex_;
    do {
        selectedIndex_--;
        if (selectedIndex_ < 0) {
            selectedIndex_ = static_cast<int>(items_.size()) - 1;
        }
        // Found an enabled item or wrapped around
        if (items_[selectedIndex_].enabled || selectedIndex_ == startIndex) {
            break;
        }
    } while (selectedIndex_ != startIndex);
}

void ActionMenu::navigateDown() {
    if (items_.empty()) return;
    
    int startIndex = selectedIndex_;
    do {
        selectedIndex_++;
        if (selectedIndex_ >= static_cast<int>(items_.size())) {
            selectedIndex_ = 0;
        }
        // Found an enabled item or wrapped around
        if (items_[selectedIndex_].enabled || selectedIndex_ == startIndex) {
            break;
        }
    } while (selectedIndex_ != startIndex);
}

void ActionMenu::selectCurrent() {
    if (items_.empty()) return;
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(items_.size())) return;
    
    const auto& item = items_[selectedIndex_];
    if (item.enabled) {
        visible_ = false;
        if (callback_) {
            callback_(item.shortcut);
        }
    }
}

} // namespace ui
} // namespace adversary
