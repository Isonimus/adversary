/**
 * @file karma_screen.h
 * @brief Karma AP Attack UI Screen
 * 
 * Displays Karma AP status, captured probes, and harvested credentials.
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/attack/karma_ap.h"
#include "modules/sniffer/traffic_proxy.h"
#include "core/event_bus.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast_manager.h"
#include "ui/components/footer_hints.h"
#include "screen_interface.h"
#include "config/config.h"
#include <string>

namespace adversary {

/**
 * @brief Karma AP screen states
 */
enum class KarmaScreenState {
    CONFIG,
    LISTENING,
    ACTIVE,
    SNIFFER,
    PROBES,
    CREDENTIALS,
    CLIENTS,
    CLIENT_DETAILS,
    CLIENT_TRAFFIC
};

/**
 * @brief Karma AP Attack UI Screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class KarmaScreen : public IScreen {
public:
    KarmaScreen();
    ~KarmaScreen() override = default;
    
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
    
    const char* getName() const override { return "Karma AP"; }
    ScreenId getId() const override { return ScreenId::KARMA; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    bool startListening();
    bool startActive();
    bool startSniffer();
    bool startWithConfig();
    void stop();
    
    bool isRunning() const { return karma_.isRunning(); }
    KarmaScreenState getScreenState() const { return screenState_; }
    attack::KarmaAP& getKarmaAP() { return karma_; }
    void forceStopPortal() { karma_.forceStopPortal(); }

private:
    // Render methods for each state
    void renderConfig(Canvas& canvas);
    void renderListening(Canvas& canvas);
    void renderActive(Canvas& canvas);
    void renderSniffer(Canvas& canvas);
    void renderProbes(Canvas& canvas);
    void renderCredentials(Canvas& canvas);
    void renderClients(Canvas& canvas);
    void renderClientDetails(Canvas& canvas);
    void renderClientTraffic(Canvas& canvas);
    
    // Helper methods
    const char* getModeStr() const;
    const char* getPortalStr() const;
    
    // Core state
    attack::KarmaAP karma_;
    KarmaScreenState screenState_ = KarmaScreenState::CONFIG;
    KarmaScreenState previousState_ = KarmaScreenState::CONFIG;
    bool visible_ = false;
    bool shouldExit_ = false;
    bool needsRedraw_ = true;
    uint32_t lastRedrawMs_ = 0;
    uint32_t credentialHandlerId_ = 0;  // EventBus subscription
    uint32_t probeHandlerId_ = 0;       // PROBE_REQUEST_RECEIVED subscription
    
    // Scroll positions
    size_t probeScrollPos_ = 0;
    size_t credentialScrollPos_ = 0;
    size_t clientScrollPos_ = 0;
    size_t selectedClientIdx_ = 0;
    std::string selectedClientMac_;
    size_t trafficScrollPos_ = 0;
    
    // Config options
    static constexpr int CONFIG_OPTION_COUNT = 4;
    int configSelection_ = 0;
    uint8_t configMode_ = 1;
    uint8_t configPortal_ = 0;
    bool configLogSD_ = true;
    
    ui::FooterHints footerHints_;
};

} // namespace adversary
