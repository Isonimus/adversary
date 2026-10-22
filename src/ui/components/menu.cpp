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
{
    // The shared model skips any row the menu treats as non-selectable (separator or
    // live-disabled), consulting the live current level each time so greying takes effect
    // without a rebuild. Replaces the old findNextSelectable/ensureValidSelection (ADR-0104).
    listModel_.setSelectablePredicate([this](int index) {
        const auto& items = getCurrentItems();
        return index >= 0 && index < static_cast<int>(items.size()) &&
               isSelectable(items[static_cast<size_t>(index)]);
    });
}

void Menu::setItems(const std::vector<MenuItem>& items) {
    rootItems_ = items;
    rootTitle_ = title_;
    menuStack_.clear();
    syncModelToLevel(0);
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
                    listModel_.setSelectedIndex(static_cast<int>(i));
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
                listModel_.setSelectedIndex(static_cast<int>(i));
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
    listModel_.moveUp();
}

void Menu::navigateDown() {
    listModel_.moveDown();
}

void Menu::selectCurrent() {
    const auto& items = getCurrentItems();
    const int sel = listModel_.selectedIndex();
    if (sel < 0 || sel >= static_cast<int>(items.size())) return;

    const MenuItem& item = items[static_cast<size_t>(sel)];
    
    switch (item.type) {
        case MenuItemType::ACTION:
            // A module unplugged while the cursor rests on its entry greys it live; refuse to
            // dispatch an action the UI is showing as unavailable (slice-0042, fail loud).
            if (callback_ && item.isEnabled()) {
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

    // Restore the parent level's saved selection (stored on the level we are leaving).
    MenuLevel prev = menuStack_.back();
    menuStack_.pop_back();
    if (menuStack_.empty()) {
        title_ = rootTitle_;  // Back to root
    }
    syncModelToLevel(prev.selection);
    return true;
}

void Menu::reset() {
    menuStack_.clear();
    title_ = rootTitle_;
    syncModelToLevel(0);
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

void Menu::syncModelToLevel(int desiredSelection) {
    const auto& items = getCurrentItems();
    listModel_.setItemCount(static_cast<int>(items.size()));
    // The predicate snaps the cursor off a separator/disabled row, so this lands on the first
    // selectable row at or after desiredSelection (what ensureValidSelection used to do).
    listModel_.setSelectedIndex(desiredSelection);
}

bool Menu::isSelectable(const MenuItem& item) const {
    // A live-gated entry (slice-0042) is selectable only while its gate holds, so nav,
    // ensureValidSelection, and the shortcut lookups all skip it exactly as a static
    // ITEM_DISABLED item. isEnabled() is true for every ungated non-disabled item.
    return (item.type == MenuItemType::ACTION ||
            item.type == MenuItemType::SUBMENU ||
            item.type == MenuItemType::BACK) &&
           item.isEnabled();
}

void Menu::enterSubmenu(const std::vector<MenuItem>& items, const std::string& title) {
    // Save current state (the parent level's selection is stored on the pushed entry and
    // restored by goBack).
    MenuLevel current;
    current.items = getCurrentItems();
    current.title = getCurrentTitle();
    current.selection = listModel_.selectedIndex();

    menuStack_.push_back(current);

    // Enter new submenu
    // Note: We store items in the stack, but we actually want to use the new items
    menuStack_.back().items = items;
    menuStack_.back().title = title;

    syncModelToLevel(0);
}

} // namespace adversary
