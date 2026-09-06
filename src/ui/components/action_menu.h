/**
 * @file action_menu.h
 * @brief Popup action menu component
 * 
 * Displays a context-sensitive popup menu with available actions
 * for a selected item (e.g., network, client, etc.)
 */

#ifndef ADVERSARY_ACTION_MENU_H
#define ADVERSARY_ACTION_MENU_H

#include "ui/theme.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace adversary {
namespace ui {

/**
 * @brief Action menu item
 */
struct ActionMenuItem {
    char shortcut;          ///< Keyboard shortcut (e.g., 'D', 'H')
    const char* label;      ///< Display label (e.g., "Deauth Attack")
    bool enabled;           ///< Whether the action is available
    
    ActionMenuItem(char key, const char* text, bool available = true)
        : shortcut(key), label(text), enabled(available) {}
};

/**
 * @brief Popup action menu component
 * 
 * Displays a centered popup with a title, subtitle, and list of actions.
 * Users can navigate with ; and . keys, select with Enter, or use shortcuts.
 */
class ActionMenu {
public:
    using ActionCallback = std::function<void(char shortcut)>;
    
    ActionMenu();
    ~ActionMenu() = default;
    
    /**
     * @brief Set the menu title (main heading)
     */
    void setTitle(const char* title) { title_ = title; }
    
    /**
     * @brief Set the menu subtitle (e.g., target details)
     */
    void setSubtitle(const char* subtitle) { subtitle_ = subtitle; }
    
    /**
     * @brief Clear all menu items
     */
    void clearItems() { items_.clear(); selectedIndex_ = 0; }
    
    /**
     * @brief Add a menu item
     */
    void addItem(char shortcut, const char* label, bool enabled = true);
    
    /**
     * @brief Set callback for when an action is selected
     */
    void setOnAction(ActionCallback callback) { callback_ = callback; }
    
    /**
     * @brief Set callback for when menu is dismissed
     */
    void setOnDismiss(std::function<void()> callback) { dismissCallback_ = callback; }
    
    /**
     * @brief Show the menu
     */
    void show();
    
    /**
     * @brief Hide the menu
     */
    void hide();
    
    /**
     * @brief Check if menu is visible
     */
    bool isVisible() const { return visible_; }
    
    /**
     * @brief Handle input
     * @param key Key pressed
     * @return true if input was handled
     */
    bool handleInput(char key);
    
    /**
     * @brief Render the menu
     */
    template<typename Canvas>
    void render(Canvas& canvas);
    
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

private:
    const char* title_;
    const char* subtitle_;
    std::vector<ActionMenuItem> items_;
    ActionCallback callback_;
    std::function<void()> dismissCallback_;
    
    int selectedIndex_;
    bool visible_;
    
    // Layout constants
    static constexpr int16_t MENU_PADDING = 8;
    static constexpr int16_t ITEM_HEIGHT = 16;
    static constexpr int16_t TITLE_HEIGHT = 18;
    static constexpr int16_t SUBTITLE_HEIGHT = 14;
    static constexpr int16_t FOOTER_HEIGHT = 14;
    static constexpr int16_t BORDER_RADIUS = 4;
};

// ==================== Template Implementation ====================

template<typename Canvas>
void ActionMenu::render(Canvas& canvas) {
    if (!visible_) return;
    
    int16_t screenWidth = canvas.width();
    int16_t screenHeight = canvas.height();
    
    // Calculate menu dimensions
    int16_t menuWidth = screenWidth - 40;  // 20px margin each side
    int16_t contentHeight = TITLE_HEIGHT + 
                            (subtitle_ ? SUBTITLE_HEIGHT : 0) + 
                            (items_.size() * ITEM_HEIGHT) + 
                            FOOTER_HEIGHT + 
                            (MENU_PADDING * 2);
    int16_t menuHeight = contentHeight;
    
    // Center the menu
    int16_t menuX = (screenWidth - menuWidth) / 2;
    int16_t menuY = (screenHeight - menuHeight) / 2;
    
    // Draw semi-transparent backdrop (darken background)
    // Since we can't do true transparency, just draw a dark rect
    canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
    
    // Draw menu background with border
    canvas.fillRoundRect(menuX, menuY, menuWidth, menuHeight, 
                         BORDER_RADIUS, theme::BG_SECONDARY());
    canvas.drawRoundRect(menuX, menuY, menuWidth, menuHeight, 
                         BORDER_RADIUS, theme::ACCENT());
    
    int16_t y = menuY + MENU_PADDING;
    
    // Draw title
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    
    // Center title
    int16_t titleWidth = strlen(title_) * 6;
    int16_t titleX = menuX + (menuWidth - titleWidth) / 2;
    canvas.setCursor(titleX, y);
    canvas.print(title_);
    y += TITLE_HEIGHT;
    
    // Draw subtitle if present
    if (subtitle_ && strlen(subtitle_) > 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        int16_t subWidth = strlen(subtitle_) * 6;
        int16_t subX = menuX + (menuWidth - subWidth) / 2;
        canvas.setCursor(subX, y);
        canvas.print(subtitle_);
        y += SUBTITLE_HEIGHT;
    }
    
    // Draw separator line
    canvas.drawFastHLine(menuX + MENU_PADDING, y, menuWidth - (MENU_PADDING * 2), 
                         theme::TEXT_DISABLED());
    y += 4;
    
    // Draw menu items
    for (size_t i = 0; i < items_.size(); i++) {
        const auto& item = items_[i];
        
        // Highlight selected item
        if (static_cast<int>(i) == selectedIndex_) {
            canvas.fillRect(menuX + 4, y - 4, menuWidth - 8, ITEM_HEIGHT, theme::ACCENT());
            canvas.setTextColor(theme::BG_PRIMARY());
        } else if (!item.enabled) {
            canvas.setTextColor(theme::TEXT_DISABLED());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        // Draw shortcut key
        canvas.setCursor(menuX + MENU_PADDING + 4, y);
        canvas.print(item.shortcut);
        canvas.print(": ");
        canvas.print(item.label);
        
        y += ITEM_HEIGHT;
    }
    
    // Draw separator
    y += 2;
    canvas.drawFastHLine(menuX + MENU_PADDING, y, menuWidth - (MENU_PADDING * 2), 
                         theme::TEXT_DISABLED());
    y += 4;
    
    // Draw footer hint
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(menuX + MENU_PADDING, y);
    canvas.print("`;/.:Nav  ENTER:Sel  ESC:Back");
}

} // namespace ui
} // namespace adversary

#endif // ADVERSARY_ACTION_MENU_H
