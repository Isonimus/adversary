/**
 * @file scanner_screen.h
 * @brief WiFi Scanner UI screen
 * 
 * Displays a list of discovered WiFi networks with
 * detailed information and interaction capabilities.
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/wifi/wifi_scanner.h"
#include "modules/storage/capture_registry.h"
#include "modules/storage/settings_manager.h"
#include "core/event_bus.h"
#include "ui/theme.h"
#include "ui/components/action_menu.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "ui/components/text_input_popup.h"
#include "modules/wifi/wifi_connection.h"
#include "screen_interface.h"
#include <cstdint>
#include <cstring>
#include <functional>

#ifdef ESP32
#include <M5GFX.h>
#endif

namespace adversary {

/**
 * @brief Sort mode for network list
 */
enum class SortMode : uint8_t {
    SIGNAL,
    CHANNEL,
    SSID
};

/**
 * @brief Action types from action menu
 */
enum class NetworkAction : uint8_t {
    DEAUTH,
    HANDSHAKE,
    EVIL_TWIN,
    PROBE_FLOOD,
    INFO
};

/**
 * @brief Scanner screen UI component
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class ScannerScreen : public IScreen {
public:
    using NetworkSelectedCallback = std::function<void(const NetworkInfo&)>;
    using NetworkActionCallback = std::function<void(const NetworkInfo&, NetworkAction)>;
    
    ScannerScreen();
    ~ScannerScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return m_active; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }
    
    const char* getName() const override { return "WiFi Scanner"; }
    ScreenId getId() const override { return ScreenId::SCANNER; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    void deinit();
    
    void navigateUp();
    void navigateDown();
    void selectCurrent();
    void toggleScan();
    void cycleSortMode();
    
    void setOnNetworkSelected(NetworkSelectedCallback callback) { m_onNetworkSelected = callback; }
    void setOnNetworkAction(NetworkActionCallback callback) { m_onNetworkAction = callback; }
    
    size_t getSelectedIndex() const { return m_selectedIndex; }
    bool isActive() const { return m_active; }
    void setActive(bool active);

private:
    void startScan();
    void stopScan();
    
    void drawNetworkEntry(Canvas& canvas, const NetworkInfo& net, int16_t y, bool selected);
    void drawSignalBars(Canvas& canvas, int16_t x, int16_t y, int8_t rssi, uint16_t color);
    void drawHeader(Canvas& canvas);
    
    void applySort();
    void setupActionMenu();
    void handleAction(char action);
    
    WiFiScanner& m_scanner;
    NetworkSelectedCallback m_onNetworkSelected;
    NetworkActionCallback m_onNetworkAction;
    size_t m_selectedIndex;
    size_t m_scrollOffset;
    SortMode m_sortMode;
    bool m_active;
    bool m_shouldExit;
    bool m_scanning;
    bool m_needsRedraw;
    uint32_t m_lastUpdate;
    size_t m_lastNetworkCount;
    uint32_t m_scanHandlerId = 0;
    
    // Action menu for network selection
    ui::ActionMenu m_actionMenu;
    TextInputPopup m_passwordPopup;
    ui::FooterHints footerHints_;
    NetworkInfo m_selectedNetwork;
    
#ifdef ESP32
    // Double buffer canvas for flicker-free rendering
    M5Canvas* m_canvas;
    bool m_canvasInitialized;
#endif
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t STATUS_HEIGHT = 16;
    static constexpr int16_t ENTRY_HEIGHT = 24;
    static constexpr int16_t VISIBLE_ENTRIES = 4;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 500;
};

} // namespace adversary
