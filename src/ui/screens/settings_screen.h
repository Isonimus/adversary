/**
 * @file settings_screen.h
 * @brief Settings configuration screen
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/toast_manager.h"
#include "../components/status_bar.h"
#include "../components/toast_manager.h"
#include "../components/action_menu.h"
#include "../components/footer_hints.h"
#include "../components/text_input_popup.h"
#include "../theme_manager.h"
#include "modules/storage/settings_manager.h"
#include "modules/notification/notification_manager.h"
#include "modules/wifi/wifi_connection.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Settings item types
 */
enum class SettingType : uint8_t {
    HEADER,         // Section header (not editable)
    NUMBER,         // Numeric value with min/max
    TOGGLE,         // Boolean on/off
    INFO,           // Read-only info text
    ACTION,         // Triggerable action (button)
    SEPARATOR
};

/**
 * @brief Settings screen state
 */
enum class SettingsScreenState : uint8_t {
    BROWSING,       // Scrolling through items
    EDITING         // Editing selected value
};

/**
 * @brief Individual setting item
 */
struct SettingItem {
    const char* label;
    SettingType type;
    int32_t* valuePtr;      // Pointer to the value (cast as needed)
    int32_t minVal;
    int32_t maxVal;
    int32_t step;
    const char* suffix;     // e.g., "ms", "%", ""
    
    // Default constructor for array initialization
    SettingItem() 
        : label(""), type(SettingType::SEPARATOR), valuePtr(nullptr), minVal(0), maxVal(0), step(1), suffix("") {}
    
    SettingItem(const char* lbl, SettingType t) 
        : label(lbl), type(t), valuePtr(nullptr), minVal(0), maxVal(0), step(1), suffix("") {}
    
    SettingItem(const char* lbl, int32_t* val, int32_t min, int32_t max, int32_t stp = 1, const char* sfx = "")
        : label(lbl), type(SettingType::NUMBER), valuePtr(val), minVal(min), maxVal(max), step(stp), suffix(sfx) {}
    
    static SettingItem header(const char* lbl) {
        return SettingItem(lbl, SettingType::HEADER);
    }
    
    static SettingItem toggle(const char* lbl, int32_t* val) {
        SettingItem item(lbl, SettingType::TOGGLE);
        item.valuePtr = val;
        return item;
    }
    
    static SettingItem separator() {
        return SettingItem("", SettingType::SEPARATOR);
    }
    
    static SettingItem info(const char* lbl) {
        return SettingItem(lbl, SettingType::INFO);
    }
    
    static SettingItem action(const char* lbl) {
        return SettingItem(lbl, SettingType::ACTION);
    }
};

/**
 * @brief Settings configuration screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class SettingsScreen : public IScreen {
public:
    SettingsScreen();
    ~SettingsScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    
    const char* getName() const override { return "Settings"; }
    ScreenId getId() const override { return ScreenId::SETTINGS; }
    
    // =========================================================================
    // Settings-specific methods
    // =========================================================================
    
    void init() override;
    
    using SavedNetworksCallback = std::function<void()>;
    void setOnSavedNetworksRequested(SavedNetworksCallback callback) {
        onSavedNetworksRequested_ = callback;
    }

private:
    void drawHeader(Canvas& canvas, const char* title);
    void drawFooter(Canvas& canvas);
    void drawItems(Canvas& canvas);
    void buildSettingsList();
    void applyBrightness();
    
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    SettingsScreenState screenState_;
    
    // List state for scroll/selection management
    int selection_;
    int scrollOffset_;
    static constexpr int VISIBLE_ITEMS = 5;
    static constexpr int16_t ROW_HEIGHT = 18;
    
    // Settings items
    static constexpr int MAX_ITEMS = 50;  // API keys + Storage section
    SettingItem items_[MAX_ITEMS];
    int itemCount_;
    int selectableCount_;  // Count of non-header items

    // Deferred manifest rebuild: armed by the Rebuild Index confirm dialog so the
    // "Rebuilding index..." toast renders before the blocking ~20s dir scan.
    int rebuildPendingFrames_ = 0;
    
    // UI Components
    ui::FooterHints footerHints_;
    ui::ActionMenu actionMenu_;
    TextInputPopup deviceNamePopup_;
    TextInputPopup macPopup_;
    SavedNetworksCallback onSavedNetworksRequested_;
    
    // Temporary values for editing (cast from Settings fields)
    int32_t tempKarmaChannel_;
    int32_t tempKarmaAutoRotate_;
    int32_t tempKarmaRotationSpeed_;
    int32_t tempKarmaCooldown_;
    int32_t tempDeauthBurst_;
    int32_t tempDeauthInterval_;
    int32_t tempBeaconInterval_;
    int32_t tempWardrivingScanInterval_;
    int32_t tempBrightness_;
    int32_t tempSerialDebug_;
    int32_t tempNotifySounds_;
    int32_t tempNotifyLeds_;
    int32_t tempShowHeapBadge_;
    int32_t tempToastPosition_;
    int32_t tempThemePreset_;
    char themeLabelBuf_[32];
    int32_t tempCapOverride_;        // multi-radio cap detection override (slice-0002)
    char capOverrideLabelBuf_[32];
    char timeSyncLabelBuf_[48];
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t FOOTER_HEIGHT = 16;
    
    // Buffer for dynamic labels
    char savedSSIDLabel_[48];
    char deviceNameLabel_[48];
    char macLabelBuf_[48];
    
    // BLE Name
    TextInputPopup bleNamePopup_;
    char bleNameLabelBuf_[48];
    
    // Dashboard Server settings
    int32_t tempDashboardAuthEnabled_;
    TextInputPopup dashboardUserPopup_;
    TextInputPopup dashboardPassPopup_;
    char dashboardUserLabel_[48];
    char dashboardPassLabel_[48];
    
    // API Key popups
    TextInputPopup wpaSecKeyPopup_;
    TextInputPopup wigleKeyPopup_;
    TextInputPopup pwncrackKeyPopup_;
    char wpaSecKeyLabel_[48];
    char wigleKeyLabel_[48];
    char pwncrackKeyLabel_[48];
};

} // namespace adversary
