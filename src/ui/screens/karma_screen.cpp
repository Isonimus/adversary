/**
 * @file karma_screen.cpp
 * @brief Karma AP Attack UI screen implementation
 */

#include "karma_screen.h"

#ifdef ESP32
#include <WiFi.h>
#endif

namespace adversary {

// =============================================================================
// Constructor
// =============================================================================

KarmaScreen::KarmaScreen()
    : screenState_(KarmaScreenState::CONFIG)
    , previousState_(KarmaScreenState::CONFIG)
    , visible_(false)
    , shouldExit_(false)
    , probeScrollPos_(0)
    , credentialScrollPos_(0)
    , clientScrollPos_(0)
    , selectedClientIdx_(0)
    , trafficScrollPos_(0)
    , configSelection_(0)
    , configMode_(1)
    , configPortal_(0)
    , configLogSD_(true)
{
}

// =============================================================================
// IScreen Interface
// =============================================================================

void KarmaScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    screenState_ = KarmaScreenState::CONFIG;
    configSelection_ = 0;
    
    footerHints_.setHints({}); // Will be set dynamically in render
    footerHints_.setFocus(false);
    
    // Subscribe to credential capture events for notifications
    if (credentialHandlerId_ == 0) {
        credentialHandlerId_ = EventBus::getInstance().subscribe(
            EventType::CREDENTIAL_CAPTURED,
            [this](const EventData& evt) {
                // Show toast notification when credential is captured
                showSuccessToast("Credential captured!");
            }
        );
    }
    
    // Subscribe to probe request events for reactive UI updates
    if (probeHandlerId_ == 0) {
        probeHandlerId_ = EventBus::getInstance().subscribe(
            EventType::PROBE_REQUEST_RECEIVED,
            [this](const EventData&) {
                if (screenState_ == KarmaScreenState::LISTENING) {
                    showSuccessToast("Probe captured!");
                }
            }
        );
    }
}

void KarmaScreen::hide() {
    // Unsubscribe from EventBus
    if (credentialHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(credentialHandlerId_);
        credentialHandlerId_ = 0;
    }
    if (probeHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(probeHandlerId_);
        probeHandlerId_ = 0;
    }
    
    visible_ = false;
    if (karma_.isRunning()) {
        stop();
    }
}

void KarmaScreen::update() {
    if (!visible_) return;
    karma_.update();
    
    // For live-counter views refresh at ~2 Hz; static views redraw on input only.
    if (screenState_ == KarmaScreenState::LISTENING ||
        screenState_ == KarmaScreenState::ACTIVE ||
        screenState_ == KarmaScreenState::SNIFFER) {
        uint32_t now = millis();
        if (now - lastRedrawMs_ >= 500) {
            needsRedraw_ = true;
            lastRedrawMs_ = now;
        }
    }
}

// =============================================================================
// Start/Stop Methods
// =============================================================================

bool KarmaScreen::startListening() {
    if (karma_.startListening()) {
        screenState_ = KarmaScreenState::LISTENING;
        return true;
    }
    return false;
}

bool KarmaScreen::startActive() {
    ap::CaptivePortalConfig config;
    config.setTitle("Network Login");
    config.setSuccessMessage("Connected! You may now browse.");
    config.logToSD = true;
    
    if (karma_.startActive(config)) {
        screenState_ = KarmaScreenState::ACTIVE;
        return true;
    }
    return false;
}

