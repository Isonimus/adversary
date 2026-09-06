/**
 * @file menu.h
 * @brief Hierarchical menu system component
 * 
 * Provides a flexible menu system with support for nested submenus,
 * icons, and keyboard shortcuts.
 */

#ifndef ADVERSARY_MENU_H
#define ADVERSARY_MENU_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "../../hal/input/input_manager.h"
#include <memory>

namespace adversary {

/**
 * @brief Menu item types
 */
enum class MenuItemType : uint8_t {
    ACTION,         ///< Executes an action when selected
    SUBMENU,        ///< Opens a submenu when selected
    BACK,           ///< Goes back to parent menu
    SEPARATOR,      ///< Visual separator (not selectable)
    ITEM_DISABLED   ///< Grayed out item (not selectable)
};

/**
 * @brief Single menu item
 */
struct MenuItem {
    std::string label;              ///< Display text
    std::string icon;               ///< Optional icon (emoji or char)
    MenuItemType type;              ///< Item type
    char shortcut;                  ///< Keyboard shortcut (0 = none)
    int actionId;                   ///< Action ID for callbacks
    std::shared_ptr<std::vector<MenuItem>> submenuPtr;  ///< Submenu items (if type == SUBMENU)
    
    MenuItem() : type(MenuItemType::ACTION), shortcut(0), actionId(0) {}
    
    MenuItem(const std::string& lbl, MenuItemType t, int id = 0, char key = 0, const std::string& ico = "")
        : label(lbl), icon(ico), type(t), shortcut(key), actionId(id) {}
    
    // Get submenu (returns empty vector if none)
    const std::vector<MenuItem>& submenu() const {
        static const std::vector<MenuItem> empty;
        return submenuPtr ? *submenuPtr : empty;
    }
    
    // Helper for creating action items
    static MenuItem action(const std::string& label, int actionId, char shortcut = 0) {
        return MenuItem(label, MenuItemType::ACTION, actionId, shortcut);
    }
    
    // Helper for creating submenu items
    static MenuItem submenu(const std::string& label, const std::vector<MenuItem>& items, char shortcut = 0) {
        MenuItem item(label, MenuItemType::SUBMENU, 0, shortcut);
        item.submenuPtr = std::make_shared<std::vector<MenuItem>>(items);
        return item;
    }
    
    // Helper for creating back item
    static MenuItem back(const std::string& label = "< Back") {
        return MenuItem(label, MenuItemType::BACK, 0, '`');
    }
    
    // Helper for creating separator
    static MenuItem separator() {
        return MenuItem("", MenuItemType::SEPARATOR);
    }
    
    // Helper for creating disabled item
    static MenuItem disabled(const std::string& label) {
        return MenuItem(label, MenuItemType::ITEM_DISABLED);
    }
};

/**
 * @brief Hierarchical menu component
 * 
 * Supports nested menus with navigation and keyboard shortcuts.
 */
class Menu {
public:
    /// Callback when an action item is selected
    using ActionCallback = std::function<void(int actionId)>;
    
    Menu();
    ~Menu() = default;
    
    /**
     * @brief Set the root menu items
     */
    void setItems(const std::vector<MenuItem>& items);
    
    /**
     * @brief Set callback for action selection
     */
    void setOnAction(ActionCallback callback) { callback_ = callback; }
    
    /**
     * @brief Set menu title
     */
    void setTitle(const std::string& title) { title_ = title; }
    
    /**
     * @brief Handle key input (legacy)
     * @return true if input was handled
     */
    bool handleInput(char key);
    
    /**
     * @brief Handle InputAction from InputManager
     * @return true if input was handled
     */
    bool handleAction(InputAction action);
    
    /**
     * @brief Navigate up
     */
    void navigateUp();
    
    /**
     * @brief Navigate down
     */
    void navigateDown();
    
    /**
     * @brief Select current item
     */
    void selectCurrent();
    
    /**
     * @brief Go back to parent menu
     * @return true if went back, false if already at root
     */
    bool goBack();
    
    /**
     * @brief Reset to root menu
     */
    void reset();
    
    /**
     * @brief Render the menu to a canvas (flicker-free)
     * @param canvas Canvas to render to
     */
    template<typename Canvas>
    void render(Canvas& canvas);
    
