/**
 * @file evil_twin_screen.h
 * @brief Evil Twin Attack UI Screen
 * 
 * Displays Evil Twin attack status, connected clients,
 * and captured credentials.
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/attack/evil_twin.h"
#include "modules/sniffer/traffic_proxy.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/text_input_popup.h"
#include "ui/components/toast_manager.h"
#include "ui/components/footer_hints.h"
#include "config/config.h"
#include "utils/mac_utils.h"
#include "screen_interface.h"

namespace adversary {

/**
 * @brief Evil Twin screen states
 */
enum class EvilTwinScreenState {
    CONFIG,         // Configuration screen
    RUNNING,        // Attack active with captive portal
    SNIFFER,        // Attack active with traffic proxy
    CREDENTIALS,    // Viewing captured credentials
    CLIENTS         // Viewing connected clients
};

/**
 * @brief Evil Twin Attack UI Screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class EvilTwinScreen : public IScreen {
public:
    EvilTwinScreen();
    ~EvilTwinScreen() override = default;
    
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
    
    const char* getName() const override { return "Evil Twin"; }
    ScreenId getId() const override { return ScreenId::EVIL_TWIN; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void setTarget(const char* ssid, const uint8_t* bssid, uint8_t channel);
    
    // Factory lazy-loading: receive target via ScreenManager::navigateWithParams()
    // Note: EvilTwin setTarget arg order is (ssid, bssid, channel) — opposite to others
    void setParams(const ScreenParams& params) override {
        setTarget(params.ssid, params.bssid, params.channel);
    }
    bool start();
    void stop();
    bool startSniffer();
    bool startWithConfig();
    
    bool isRunning() const;
    EvilTwinScreenState getScreenState() const { return screenState_; }
    attack::EvilTwin& getEvilTwin() { return evilTwin_; }
    void forceStopPortal();

private:
    void renderConfig(Canvas& canvas);
    void renderRunning(Canvas& canvas);
    void renderSniffer(Canvas& canvas);
    void renderCredentials(Canvas& canvas);
    void renderClients(Canvas& canvas);
    
    const char* getModeStr() const;
    const char* getPortalStr() const;
    const char* getDeauthIntervalStr() const;
    uint32_t getDeauthIntervalMs() const;
    const char* getSSIDDisplay() const;
    
    attack::EvilTwin evilTwin_;
    EvilTwinScreenState screenState_ = EvilTwinScreenState::CONFIG;
    bool visible_ = false;
    bool shouldExit_ = false;
    bool needsRedraw_ = true;
    uint32_t lastRedrawMs_ = 0;
    uint32_t credentialHandlerId_ = 0;  // EventBus subscription
    
    size_t credentialScrollPos_ = 0;
    size_t clientScrollPos_ = 0;
    
    // Config options
    static constexpr int CONFIG_OPTION_COUNT = 7;
    int configSelection_ = 0;
    int configScrollOffset_ = 0;
    
    // Config values
    uint8_t configMode_ = 0;           // 0=Portal, 1=Sniffer
    uint8_t configPortal_ = 0;         // 0=Generic, 1=Google
    bool configUseFixedSSID_ = false;
    char configFixedSSID_[33] = {0};
    bool configDeauth_ = false;
    uint8_t configDeauthInterval_ = 1; // 0=5s, 1=10s, 2=30s
    bool configLogSD_ = true;
    
    // Text input popup
    TextInputPopup ssidPopup_;
    bool showingSsidPopup_ = false;
    
    // UI Components
    ui::FooterHints footerHints_;
};

} // namespace adversary
