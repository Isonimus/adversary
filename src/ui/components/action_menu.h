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
#include "ui/components/list_scroll_model.h"
#include "ui/components/scrollable_list.h"
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
     *
     * The text is copied into the menu's own buffer, so callers may pass a
     * temporary (e.g. a std::string's c_str()) without the pointer dangling.
     * Titles longer than TEXT_MAX-1 are truncated.
     */
    void setTitle(const char* title) { copyText(title_, title); }

    /**
     * @brief Set the menu subtitle (e.g., target details); nullptr clears it.
     */
    void setSubtitle(const char* subtitle) { copyText(subtitle_, subtitle); }
    
    /**
     * @brief Clear all menu items
     */
    void clearItems() { items_.clear(); model_.setItemCount(0); }
    
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
    // Owned, fixed-capacity text: the menu copies titles in rather than borrowing the
    // caller's pointer, so a title built from a temporary can't dangle (char[] over String
    // to avoid heap fragmentation on this chip, per CLAUDE.md).
    static constexpr size_t TEXT_MAX = 48;
    static void copyText(char (&dst)[TEXT_MAX], const char* src) {
        if (!src) { dst[0] = '\0'; return; }
        strncpy(dst, src, TEXT_MAX - 1);
        dst[TEXT_MAX - 1] = '\0';
    }

    char title_[TEXT_MAX];
    char subtitle_[TEXT_MAX];
    std::vector<ActionMenuItem> items_;
    ActionCallback callback_;
    std::function<void()> dismissCallback_;

    // Selection + scroll window are owned by the shared, unit-tested model (slice-0062), the
    // same one the config forms use. Its selectable-predicate skips disabled rows, so the popup
    // scrolls and navigates consistently with every other list instead of overflowing the
    // screen when it has many actions.
    ListScrollModel model_;
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

    const int16_t screenWidth = canvas.width();
    const int16_t screenHeight = canvas.height();
    const int16_t menuWidth = screenWidth - 40;  // 20px margin each side

    // Everything except the item rows. The item area is then capped to whatever height is left
    // so the popup never grows past the screen: extra actions scroll instead of clipping.
    const int16_t chrome = (MENU_PADDING * 2) + TITLE_HEIGHT +
                           (subtitle_[0] ? SUBTITLE_HEIGHT : 0) +
                           4 +   // separator under the title/subtitle
                           6 +   // spacing + separator above the footer
                           FOOTER_HEIGHT;
    const int16_t maxMenuHeight = screenHeight - 8;  // 4px breathing room top and bottom
    int maxItemRows = (maxMenuHeight - chrome) / ITEM_HEIGHT;
    if (maxItemRows < 1) maxItemRows = 1;
    const int itemCount = static_cast<int>(items_.size());
    int visibleRows = (itemCount > 0 && itemCount < maxItemRows) ? itemCount : maxItemRows;
    if (visibleRows < 1) visibleRows = 1;

    const int16_t itemsAreaH = static_cast<int16_t>(visibleRows * ITEM_HEIGHT);
    const int16_t menuHeight = chrome + itemsAreaH;

    const int16_t menuX = (screenWidth - menuWidth) / 2;
    const int16_t menuY = (screenHeight - menuHeight) / 2;

    // Darken the background (no true alpha on this canvas) and draw the bordered popup.
    canvas.fillRect(0, 0, screenWidth, screenHeight, theme::BG_PRIMARY());
    canvas.fillRoundRect(menuX, menuY, menuWidth, menuHeight, BORDER_RADIUS, theme::BG_SECONDARY());
    canvas.drawRoundRect(menuX, menuY, menuWidth, menuHeight, BORDER_RADIUS, theme::ACCENT());

    int16_t y = menuY + MENU_PADDING;

    // Title (centered)
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    const int16_t titleWidth = strlen(title_) * theme::FONT_CELL_WIDTH;
    canvas.setCursor(menuX + (menuWidth - titleWidth) / 2, y);
    canvas.print(title_);
    y += TITLE_HEIGHT;

    // Subtitle (centered), when present
    if (subtitle_[0]) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        const int16_t subWidth = strlen(subtitle_) * theme::FONT_CELL_WIDTH;
        canvas.setCursor(menuX + (menuWidth - subWidth) / 2, y);
        canvas.print(subtitle_);
        y += SUBTITLE_HEIGHT;
    }

    canvas.drawFastHLine(menuX + MENU_PADDING, y, menuWidth - (MENU_PADDING * 2),
                         theme::TEXT_DISABLED());
    y += 4;

    // Scrollable item body (slice-0062 primitive): the shared renderer paints the selection
    // band, the visible window and a proportional thumb; the callback draws one row's text.
    // This is the one place the modal selected-row style is set (slice-0063).
    model_.setItemCount(itemCount);
    const int16_t itemsX = menuX + 4;
    const int16_t itemsW = menuWidth - 8;
    ScrollableList::render(canvas, model_, itemsX, y, itemsW, itemsAreaH, ITEM_HEIGHT,
        [this, itemsX](Canvas& c, int idx, int16_t rowY, bool selected) {
            const ActionMenuItem& item = items_[static_cast<size_t>(idx)];
            (void)selected;  // band is drawn by the renderer; text stays readable on it
            const int16_t textDy = (ITEM_HEIGHT - theme::FONT_CELL_HEIGHT) / 2;
            c.setTextColor(item.enabled ? theme::TEXT_PRIMARY() : theme::TEXT_DISABLED());
            c.setCursor(itemsX + MENU_PADDING, rowY + textDy);
            c.print(item.shortcut);
            c.print(": ");
            c.print(item.label);
        },
        true);
    y += itemsAreaH + 2;

    canvas.drawFastHLine(menuX + MENU_PADDING, y, menuWidth - (MENU_PADDING * 2),
                         theme::TEXT_DISABLED());
    y += 4;

    // Footer hint
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(menuX + MENU_PADDING, y);
    canvas.print("`;/.:Nav  ENTER:Sel  ESC:Back");
}

} // namespace ui
} // namespace adversary

#endif // ADVERSARY_ACTION_MENU_H
