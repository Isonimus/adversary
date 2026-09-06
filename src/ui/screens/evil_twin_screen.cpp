/**
 * @file evil_twin_screen.cpp
 * @brief Evil Twin Screen implementation
 */

#include "evil_twin_screen.h"
#include "core/event_bus.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

EvilTwinScreen::EvilTwinScreen() {
    footerHints_.setHints({
        {'`', "Back"},
        {';', "Nav"},
        {'/', "Edit"},
        {'\n', "Go"}
    });
}

void EvilTwinScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    screenState_ = EvilTwinScreenState::CONFIG;
    configSelection_ = 0;
    configScrollOffset_ = 0;
    
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
}

void EvilTwinScreen::hide() {
    // Unsubscribe from EventBus
    if (credentialHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(credentialHandlerId_);
        credentialHandlerId_ = 0;
    }
    
    if (isRunning()) {
        stop();
    }
    visible_ = false;
}

void EvilTwinScreen::setTarget(const char* ssid, const uint8_t* bssid, uint8_t channel) {
    evilTwin_.setTarget(ssid, bssid, channel);
}

bool EvilTwinScreen::start() {
    if (evilTwin_.start()) {
        screenState_ = EvilTwinScreenState::RUNNING;
        return true;
    }
    return false;
}

void EvilTwinScreen::stop() {
    if (TrafficProxy::getInstance().isRunning()) {
        TrafficProxy::getInstance().stop();
    }
    evilTwin_.stop();
    screenState_ = EvilTwinScreenState::CONFIG;
}

bool EvilTwinScreen::startSniffer() {
#ifdef ESP32
    if (WiFi.status() != WL_CONNECTED) {
        showErrorToast("No WiFi! Connect first");
        return false;
    }
#endif
    
    ap::CaptivePortalConfig config;
    config.enableCaptivePortal = false;
    config.logToSD = true;
    
    evilTwin_.setPortalConfig(config);
    if (evilTwin_.start()) {
        if (TrafficProxy::getInstance().start()) {
            screenState_ = EvilTwinScreenState::SNIFFER;
            return true;
        }
        showErrorToast("Traffic proxy failed!");
        evilTwin_.stop();
    }
    return false;
}

void EvilTwinScreen::update() {
    if (!visible_) return;
    evilTwin_.update();
    
    // For live-counter views (RUNNING/SNIFFER) refresh at ~2 Hz.
    // Static views (CONFIG, CREDENTIALS, CLIENTS) only redraw on input.
    if (screenState_ == EvilTwinScreenState::RUNNING ||
        screenState_ == EvilTwinScreenState::SNIFFER) {
        uint32_t now = millis();
        if (now - lastRedrawMs_ >= 500) {
            needsRedraw_ = true;
            lastRedrawMs_ = now;
        }
    }
}

bool EvilTwinScreen::isRunning() const {
    return evilTwin_.isRunning();
}

void EvilTwinScreen::forceStopPortal() {
    evilTwin_.forceStopPortal();
}

