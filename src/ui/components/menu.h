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
#include "footer_hints.h"       // FooterHints: the shared footer-hint bar (ADR-0098/0100)
#include "list_scroll_model.h"  // shared selection/scroll state + skip-nav predicate (ADR-0104)
#include "scrollable_list.h"    // shared list renderer: band + unified thumb (slice-0063)
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
    int getSelection() const { return listModel_.selectedIndex(); }
    int getMenuDepth() const { return menuStack_.size(); }
    const std::string& getCurrentTitle() const;
    const std::vector<MenuItem>& getCurrentItems() const;
    bool isAtRoot() const { return menuStack_.empty(); }

private:
    bool isSelectable(const MenuItem& item) const;
    /// Point the shared model at the current level: sync its row count, then set the selection
    /// (the predicate snaps off a separator/disabled row). Replaces the old ensureValidSelection
    /// + findNextSelectable skip-nav, now folded into ListScrollModel (ADR-0104).
    void syncModelToLevel(int desiredSelection);
    void enterSubmenu(const std::vector<MenuItem>& items, const std::string& title);

    std::vector<MenuItem> rootItems_;
    std::string title_;
    std::string rootTitle_;
    /// Selection + scroll window for the current level, with a selectable predicate that skips
    /// separators and live-disabled rows. The per-level selection is saved/restored via the
    /// menuStack_ entries (ADR-0104).
    ListScrollModel listModel_;
    ui::FooterHints footer_;  // the menu's own non-interactive hint bar
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

    // Footer hints, drawn through the shared FooterHints component (same house style as every
    // screen; replaces the old hand-rolled bar, ADR-0104). The bar is non-interactive here: the
    // menu keeps its own key handling and never gives the footer focus. The root offers vertical
    // Nav; a submenu offers Back. Enter always selects. On the keyboardless M5StickC the
    // component renders the action words alone.
    footer_.setHints(menuStack_.empty()
                         ? std::initializer_list<ui::FooterHint>{ {';', "Nav"}, {'\n', "Select"} }
                         : std::initializer_list<ui::FooterHint>{ {'`', "Back"}, {'\n', "Select"} });

    // The list body rides the shared component: it derives the viewport from the rect, paints
    // the app-wide selection band (ADR-0104 unifies the old accent-inverted idiom with every
    // data list), and draws the one proportional scrollbar thumb (retiring the up/down carets).
    // The callback draws row content only — label/greying/chevron for an item, a divider line
    // for a separator; the band is never painted here. The model's predicate already keeps the
    // cursor off separators and live-disabled rows, so the selected row is always a real item.
    const int16_t listTop = config::STATUS_BAR_HEIGHT + 2;
    const int16_t listH =
        static_cast<int16_t>(config::SCREEN_HEIGHT - ui::FOOTER_HEIGHT - listTop);
    canvas.setTextSize(1);
    ScrollableList::render(
        canvas, listModel_, 0, listTop, config::SCREEN_WIDTH, listH, theme::LIST_ROW_HEIGHT,
        [&items](Canvas& c, int itemIndex, int16_t rowTopY, bool /*isSelected*/) {
            const MenuItem& item = items[static_cast<size_t>(itemIndex)];
            if (item.type == MenuItemType::SEPARATOR) {
                const int16_t midY = rowTopY + theme::LIST_ROW_HEIGHT / 2;
                c.drawLine(10, midY, config::SCREEN_WIDTH - 10, midY, theme::TEXT_DISABLED());
                return;
            }
            // "Disabled" is the live isEnabled() verdict, not just the static ITEM_DISABLED type,
            // so a gated-off entry greys itself and shows its reason-bearing label, e.g.
            // "RFID (Module missing)" (slice-0042). The selected row reads as primary text on
            // the shared band, no longer inverted.
            const bool enabled = item.isEnabled();
            c.setTextColor(enabled ? theme::TEXT_PRIMARY() : theme::TEXT_DISABLED());
            const int16_t textY = rowTopY + theme::LIST_ROW_TEXT_DY;
            c.setCursor(10, textY);
            const std::string& shownLabel = (!enabled && !item.disabledLabel.empty())
                                                ? item.disabledLabel : item.label;
            c.print(shownLabel.c_str());
            if (item.type == MenuItemType::SUBMENU) {
                c.setCursor(config::SCREEN_WIDTH - 15, textY);
                c.print(">");
            }
        });  // focused defaults true: the menu footer never steals the band's colour

    footer_.render(canvas);
}

} // namespace adversary
#endif

#endif // ADVERSARY_MENU_H
