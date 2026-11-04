/**
 * @file action_menu.cpp
 * @brief Popup action menu implementation
 */

#include "action_menu.h"

namespace adversary {
namespace ui {

ActionMenu::ActionMenu()
    : callback_(nullptr)
    , dismissCallback_(nullptr)
    , visible_(false)
{
    copyText(title_, "Select Action");
    copyText(subtitle_, nullptr);
    // The model skips disabled rows for navigation and selection snapping, replacing the
    // hand-rolled enabled-item wrap below. Bound to items_, which outlives the predicate.
    model_.setSelectablePredicate([this](int i) {
        return i >= 0 && i < static_cast<int>(items_.size()) && items_[i].enabled;
    });
}

void ActionMenu::addItem(char shortcut, const char* label, bool enabled) {
    items_.emplace_back(shortcut, label, enabled);
    model_.setItemCount(static_cast<int>(items_.size()));
}

void ActionMenu::show() {
    visible_ = true;
    // Snap to the first enabled item (the predicate moves off a disabled row 0).
    model_.setSelectedIndex(0);
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
    model_.moveUp();  // wraps and skips disabled rows via the selectable predicate
}

void ActionMenu::navigateDown() {
    model_.moveDown();  // wraps and skips disabled rows via the selectable predicate
}

void ActionMenu::selectCurrent() {
    const int sel = model_.selectedIndex();
    if (sel < 0 || sel >= static_cast<int>(items_.size())) return;

    const auto& item = items_[static_cast<size_t>(sel)];
    if (item.enabled) {
        visible_ = false;
        if (callback_) {
            callback_(item.shortcut);
        }
    }
}

} // namespace ui
} // namespace adversary