bool EvilTwinScreen::startWithConfig() {
    const char* ssid = nullptr;
    if (configUseFixedSSID_ && configFixedSSID_[0] != '\0') {
        ssid = configFixedSSID_;
    } else {
        ssid = evilTwin_.getTargetSSID();
    }
    
    if (ssid == nullptr || ssid[0] == '\0') {
        showErrorToast("No SSID set!");
        return false;
    }
    
    if (configUseFixedSSID_ && configFixedSSID_[0] != '\0') {
        uint8_t bssid[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
        evilTwin_.setTarget(configFixedSSID_, bssid, 6);
    }
    
    ap::CaptivePortalConfig portalConfig;
    portalConfig.logToSD = configLogSD_;
    
    evilTwin_.setDeauthEnabled(configDeauth_, getDeauthIntervalMs());
    
    if (configMode_ == 0) {
        portalConfig.pageType = configPortal_ == 0 ? 
            ap::PortalPageType::GENERIC_LOGIN : ap::PortalPageType::SOCIAL_GOOGLE;
        portalConfig.setTitle(configPortal_ == 0 ? "Network Login" : "Google Sign-In");
        portalConfig.setSuccessMessage("Connected! You may now browse.");
        portalConfig.enableCaptivePortal = true;
        
        evilTwin_.setPortalConfig(portalConfig);
        
        if (evilTwin_.start()) {
            screenState_ = EvilTwinScreenState::RUNNING;
            return true;
        }
    } else {
#ifdef ESP32
        if (WiFi.status() != WL_CONNECTED) {
            showErrorToast("No WiFi! Connect first");
            return false;
        }
#endif
        portalConfig.enableCaptivePortal = false;
        
        evilTwin_.setPortalConfig(portalConfig);
        
        if (evilTwin_.start()) {
            if (TrafficProxy::getInstance().start()) {
                screenState_ = EvilTwinScreenState::SNIFFER;
                return true;
            }
            showErrorToast("Traffic proxy failed!");
            evilTwin_.stop();
        }
    }
    
    return false;
}

const char* EvilTwinScreen::getModeStr() const {
    return configMode_ == 0 ? "Portal" : "Sniffer";
}

const char* EvilTwinScreen::getPortalStr() const {
    return configPortal_ == 0 ? "Generic" : "Google";
}

const char* EvilTwinScreen::getDeauthIntervalStr() const {
    switch (configDeauthInterval_) {
        case 0: return "5 sec";
        case 1: return "10 sec";
        case 2: return "30 sec";
        default: return "10 sec";
    }
}

uint32_t EvilTwinScreen::getDeauthIntervalMs() const {
    switch (configDeauthInterval_) {
        case 0: return 5000;
        case 1: return 10000;
        case 2: return 30000;
        default: return 10000;
    }
}

const char* EvilTwinScreen::getSSIDDisplay() const {
    if (configUseFixedSSID_ && configFixedSSID_[0] != '\0') {
        return configFixedSSID_;
    }
    return evilTwin_.getTargetSSID()[0] != '\0' ? 
           evilTwin_.getTargetSSID() : "(from scanner)";
}

// =============================================================================
// Input Handling
// =============================================================================

bool EvilTwinScreen::handleInput(char key) {
    if (!visible_) return false;
    needsRedraw_ = true;  // any key press triggers redraw
    
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);
            return true;
        }
    }
    
    // Handle popup input first
    if (showingSsidPopup_) {
        ssidPopup_.handleInput(key);
        if (!ssidPopup_.isVisible()) {
            showingSsidPopup_ = false;
        }
        return true;
    }
    
    switch (key) {
        case '`':  // Back/ESC
            if (screenState_ == EvilTwinScreenState::CREDENTIALS ||
                screenState_ == EvilTwinScreenState::CLIENTS) {
                screenState_ = (EvilTwinScreenState::SNIFFER == screenState_ || 
                               (EvilTwinScreenState::RUNNING == screenState_)) ? 
                               screenState_ : // Should not happen
                               (TrafficProxy::getInstance().isRunning() ?
                                EvilTwinScreenState::SNIFFER : EvilTwinScreenState::RUNNING);
                // Fix: previous state logic was a bit flawed, let's just go back to main attack screen
                if (TrafficProxy::getInstance().isRunning()) screenState_ = EvilTwinScreenState::SNIFFER;
                else screenState_ = EvilTwinScreenState::RUNNING;
                return true;
            }
            if (screenState_ == EvilTwinScreenState::RUNNING ||
                screenState_ == EvilTwinScreenState::SNIFFER) {
                stop();
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                shouldExit_ = true;
                return true;
            }
            return false;
            
        case ',':
        case '<':
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                switch (configSelection_) {
                    case 0: if (configMode_ > 0) configMode_--; break;
                    case 1: configPortal_ = 0; break;
                    case 2: 
                        configUseFixedSSID_ = false;
                        memset(configFixedSSID_, 0, sizeof(configFixedSSID_));
                        break;
                    case 3: configDeauth_ = false; break;
                    case 4: if (configDeauthInterval_ > 0) configDeauthInterval_--; break;
                    case 5: configLogSD_ = false; break;
                }
                return true;
            }
            break;
            
        case '/':
        case '>':
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                switch (configSelection_) {
                    case 0: if (configMode_ < 1) configMode_++; break;
                    case 1: configPortal_ = (configPortal_ == 0) ? 1 : 0; break;
                    case 2: {
                        ssidPopup_.setOnSubmit([this](const char* text) {
                            strncpy(configFixedSSID_, text, sizeof(configFixedSSID_) - 1);
                            configFixedSSID_[sizeof(configFixedSSID_) - 1] = '\0';
                            if (configFixedSSID_[0] != '\0') {
                                configUseFixedSSID_ = true;
                            }
                        });
                        ssidPopup_.setOnCancel([this]() {});
                        ssidPopup_.show("Enter SSID:", configFixedSSID_[0] ? configFixedSSID_ : "", false, 32);
                        showingSsidPopup_ = true;
                        break;
                    }
                    case 3: configDeauth_ = true; break;
                    case 4: if (configDeauthInterval_ < 2) configDeauthInterval_++; break;
                    case 5: configLogSD_ = true; break;
                }
                return true;
            }
            break;

        case 'm':
        case 'M':
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                configMode_ = (configMode_ == 0) ? 1 : 0;
                return true;
            }
            break;

        case 'p':
        case 'P':
            if (screenState_ == EvilTwinScreenState::CONFIG && configMode_ == 0) {
                configPortal_ = (configPortal_ == 0) ? 1 : 0;
                return true;
            }
            break;

        case 's':
        case 'S':
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                ssidPopup_.setOnSubmit([this](const char* text) {
                    strncpy(configFixedSSID_, text, sizeof(configFixedSSID_) - 1);
                    configFixedSSID_[sizeof(configFixedSSID_) - 1] = '\0';
                    if (configFixedSSID_[0] != '\0') {
                        configUseFixedSSID_ = true;
                    } else {
                        configUseFixedSSID_ = false;
                    }
                });
                ssidPopup_.setOnCancel([this]() {});
                ssidPopup_.show("Enter SSID:", configFixedSSID_[0] ? configFixedSSID_ : "", false, 32);
                showingSsidPopup_ = true;
                return true;
            }
            break;
            
        case 'c':
        case 'C':
            if (screenState_ == EvilTwinScreenState::RUNNING) {
                screenState_ = EvilTwinScreenState::CREDENTIALS;
                credentialScrollPos_ = 0;
                return true;
            }
            break;
            
        case 'l':
        case 'L':
            if (screenState_ == EvilTwinScreenState::RUNNING ||
                screenState_ == EvilTwinScreenState::SNIFFER) {
                screenState_ = EvilTwinScreenState::CLIENTS;
                clientScrollPos_ = 0;
                return true;
            }
            break;
            
        case ';':  // Up
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                if (configSelection_ > 0) {
                    configSelection_--;
                    if (configSelection_ < configScrollOffset_) {
                        configScrollOffset_ = configSelection_;
                    }
                }
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CREDENTIALS && credentialScrollPos_ > 0) {
                credentialScrollPos_--;
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CLIENTS && clientScrollPos_ > 0) {
                clientScrollPos_--;
                return true;
            }
            break;
            
        case '.':  // Down
            if (screenState_ == EvilTwinScreenState::CONFIG) {
                if (configSelection_ < CONFIG_OPTION_COUNT - 1) {
                    configSelection_++;
                }
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CREDENTIALS) {
                size_t maxScroll = evilTwin_.getCredentials().size();
                if (credentialScrollPos_ < maxScroll - 1) {
                    credentialScrollPos_++;
                    return true;
                }
            }
            break;
            
        case '\n':
        case '\r':
            if (screenState_ == EvilTwinScreenState::CONFIG && 
                configSelection_ == CONFIG_OPTION_COUNT - 1) {
                return startWithConfig();
            }
            break;
    }
    return false;
}