    // Getters
    int getSelection() const { return selection_; }
    int getMenuDepth() const { return menuStack_.size(); }
    const std::string& getCurrentTitle() const;
    const std::vector<MenuItem>& getCurrentItems() const;
    bool isAtRoot() const { return menuStack_.empty(); }
    
private:
    void ensureValidSelection();
    bool isSelectable(const MenuItem& item) const;
    int findNextSelectable(int from, int direction);
    void enterSubmenu(const std::vector<MenuItem>& items, const std::string& title);
    
    std::vector<MenuItem> rootItems_;
    std::string title_;
    std::string rootTitle_;
    int selection_;
    ActionCallback callback_;
    
    // Stack for submenu navigation
    struct MenuLevel {
        std::vector<MenuItem> items;
        std::string title;
        int selection;
    };
    std::vector<MenuLevel> menuStack_;
};

} // namespace adversary

// Template implementation must be in header
#ifdef ESP32
#include <M5Unified.h>
#include "../theme.h"
#include "../../config/config.h"
#include "status_bar.h"

namespace adversary {

template<typename Canvas>
void Menu::render(Canvas& canvas) {
    const auto& items = getCurrentItems();
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Status bar with SD and battery indicators. In a submenu, prefix the title
    // with a back chevron so the "you can go back" hint sits on the LEFT with the
    // title — the previous fixed-position overlay at (SCREEN_WIDTH-50, 6) collided
    // with the right-aligned status badges.
    std::string title = getCurrentTitle();
    if (!menuStack_.empty()) {
        title = "< " + title;
    }
    ui::StatusBar::render(canvas, title.c_str());
    
    // Menu items
    int16_t yPos = 20 + 8;
    
    // Calculate visible range for scrolling
    // Account for: status bar (20px), padding (8px), action bar (20px)
    const int16_t itemHeight = 18;
    const int16_t separatorHeight = 14;
    const int16_t contentAreaHeight = config::SCREEN_HEIGHT - 20 - 8 - 20;  // 135 - 48 = 87px
    const int maxVisible = contentAreaHeight / itemHeight;  // ~4-5 items
    
    int startIdx = 0;
    if (selection_ >= maxVisible) {
        startIdx = selection_ - maxVisible + 1;
    }
    
    int visibleCount = 0;
    for (size_t i = startIdx; i < items.size() && visibleCount < maxVisible; i++) {
        const MenuItem& item = items[i];
        
        // Check if we'd overflow into the action bar
        if (yPos + itemHeight > config::SCREEN_HEIGHT - 20) {
            break;
        }
        
        // Skip separators in selection but still draw them
        if (item.type == MenuItemType::SEPARATOR) {
            canvas.drawLine(10, yPos + 6, config::SCREEN_WIDTH - 10, yPos + 6, theme::TEXT_DISABLED());
            yPos += separatorHeight;
            visibleCount++;
            continue;
        }
        
        // Highlight selected item
        if (static_cast<int>(i) == selection_) {
            canvas.fillRect(0, yPos - 5, config::SCREEN_WIDTH, 18, theme::ACCENT());
            canvas.setTextColor(theme::BG_PRIMARY());
        } else if (item.type == MenuItemType::ITEM_DISABLED) {
            canvas.setTextColor(theme::TEXT_DISABLED());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        canvas.setTextSize(1);
        canvas.setCursor(10, yPos);
        
        // Just display the label - no numbering
        canvas.print(item.label.c_str());
        
        // Submenu indicator
        if (item.type == MenuItemType::SUBMENU) {
            canvas.setCursor(config::SCREEN_WIDTH - 15, yPos);
            canvas.print(">");
        }
        
        yPos += itemHeight;
        visibleCount++;
    }
    
    // Scroll indicators
    if (startIdx > 0) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, 25);
        canvas.print("^");
    }
    if (startIdx + maxVisible < static_cast<int>(items.size())) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, config::SCREEN_HEIGHT - 30);
        canvas.print("v");
    }
    
    // Action bar
    canvas.fillRect(0, config::SCREEN_HEIGHT - 20, config::SCREEN_WIDTH, 20, theme::BG_SECONDARY());
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(5, config::SCREEN_HEIGHT - 14);
    
    if (menuStack_.empty()) {
        canvas.print(";/.:Nav  ENTER:Select");
    } else {
        canvas.print("`:Back  ENTER:Select");
    }
}

} // namespace adversary
#endif

#endif // ADVERSARY_MENU_H
