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
#include "footer_hints.h"   // ui::footerKeyLabel: shared key-glyph house style (ADR-0098)
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
    std::string label;              ///< Display text (when enabled)
    std::string icon;               ///< Optional icon (emoji or char)
    MenuItemType type;              ///< Item type
    char shortcut;                  ///< Keyboard shortcut (0 = none)
    int actionId;                   ///< Action ID for callbacks
    std::shared_ptr<std::vector<MenuItem>> submenuPtr;  ///< Submenu items (if type == SUBMENU)
    /// Live availability gate (slice-0042), mirroring CarouselItem::enabledFn: when set it is
    /// consulted every frame so a hardware/SD-gated entry reflects the current detection state
    /// (after a Modules Re-scan / SD mount) instead of a boot-frozen type. Unset = not gated.
    std::function<bool()> enabledFn;
    std::string disabledLabel;      ///< Reason-bearing label shown greyed when the gate is off

    MenuItem() : type(MenuItemType::ACTION), shortcut(0), actionId(0) {}

    MenuItem(const std::string& lbl, MenuItemType t, int id = 0, char key = 0, const std::string& ico = "")
        : label(lbl), icon(ico), type(t), shortcut(key), actionId(id) {}

    /// True when the item is currently available. A live gate wins; otherwise a static
    /// ITEM_DISABLED is the only unavailable kind, so ungated items are unchanged.
    bool isEnabled() const {
        return enabledFn ? enabledFn() : type != MenuItemType::ITEM_DISABLED;
    }

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

    // Helper for a live-gated action item (slice-0042). Selectable and shown as `label`
    // while `enabledFn()` holds; otherwise greyed and unselectable, shown as `disabledLabel`
    // (the reason, e.g. "RFID (Module missing)"). Re-evaluated every frame, so a peripheral
    // attached after boot un-greys the entry without a menu rebuild.
    static MenuItem gated(const std::string& label, int actionId, char shortcut,
                          const std::string& disabledLabel, std::function<bool()> enabledFn) {
        MenuItem item(label, MenuItemType::ACTION, actionId, shortcut);
        item.disabledLabel = disabledLabel;
        item.enabledFn = std::move(enabledFn);
        return item;
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
    
    // Visible-row budget. A row's selection band is itemHeight tall and starts
    // highlightInsetAbove px above the text baseline (yPos), so a row is fully visible
    // when that band clears the action bar top. Count the bands that fit rather than a
    // naive contentHeight/itemHeight: 87/18 truncated to 4 and dropped a 5th row that
    // in fact fits (band at yPos=100 ends at y=113, above the y=115 action bar).
    const int16_t itemHeight = 18;
    const int16_t separatorHeight = 14;
    const int16_t highlightInsetAbove = 5;
    const int16_t actionBarTop = config::SCREEN_HEIGHT - 20;                    // 115
    const int maxVisible = (actionBarTop - (yPos - highlightInsetAbove)) / itemHeight;  // (115-23)/18 = 5
    
    int startIdx = 0;
    if (selection_ >= maxVisible) {
        startIdx = selection_ - maxVisible + 1;
    }
    
    int visibleCount = 0;
    for (size_t i = startIdx; i < items.size() && visibleCount < maxVisible; i++) {
        const MenuItem& item = items[i];
        
        // Stop before a row's selection band would cross into the action bar.
        if ((yPos - highlightInsetAbove) + itemHeight > actionBarTop) {
            break;
        }
        
        // Skip separators in selection but still draw them
        if (item.type == MenuItemType::SEPARATOR) {
            canvas.drawLine(10, yPos + 6, config::SCREEN_WIDTH - 10, yPos + 6, theme::TEXT_DISABLED());
            yPos += separatorHeight;
            visibleCount++;
            continue;
        }
        
        // Highlight selected item. "Disabled" is now the live isEnabled() verdict, not just
        // the static ITEM_DISABLED type, so a gated-off entry greys itself (slice-0042).
        const bool enabled = item.isEnabled();
        if (static_cast<int>(i) == selection_) {
            canvas.fillRect(0, yPos - 5, config::SCREEN_WIDTH, 18, theme::ACCENT());
            canvas.setTextColor(theme::BG_PRIMARY());
        } else if (!enabled) {
            canvas.setTextColor(theme::TEXT_DISABLED());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }

        canvas.setTextSize(1);
        canvas.setCursor(10, yPos);

        // A gated-off entry shows its reason-bearing label (e.g. "RFID (Module missing)").
        const std::string& shownLabel = (!enabled && !item.disabledLabel.empty())
                                            ? item.disabledLabel : item.label;
        canvas.print(shownLabel.c_str());
        
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
        ui::drawTriGlyph(canvas, config::SCREEN_WIDTH - 10, 25, ui::TriGlyph::Up,
                         theme::ACCENT());  // triangle (ADR-0100): items scrolled off above
    }
    if (startIdx + maxVisible < static_cast<int>(items.size())) {
        ui::drawTriGlyph(canvas, config::SCREEN_WIDTH - 10, config::SCREEN_HEIGHT - 30,
                         ui::TriGlyph::Down, theme::ACCENT());  // items scrolled off below
    }
    
    // Action bar, in the same house style as the FooterHints component: text keys from
    // ui::footerKeyLabel, the arrow-nav glyph as a filled triangle pair drawn with
    // ui::drawFooterKeyGlyphs (ADR-0100: the CP437 triangle bytes do not render through the
    // text layer). Moving the menu fully onto FooterHints rides with the Menu->ScrollableList
    // rework (LEDGER item 94). On the keyboardless M5StickC it shows the action words alone.
    const int16_t barY = config::SCREEN_HEIGHT - 14;
    canvas.fillRect(0, config::SCREEN_HEIGHT - 20, config::SCREEN_WIDTH, 20, theme::BG_SECONDARY());
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(5, barY);

#if defined(TARGET_M5STICK)
    canvas.print(menuStack_.empty() ? "Nav  Select" : "Back  Select");
#else
    if (menuStack_.empty()) {
        // Root: up+down triangle pair (2 cells) for vertical nav, drawn as geometry.
        ui::drawFooterKeyGlyphs(canvas, 5, barY, ';', theme::TEXT_SECONDARY());
        canvas.setCursor(5 + 2 * theme::FONT_CELL_WIDTH, barY);
        canvas.print(":Nav  ");
    } else {
        canvas.print(ui::footerKeyLabel('`'));  // Esc
        canvas.print(":Back  ");
    }
    canvas.print(ui::footerKeyLabel('\n'));      // Enter
    canvas.print(":Select");
#endif
}

} // namespace adversary
#endif

#endif // ADVERSARY_MENU_H