// =============================================================================
// Rendering
// =============================================================================

void EvilTwinScreen::render(Canvas& canvas) {
    if (!visible_) return;
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
#ifdef ESP32
    switch (screenState_) {
        case EvilTwinScreenState::CONFIG:
            renderConfig(canvas);
            break;
        case EvilTwinScreenState::RUNNING:
            renderRunning(canvas);
            break;
        case EvilTwinScreenState::SNIFFER:
            renderSniffer(canvas);
            break;
        case EvilTwinScreenState::CREDENTIALS:
            renderCredentials(canvas);
            break;
        case EvilTwinScreenState::CLIENTS:
            renderClients(canvas);
            break;
    }
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::renderConfig(Canvas& canvas) {
#ifdef ESP32
    if (showingSsidPopup_) {
        ssidPopup_.render(canvas);
        return;
    }
    
    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "Evil Twin Config");
    
    int16_t y = 26;
    const int lineHeight = 15;
    int16_t screenWidth = canvas.width();
    
    // Target info
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Target: ");
    canvas.setTextColor(theme::ACCENT());
    const char* ssidDisplay = getSSIDDisplay();
    if (strlen(ssidDisplay) > 16) {
        char truncated[17];
        strncpy(truncated, ssidDisplay, 13);
        strcpy(truncated + 13, "...");
        canvas.print(truncated);
    } else {
        canvas.print(ssidDisplay);
    }
    y += lineHeight;
    
    // Calculate visible items
    const int16_t footerHeight = 20;
    const int16_t headerHeight = 26 + lineHeight;
    const int16_t availableHeight = config::SCREEN_HEIGHT - headerHeight - footerHeight - 4;
    const int maxVisible = availableHeight / lineHeight;
    
    // Update scroll offset
    if (configSelection_ >= configScrollOffset_ + maxVisible) {
        configScrollOffset_ = configSelection_ - maxVisible + 1;
    }
    
    const char* optionLabels[] = {
        "Mode:", "Portal:", "Fixed SSID:", "Deauth:", "Interval:", "Log SD:", ">> START <<"
    };
    
    // Draw scroll indicator if needed
    if (configScrollOffset_ > 0) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, y - 2);
        canvas.print("^");
    }
    
    for (int i = configScrollOffset_; i < CONFIG_OPTION_COUNT && i < configScrollOffset_ + maxVisible; i++) {
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
            case 1: 
                strncpy(valueStr, configMode_ == 0 ? getPortalStr() : "---", sizeof(valueStr));
                break;
            case 2: 
                if (configUseFixedSSID_ && configFixedSSID_[0]) {
                    if (strlen(configFixedSSID_) > 10) {
                        strncpy(valueStr, configFixedSSID_, 7);
                        strcpy(valueStr + 7, "...");
                    } else {
                        strncpy(valueStr, configFixedSSID_, sizeof(valueStr));
                    }
                } else {
                    strncpy(valueStr, configUseFixedSSID_ ? "[Edit]" : "OFF", sizeof(valueStr));
                }
                break;
            case 3: strncpy(valueStr, configDeauth_ ? "ON" : "OFF", sizeof(valueStr)); break;
            case 4: 
                strncpy(valueStr, configDeauth_ ? getDeauthIntervalStr() : "---", sizeof(valueStr));
                break;
            case 5: strncpy(valueStr, configLogSD_ ? "Yes" : "No", sizeof(valueStr)); break;
        }
        
        if (i < CONFIG_OPTION_COUNT - 1) {
            canvas.setCursor(80, y);
            canvas.print(valueStr);
        }
        
        y += lineHeight;
    }
    
    // Draw scroll down indicator
    if (configScrollOffset_ + maxVisible < CONFIG_OPTION_COUNT) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, y - lineHeight + 2);
        canvas.print("v");
    }
    
    // Footer hints
    static char modeBuf[32];
    snprintf(modeBuf, sizeof(modeBuf), "Mode - %s", getModeStr());

    footerHints_.setHints({
        {'M', modeBuf, true},
        {'P', "Portal", configMode_ == 0},
        {'S', "SSID", true},
        {'\r', "Start", configSelection_ == CONFIG_OPTION_COUNT - 1}
    });
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::renderRunning(Canvas& canvas) {
#ifdef ESP32
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& stats = evilTwin_.getStats();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%lu clients", (unsigned long)stats.clientsConnected);
    ui::StatusBar::render(canvas, evilTwin_.getTargetSSID(), centerStr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
    
    int16_t y = 28;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(10, y);
    canvas.print("Duration: ");
    uint32_t duration = stats.getDuration() / 1000;
    canvas.printf("%02lu:%02lu", (unsigned long)(duration / 60), (unsigned long)(duration % 60));
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.print("Clients: ");
    canvas.setTextColor(stats.clientsConnected > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.clientsConnected);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.printf(" (%lu total)", (unsigned long)stats.totalConnections);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print("Credentials: ");
    canvas.setTextColor(stats.credentialsCaptured > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(stats.credentialsCaptured);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.printf("DNS: %lu  HTTP: %lu", (unsigned long)stats.dnsQueries, (unsigned long)stats.httpRequests);
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.printf("Deauths: %lu", (unsigned long)stats.deauthsSent);
    
    // Footer hints
    footerHints_.setHints({
        {'`', "Stop", true},
        {'c', "Creds", true},
        {'l', "List", true}
    });
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::renderSniffer(Canvas& canvas) {
#ifdef ESP32
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& proxy = TrafficProxy::getInstance();
    const auto& stats = proxy.getStats();
    const auto& evilStats = evilTwin_.getStats();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%lu HTTP", (unsigned long)stats.httpRequests);
    ui::StatusBar::render(canvas, evilTwin_.getTargetSSID(), centerStr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
    
    int16_t y = 28;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(10, y);
    canvas.print("Duration: ");
    uint32_t duration = evilStats.getDuration() / 1000;
    canvas.printf("%02lu:%02lu", (unsigned long)(duration / 60), (unsigned long)(duration % 60));
    y += 14;
    
    canvas.setCursor(10, y);
    canvas.printf("Clients: %lu  Pkts: %lu",
                   (unsigned long)evilStats.clientsConnected, (unsigned long)stats.totalPackets);
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
    canvas.printf("Domains: %lu", (unsigned long)stats.uniqueDomains);
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
        {'l', "Clients", true}
    });
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::renderCredentials(Canvas& canvas) {
#ifdef ESP32
    canvas.fillScreen(theme::BG_PRIMARY());
    const auto& creds = evilTwin_.getCredentials();
    
    char centerStr[16];
    snprintf(centerStr, sizeof(centerStr), "%zu items", creds.size());
    ui::StatusBar::render(canvas, "Credentials", centerStr);
    
    int16_t y = 28;
    const int maxVisible = 5;
    
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
            canvas.setCursor(10, y);
            canvas.print(cred.username);
            y += 10;
            
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.setCursor(20, y);
            canvas.print(cred.password);
            y += 14;
        }
    }
    
    // Scroll indicators
    if (credentialScrollPos_ > 0) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, 25);
        canvas.print("^");
    }
    if (credentialScrollPos_ + maxVisible < creds.size()) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(config::SCREEN_WIDTH - 10, config::SCREEN_HEIGHT - 30);
        canvas.print("v");
    }
    
    // Footer hints
    footerHints_.setHints({});
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::renderClients(Canvas& canvas) {
#ifdef ESP32
    canvas.fillScreen(theme::BG_PRIMARY());
    ap::APClient clients[8];
    uint8_t clientCount = evilTwin_.getClients(clients, 8);
    
    char centerStr[8];
    snprintf(centerStr, sizeof(centerStr), "%d", clientCount);
    ui::StatusBar::render(canvas, "Clients", centerStr);
    
    int16_t y = 28;
    
    if (clientCount == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No clients connected");
    } else {
        for (uint8_t i = 0; i < clientCount && y < config::SCREEN_HEIGHT - 30; i++) {
            const auto& client = clients[i];
            
            char macStr[18];
            utils::formatMacBytes(client.mac, macStr, sizeof(macStr));
            canvas.setTextColor(theme::TEXT_PRIMARY());
            canvas.setCursor(10, y);
            canvas.print(macStr);
            
            canvas.setTextColor(theme::TEXT_SECONDARY());
            canvas.printf(" %ddBm", client.rssi);
            y += 14;
        }
    }
    
    // Footer hints
    footerHints_.setHints({});
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

} // namespace adversary