bool KarmaScreen::startSniffer() {
#ifdef ESP32
    if (WiFi.status() != WL_CONNECTED) {
        ToastManager::getInstance().show("No WiFi! Connect first", 
            ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
        return false;
    }
#endif
    ap::CaptivePortalConfig config;
    config.enableCaptivePortal = false;
    config.logToSD = true;
    
    if (karma_.startActive(config)) {
        if (TrafficProxy::getInstance().start()) {
            screenState_ = KarmaScreenState::SNIFFER;
            return true;
        }
#ifdef ESP32
        ToastManager::getInstance().show("Traffic proxy failed!", 
            ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
#endif
        karma_.stop();
    }
    return false;
}

bool KarmaScreen::startWithConfig() {
    switch (configMode_) {
        case 0:
            if (karma_.startListening()) {
                screenState_ = KarmaScreenState::LISTENING;
                return true;
            }
            break;
            
        case 1: {
            ap::CaptivePortalConfig config;
            config.pageType = configPortal_ == 0 ? 
                ap::PortalPageType::GENERIC_LOGIN : ap::PortalPageType::SOCIAL_GOOGLE;
            config.setTitle(configPortal_ == 0 ? "Network Login" : "Google Sign-In");
            config.setSuccessMessage("Connected! You may now browse.");
            config.logToSD = configLogSD_;
            config.enableCaptivePortal = true;
            
            if (karma_.startActive(config)) {
                screenState_ = KarmaScreenState::ACTIVE;
                return true;
            }
            break;
        }
        
        case 2: {
#ifdef ESP32
            if (WiFi.status() != WL_CONNECTED) {
                ToastManager::getInstance().show("No WiFi! Connect first", 
                    ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
                return false;
            }
#endif
            ap::CaptivePortalConfig config;
            config.enableCaptivePortal = false;
            config.logToSD = configLogSD_;
            
            if (karma_.startActive(config)) {
                if (TrafficProxy::getInstance().start()) {
                    screenState_ = KarmaScreenState::SNIFFER;
                    return true;
                }
#ifdef ESP32
                ToastManager::getInstance().show("Traffic proxy failed!", 
                    ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
#endif
                karma_.stop();
            }
            break;
        }
    }
    return false;
}

void KarmaScreen::stop() {
    if (TrafficProxy::getInstance().isRunning()) {
        TrafficProxy::getInstance().stop();
    }
    karma_.stop();
    screenState_ = KarmaScreenState::CONFIG;
}

// =============================================================================
// Input Handling
// =============================================================================

bool KarmaScreen::handleInput(char key) {
    needsRedraw_ = true;  // any key press triggers redraw
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);
            return true;
        }
    }

    switch (key) {
        case '`':
        case 27:
            if (screenState_ == KarmaScreenState::PROBES ||
                screenState_ == KarmaScreenState::CREDENTIALS ||
                screenState_ == KarmaScreenState::CLIENTS) {
                screenState_ = previousState_;
                return true;
            }
            if (screenState_ == KarmaScreenState::CLIENT_DETAILS) {
                screenState_ = KarmaScreenState::CLIENTS;
                return true;
            }
            if (screenState_ == KarmaScreenState::CLIENT_TRAFFIC) {
                screenState_ = KarmaScreenState::CLIENT_DETAILS;
                return true;
            }
            if (screenState_ == KarmaScreenState::LISTENING ||
                screenState_ == KarmaScreenState::ACTIVE ||
                screenState_ == KarmaScreenState::SNIFFER) {
                stop();
                return true;
            }
            if (screenState_ == KarmaScreenState::CONFIG) {
                shouldExit_ = true;
                return true;
            }
            return false;
            
        case ',':
        case '<':
            if (screenState_ == KarmaScreenState::CONFIG) {
                switch (configSelection_) {
                    case 0: if (configMode_ > 0) configMode_--; break;
                    case 1: configPortal_ = (configPortal_ == 0) ? 1 : 0; break;
                    case 2: configLogSD_ = false; break;
                }
                return true;
            }
            break;
            
        case 'm':
        case 'M':
            if (screenState_ == KarmaScreenState::CONFIG) {
                configMode_ = (configMode_ + 1) % 3;
                return true;
            }
            break;
            
        case 'p':
        case 'P':
            if (screenState_ == KarmaScreenState::CONFIG && configMode_ == 1) {
                configPortal_ = (configPortal_ == 0) ? 1 : 0;
                return true;
            }
            if (screenState_ == KarmaScreenState::LISTENING ||
                screenState_ == KarmaScreenState::ACTIVE ||
                screenState_ == KarmaScreenState::SNIFFER) {
                previousState_ = screenState_;
                screenState_ = KarmaScreenState::PROBES;
                probeScrollPos_ = 0;
                return true;
            }
            break;
            
        case '/':
        case '>':
            if (screenState_ == KarmaScreenState::CONFIG) {
                switch (configSelection_) {
                    case 0: if (configMode_ < 2) configMode_++; break;
                    case 1: configPortal_ = (configPortal_ == 0) ? 1 : 0; break;
                    case 2: configLogSD_ = true; break;
                }
                return true;
            }
            break;
            
        case 'c':
        case 'C':
            if (screenState_ == KarmaScreenState::SNIFFER) {
                previousState_ = screenState_;
                screenState_ = KarmaScreenState::CLIENTS;
                clientScrollPos_ = 0;
                selectedClientIdx_ = 0;
                return true;
            }
            if (screenState_ == KarmaScreenState::ACTIVE) {
                previousState_ = screenState_;
                screenState_ = KarmaScreenState::CREDENTIALS;
                credentialScrollPos_ = 0;
                return true;
            }
            break;
            
        case ';':
            if (screenState_ == KarmaScreenState::CONFIG) {
                if (configSelection_ > 0) configSelection_--;
                return true;
            }
            if (screenState_ == KarmaScreenState::PROBES && probeScrollPos_ > 0) {
                probeScrollPos_--;
                return true;
            }
            if (screenState_ == KarmaScreenState::CREDENTIALS && credentialScrollPos_ > 0) {
                credentialScrollPos_--;
                return true;
            }
            if (screenState_ == KarmaScreenState::CLIENTS && selectedClientIdx_ > 0) {
                selectedClientIdx_--;
                return true;
            }
            if (screenState_ == KarmaScreenState::CLIENT_TRAFFIC && trafficScrollPos_ > 0) {
                trafficScrollPos_--;
                return true;
            }
            break;
            
        case '.':
            if (screenState_ == KarmaScreenState::CONFIG) {
                if (configSelection_ < CONFIG_OPTION_COUNT - 1) configSelection_++;
                return true;
            }
            if (screenState_ == KarmaScreenState::PROBES) {
                size_t maxScroll = karma_.getProbes().size();
                if (maxScroll > 0 && probeScrollPos_ < maxScroll - 1) {
                    probeScrollPos_++;
                    return true;
                }
            }
            if (screenState_ == KarmaScreenState::CREDENTIALS) {
                size_t maxScroll = karma_.getCredentials().size();
                if (maxScroll > 0 && credentialScrollPos_ < maxScroll - 1) {
                    credentialScrollPos_++;
                    return true;
                }
            }
            if (screenState_ == KarmaScreenState::CLIENTS) {
                auto macs = TrafficProxy::getInstance().getClientMacList();
                if (macs.size() > 0 && selectedClientIdx_ < macs.size() - 1) {
                    selectedClientIdx_++;
                    return true;
                }
            }
            if (screenState_ == KarmaScreenState::CLIENT_TRAFFIC) {
                auto* client = TrafficProxy::getInstance().getClientByMac(selectedClientMac_.c_str());
                if (client && trafficScrollPos_ < client->entries.size()) {
                    trafficScrollPos_++;
                    return true;
                }
            }
            break;
            
        case '\n':
        case '\r':
            if (screenState_ == KarmaScreenState::CONFIG && configSelection_ == CONFIG_OPTION_COUNT - 1) {
                return startWithConfig();
            }
            if (screenState_ == KarmaScreenState::CLIENTS) {
                auto macs = TrafficProxy::getInstance().getClientMacList();
                if (selectedClientIdx_ < macs.size()) {
                    selectedClientMac_ = macs[selectedClientIdx_];
                    screenState_ = KarmaScreenState::CLIENT_DETAILS;
                    return true;
                }
            }
            break;
            
        case 'd':
        case 'D':
            if (screenState_ == KarmaScreenState::CLIENT_DETAILS) {
                trafficScrollPos_ = 0;
                screenState_ = KarmaScreenState::CLIENT_TRAFFIC;
                return true;
            }
            break;
            
        case 's':
        case 'S':
            if (screenState_ == KarmaScreenState::CLIENT_DETAILS) {
                TrafficProxy::getInstance().saveClientTraffic(selectedClientMac_.c_str());
                return true;
            }
            break;
    }
    return false;
}

// =============================================================================
// Helpers
// =============================================================================

const char* KarmaScreen::getModeStr() const {
    switch (configMode_) {
        case 0: return "Listen Only";
        case 1: return "Active+Portal";
        case 2: return "Sniffer";
        default: return "Active+Portal";
    }
}

const char* KarmaScreen::getPortalStr() const {
    return configPortal_ == 0 ? "Generic" : "Google";
}

// =============================================================================
// Rendering
// =============================================================================

void KarmaScreen::render(Canvas& canvas) {
    if (!visible_) return;
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    switch (screenState_) {
        case KarmaScreenState::CONFIG:
            renderConfig(canvas);
            break;
        case KarmaScreenState::LISTENING:
            renderListening(canvas);
            break;
        case KarmaScreenState::ACTIVE:
            renderActive(canvas);
            break;
        case KarmaScreenState::SNIFFER:
            renderSniffer(canvas);
            break;
        case KarmaScreenState::PROBES:
            renderProbes(canvas);
            break;
        case KarmaScreenState::CREDENTIALS:
            renderCredentials(canvas);
            break;
        case KarmaScreenState::CLIENTS:
            renderClients(canvas);
            break;
        case KarmaScreenState::CLIENT_DETAILS:
            renderClientDetails(canvas);
            break;
        case KarmaScreenState::CLIENT_TRAFFIC:
            renderClientTraffic(canvas);
            break;
    }
}

void KarmaScreen::renderConfig(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    
    ui::StatusBar::render(canvas, "Karma AP Config");
    
    int16_t y = 28;
    const int lineHeight = 16;
    int16_t screenWidth = canvas.width();
    
    const char* optionLabels[] = {
        "Mode:",
        "Portal:",
        "Log SD:",
        ">> START <<"
    };
    
    for (int i = 0; i < CONFIG_OPTION_COUNT; i++) {
        bool selected = (i == configSelection_);
        
        if (selected) {
            canvas.fillRect(0, y - 2, screenWidth, lineHeight, theme::BG_SECONDARY());
            canvas.setTextColor(theme::ACCENT());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        canvas.setCursor(4, y);
        canvas.print(optionLabels[i]);
        
        char valueStr[24] = "";
        switch (i) {
            case 0: strncpy(valueStr, getModeStr(), sizeof(valueStr)); break;
            case 1: strncpy(valueStr, getPortalStr(), sizeof(valueStr)); break;
            case 2: strncpy(valueStr, configLogSD_ ? "Yes" : "No", sizeof(valueStr)); break;
        }
        
        if (i < CONFIG_OPTION_COUNT - 1) {
            int valueX = 75;
            canvas.setCursor(valueX, y);
            canvas.print(valueStr);
        }
        
        y += lineHeight;
    }
    
    // Footer hints
    static char modeBuf[32];
    snprintf(modeBuf, sizeof(modeBuf), "Mode - %s", getModeStr());
    
    footerHints_.setHints({
        {'M', modeBuf, true},
        {'P', "Portal", configMode_ == 1}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderListening(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& stats = karma_.getStats();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%lu probes", stats.probesCaptured);
    ui::StatusBar::render(canvas, "Listening...", centerStr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
    
    int16_t y = 28;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(10, y);
    canvas.print("Duration: ");
    uint32_t duration = stats.getDuration() / 1000;
    canvas.printf("%02lu:%02lu", duration / 60, duration % 60);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.print("Probes: ");
    canvas.setTextColor(stats.probesCaptured > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.probesCaptured);
    y += 14;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(10, y);
    canvas.printf("Clients: %lu  SSIDs: %lu", stats.uniqueClients, stats.uniqueSSIDs);
    y += 20;
    
    const auto& probes = karma_.getProbes();
    if (!probes.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, y);
        canvas.print("Latest: ");
        canvas.setTextColor(theme::ACCENT());
        
        const char* ssid = probes.back().ssid;
        if (strlen(ssid) > 20) {
            char truncated[21];
            strncpy(truncated, ssid, 17);
            truncated[17] = '.';
            truncated[18] = '.';
            truncated[19] = '.';
            truncated[20] = '\0';
            canvas.print(truncated);
        } else {
            canvas.print(ssid);
        }
    }
    
    // Footer hints
    footerHints_.setHints({
        {'`', "Stop", true},
        {'p', "Probes", true}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderActive(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& stats = karma_.getStats();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%lu clients", stats.clientsConnected);
    ui::StatusBar::render(canvas, karma_.getCurrentSSID(), centerStr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
    
    int16_t y = 28;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(10, y);
    canvas.print("Duration: ");
    uint32_t duration = stats.getDuration() / 1000;
    canvas.printf("%02lu:%02lu", duration / 60, duration % 60);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.print("Connected: ");
    canvas.setTextColor(stats.clientsConnected > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.clientsConnected);
    y += 14;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(10, y);
    canvas.print("Credentials: ");
    canvas.setTextColor(stats.credentialsCaptured > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.credentialsCaptured);
    y += 14;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.printf("Probes: %lu  SSIDs: %lu", stats.probesCaptured, stats.uniqueSSIDs);
    
    // Footer hints
    footerHints_.setHints({
        {'`', "Stop", true},
        {'p', "Probes", true},
        {'c', "Creds", true}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderSniffer(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& proxy = TrafficProxy::getInstance();
    const auto& stats = proxy.getStats();
    const auto& karmaStats = karma_.getStats();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%lu HTTP", stats.httpRequests);
    ui::StatusBar::render(canvas, karma_.getCurrentSSID(), centerStr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
    
    int16_t y = 28;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(10, y);
    canvas.print("Duration: ");
    uint32_t duration = karmaStats.getDuration() / 1000;
    canvas.printf("%02lu:%02lu", duration / 60, duration % 60);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.printf("Cli:%lu Prb:", karmaStats.clientsConnected);
    canvas.setTextColor(karmaStats.probesCaptured > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(karmaStats.probesCaptured);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.printf(" Pkt:%lu", stats.totalPackets);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.print("DNS: ");
    canvas.setTextColor(stats.dnsQueries > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.dnsQueries);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print("  HTTP: ");
    canvas.setTextColor(stats.httpRequests > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.httpRequests);
    y += 14;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.printf("Domains: %lu", stats.uniqueDomains);
    y += 16;
    
    const auto& domains = proxy.getUniqueDomains();
    if (!domains.empty()) {
        canvas.setTextColor(theme::ACCENT());
        size_t start = domains.size() > 3 ? domains.size() - 3 : 0;
        for (size_t i = start; i < domains.size() && y < config::SCREEN_HEIGHT - 25; i++) {
            canvas.setCursor(15, y);
            if (strlen(domains[i]) > 28) {
                char truncated[29];
                strncpy(truncated, domains[i], 25);
                truncated[25] = '.';
                truncated[26] = '.';
                truncated[27] = '.';
                truncated[28] = '\0';
                canvas.print(truncated);
            } else {
                canvas.print(domains[i]);
            }
            y += 10;
        }
    }
    
    // Footer hints
    footerHints_.setHints({
        {'`', "Stop", true},
        {'p', "Probes", true},
        {'c', "Clients", true}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderProbes(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& probes = karma_.getProbes();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%zu items", probes.size());
    ui::StatusBar::render(canvas, "Probes", centerStr);
    
    int16_t y = 28;
    const int maxVisible = 5;
    
    if (probes.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No probes captured");
    } else {
        for (size_t i = probeScrollPos_; 
             i < probes.size() && i < probeScrollPos_ + maxVisible; 
             i++) {
            const auto& probe = probes[i];
            
            canvas.setTextColor(theme::ACCENT());
            canvas.setCursor(10, y);
            
            if (strlen(probe.ssid) > 18) {
                char truncated[19];
                strncpy(truncated, probe.ssid, 15);
                truncated[15] = '.';
                truncated[16] = '.';
                truncated[17] = '.';
                truncated[18] = '\0';
                canvas.print(truncated);
            } else {
                canvas.print(probe.ssid);
            }
            
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.printf(" x%d", probe.count);
            y += 10;
            
            canvas.setCursor(20, y);
            canvas.printf("%02X:%02X:..:%02X %ddBm",
                probe.clientMAC[0], probe.clientMAC[1], 
                probe.clientMAC[5], probe.rssi);
            y += 14;
        }
    }
    
    if (probeScrollPos_ > 0) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, 25);
        canvas.print("^");
    }
    if (probeScrollPos_ + maxVisible < probes.size()) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, config::SCREEN_HEIGHT - 30);
        canvas.print("v");
    }
    
    // Footer hints
    footerHints_.setHints({}); // Universal hints back/nav are enough
    footerHints_.render(canvas);
}

void KarmaScreen::renderCredentials(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& creds = karma_.getCredentials();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%zu items", creds.size());
    ui::StatusBar::render(canvas, "Credentials", centerStr);
    
    int16_t y = 28;
    const int maxVisible = 4;
    
    if (creds.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No credentials captured");
    } else {
        for (size_t i = credentialScrollPos_; 
             i < creds.size() && i < credentialScrollPos_ + maxVisible; 
             i++) {
            const auto& cred = creds[i];
            
            canvas.setTextColor(theme::ACCENT());
            canvas.setCursor(5, y);
            canvas.print("Credential #");
            canvas.print((int)(i + 1));
            y += 10;
            
            canvas.setTextColor(theme::SUCCESS());
            canvas.setCursor(10, y);
            canvas.print(cred.username);
            y += 10;
            
            canvas.setTextColor(theme::WARNING());
            canvas.setCursor(10, y);
            canvas.print(cred.password);
            y += 12;
        }
    }
    
    // Footer hints
    footerHints_.setHints({});
    footerHints_.render(canvas);
}

void KarmaScreen::renderClients(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    auto macs = TrafficProxy::getInstance().getClientMacList();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%zu clients", macs.size());
    ui::StatusBar::render(canvas, "Clients", centerStr);
    
    int16_t y = 28;
    const int maxVisible = 5;
    int16_t screenWidth = canvas.width();
    
    if (macs.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No clients connected");
    } else {
        for (size_t i = clientScrollPos_; 
             i < macs.size() && i < clientScrollPos_ + maxVisible; 
             i++) {
            bool selected = (i == selectedClientIdx_);
            
            if (selected) {
                canvas.fillRect(0, y - 2, screenWidth, 18, theme::BG_SECONDARY());
                canvas.setTextColor(theme::ACCENT());
            } else {
                canvas.setTextColor(theme::TEXT_PRIMARY());
            }
            
            canvas.setCursor(10, y);
            canvas.print(macs[i].c_str());
            
            auto* client = TrafficProxy::getInstance().getClientByMac(macs[i].c_str());
            if (client) {
                canvas.setTextColor(theme::TEXT_SECONDARY());
                canvas.printf(" (%zu pkts)", client->entries.size());
            }
            
            y += 18;
        }
    }
    
    // Footer hints
    footerHints_.setHints({
        {'\r', "Details", !macs.empty()},
        {'`', "Back", true}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderClientDetails(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    
    auto* client = TrafficProxy::getInstance().getClientByMac(selectedClientMac_.c_str());
    
    ui::StatusBar::render(canvas, "Client Details");
    
    int16_t y = 28;
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(10, y);
    canvas.print(selectedClientMac_.c_str());
    y += 16;
    
    if (client) {
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(10, y);
        canvas.printf("Packets: %zu", client->entries.size());
        y += 14;
        
        // Count DNS and HTTP
        size_t dnsCount = 0, httpCount = 0;
        for (const auto& entry : client->entries) {
            if (entry.type == TrafficEntryType::DNS) dnsCount++;
            else if (entry.type == TrafficEntryType::HTTP) httpCount++;
        }
        
        canvas.setCursor(10, y);
        canvas.printf("DNS: %zu  HTTP: %zu", dnsCount, httpCount);
        y += 20;
        
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, y);
        canvas.print("D: View traffic");
        y += 12;
        canvas.setCursor(10, y);
        canvas.print("S: Save to SD");
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, y);
        canvas.print("Client disconnected");
    }
    
    // Footer hints
    footerHints_.setHints({
        {'d', "Traffic", client != nullptr},
        {'s', "Save", client != nullptr},
        {'`', "Back", true}
    });
    footerHints_.render(canvas);
}

void KarmaScreen::renderClientTraffic(Canvas& canvas) {
    canvas.fillScreen(theme::BG_PRIMARY());
    
    auto* client = TrafficProxy::getInstance().getClientByMac(selectedClientMac_.c_str());
    
    ui::StatusBar::render(canvas, "Traffic Feed");
    
    int16_t y = 28;
    const int maxVisible = 6;
    
    if (!client || client->entries.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No traffic captured");
    } else {
        for (size_t i = trafficScrollPos_; 
             i < client->entries.size() && i < trafficScrollPos_ + maxVisible; 
             i++) {
            const auto& entry = client->entries[i];
            
            canvas.setTextColor(entry.type == TrafficEntryType::DNS ? theme::INFO() : theme::SUCCESS());
            canvas.setCursor(5, y);
            
            if (strlen(entry.data) > 30) {
                char truncated[31];
                strncpy(truncated, entry.data, 27);
                truncated[27] = '.';
                truncated[28] = '.';
                truncated[29] = '.';
                truncated[30] = '\0';
                canvas.print(truncated);
            } else {
                canvas.print(entry.data);
            }
            y += 14;
        }
    }
    
    // Footer hints
    footerHints_.setHints({});
    footerHints_.render(canvas);
}

} // namespace adversary
