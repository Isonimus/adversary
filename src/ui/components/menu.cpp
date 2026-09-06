/**
 * @file menu.cpp
 * @brief Hierarchical menu system implementation
 */

#include "menu.h"

#ifdef ESP32
#include <M5Unified.h>
#include "../theme.h"
#include "../../config/config.h"
#endif

namespace adversary {

Menu::Menu()
    : title_("MENU")
    , rootTitle_("MENU")
    , selection_(0)
{
}

void Menu::setItems(const std::vector<MenuItem>& items) {
    rootItems_ = items;
    rootTitle_ = title_;
    menuStack_.clear();
    selection_ = 0;
    ensureValidSelection();
}

bool Menu::handleInput(char key) {
    // Navigation
    switch (key) {
        case ';':  // Up (Cardputer)
        case 'w':
        case 'W':
            navigateUp();
            return true;
            
        case '.':  // Down (Cardputer)
        case 's':
        case 'S':
            navigateDown();
            return true;
            
        case '\n':
        case '\r':
        case 'd':
        case 'D':
            selectCurrent();
            return true;
            
        case '`':  // Back (Cardputer ESC)
            return goBack();
            
        default:
            break;
    }
    
    // Number shortcuts (1-9)
    if (key >= '1' && key <= '9') {
        int index = key - '1';
        const auto& items = getCurrentItems();
        
        // Find the nth selectable item
        int selectableCount = 0;
        for (size_t i = 0; i < items.size(); i++) {
            if (isSelectable(items[i])) {
                if (selectableCount == index) {
                    selection_ = static_cast<int>(i);
                    selectCurrent();
                    return true;
                }
                selectableCount++;
            }
        }
    }
    
    // Letter shortcuts
    const auto& items = getCurrentItems();
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].shortcut != 0 && 
            (items[i].shortcut == key || items[i].shortcut == (key ^ 0x20))) {  // Case insensitive
            if (isSelectable(items[i])) {
                selection_ = static_cast<int>(i);
                selectCurrent();
                return true;
            }
        }
    }
    
    return false;
}

bool Menu::handleAction(InputAction action) {
    switch (action) {
        case InputAction::UP:
            navigateUp();
            return true;
            
        case InputAction::DOWN:
            navigateDown();
            return true;
            
        case InputAction::SELECT:
        case InputAction::RIGHT:  // Enter submenu or select
            selectCurrent();
            return true;
            
        case InputAction::BACK:
        case InputAction::LEFT:   // Go back
            return goBack();
            
        case InputAction::MENU:
            // Could open context menu in future
            return false;
            
        default:
            return false;
    }
}

void Menu::navigateUp() {
    selection_ = findNextSelectable(selection_ - 1, -1);
}

void Menu::navigateDown() {
    selection_ = findNextSelectable(selection_ + 1, 1);
}

void Menu::selectCurrent() {
    const auto& items = getCurrentItems();
    if (selection_ < 0 || selection_ >= static_cast<int>(items.size())) return;
    
    const MenuItem& item = items[selection_];
    
    switch (item.type) {
        case MenuItemType::ACTION:
            if (callback_) {
                callback_(item.actionId);
            }
            break;
            
        case MenuItemType::SUBMENU:
            enterSubmenu(item.submenu(), item.label);
            break;
            
        case MenuItemType::BACK:
            goBack();
            break;
            
        case MenuItemType::SEPARATOR:
        case MenuItemType::ITEM_DISABLED:
            // Do nothing
            break;
    }
}

bool Menu::goBack() {
    if (menuStack_.empty()) {
        return false;  // Already at root
    }
    
    // Restore previous level
    MenuLevel prev = menuStack_.back();
    menuStack_.pop_back();
    
    if (menuStack_.empty()) {
        // Back to root
        title_ = rootTitle_;
        selection_ = prev.selection;
    } else {
        // Stay in submenu stack
        selection_ = prev.selection;
    }
    
    return true;
}

void Menu::reset() {
    menuStack_.clear();
    title_ = rootTitle_;
    selection_ = 0;
    ensureValidSelection();
}

// render() is now a template in the header file

const std::string& Menu::getCurrentTitle() const {
    if (menuStack_.empty()) {
        return title_;
    }
    return menuStack_.back().title;
}

const std::vector<MenuItem>& Menu::getCurrentItems() const {
    if (menuStack_.empty()) {
        return rootItems_;
    }
    return menuStack_.back().items;
}

void Menu::ensureValidSelection() {
    const auto& items = getCurrentItems();
    if (items.empty()) {
        selection_ = 0;
        return;
    }
    
    // Find first selectable item
    if (!isSelectable(items[selection_])) {
        selection_ = findNextSelectable(0, 1);
    }
}

bool Menu::isSelectable(const MenuItem& item) const {
    return item.type == MenuItemType::ACTION || 
           item.type == MenuItemType::SUBMENU ||
           item.type == MenuItemType::BACK;
}

int Menu::findNextSelectable(int from, int direction) {
    const auto& items = getCurrentItems();
    if (items.empty()) return 0;
    
    int count = static_cast<int>(items.size());
    int current = from;
    
    // Wrap around
    if (current < 0) current = count - 1;
    if (current >= count) current = 0;
    
    // Search for selectable item
    for (int i = 0; i < count; i++) {
        if (isSelectable(items[current])) {
            return current;
        }
        current += direction;
        if (current < 0) current = count - 1;
        if (current >= count) current = 0;
    }
    
    return 0;  // Fallback
}

void Menu::enterSubmenu(const std::vector<MenuItem>& items, const std::string& title) {
    // Save current state
    MenuLevel current;
    current.items = getCurrentItems();
    current.title = getCurrentTitle();
    current.selection = selection_;
    
    menuStack_.push_back(current);
    
    // Enter new submenu
    // Note: We store items in the stack, but we actually want to use the new items
    menuStack_.back().items = items;
    menuStack_.back().title = title;
    
    selection_ = 0;
    ensureValidSelection();
}

} // namespace adversary
