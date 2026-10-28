/**
 * @file evil_twin_screen.cpp
 * @brief Evil Twin Screen implementation
 */

#include "evil_twin_screen.h"
#include "ui/components/action_button.h"
#include "ui/components/config_row.h"
#include "ui/components/config_input.h"
#include "ui/screen_manager.h"
#include "core/event_bus.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

EvilTwinScreen::EvilTwinScreen() {
    credentialList_.setScrollOnly(true);  // read-only harvested-credentials log (ADR-0133)
    footerHints_.setHints({
        {'`', "Back"},
        {';', "Nav"},
        {'\n', "Start"}
    });
}

void EvilTwinScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    screenState_ = EvilTwinScreenState::CONFIG;
    configList_.setItemCount(0);  // reset selection + scroll to the top

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
    // Reclaim the captive-portal DNS/HTTP server heap on teardown, not just on an
    // explicit stop — hide() is the screen's full lifecycle teardown. Idempotent: no-ops
    // when no portal is up, so a plain back-to-menu exit is safe.
    forceStopPortal();
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
           evilTwin_.getTargetSSID() : "(select from scanner)";
}

// =============================================================================
// Input Handling
// =============================================================================

bool EvilTwinScreen::handleInput(char key) {
    if (!visible_) return false;
    needsRedraw_ = true;  // any key press triggers redraw

    // The client ActionMenu is modal: while open it owns every key (nav, select, ESC), so no
    // underlying list navigation or footer dispatch leaks through (ADR-0132).
    if (clientActionMenu_.isVisible()) {
        clientActionMenu_.handleInput(key);
        return true;
    }

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

    // Canonical config-form input in the CONFIG state, from the shared classifier (ADR-0107).
    // Row 0 (Target) is the navigation row -> WiFi scanner; the last row is the terminal START
    // button; the rest (including Fixed SSID, whose EditNext opens the SSID popup) are value
    // rows. Letter hotkeys (m/p/s), back, and the other states fall through below.
    if (screenState_ == EvilTwinScreenState::CONFIG) {
        const int sel = configList_.selectedIndex();
        const ui::ConfigRowKind kind =
            (sel == 0)                        ? ui::ConfigRowKind::Navigation :
            (sel == CONFIG_OPTION_COUNT - 1)  ? ui::ConfigRowKind::Terminal :
                                                ui::ConfigRowKind::Value;
        switch (ui::classifyConfigKey(key, kind)) {
            case ui::ConfigKeyAction::NavUp:
                configList_.setItemCount(CONFIG_OPTION_COUNT);
                configList_.moveUp();  // wraps to last; model keeps the selection on screen
                return true;
            case ui::ConfigKeyAction::NavDown:
                configList_.setItemCount(CONFIG_OPTION_COUNT);
                configList_.moveDown();  // wraps to first; model keeps the selection on screen
                return true;
            case ui::ConfigKeyAction::EditPrev:
                switch (sel) {
                    case 0: break;  // Target: navigation row, nothing to edit in place (ADR-0092)
                    case 1: if (configMode_ > 0) configMode_--; break;
                    case 2: configPortal_ = 0; break;
                    case 3:
                        configUseFixedSSID_ = false;
                        memset(configFixedSSID_, 0, sizeof(configFixedSSID_));
                        break;
                    case 4: configDeauth_ = false; break;
                    case 5: if (configDeauthInterval_ > 0) configDeauthInterval_--; break;
                    case 6: configLogSD_ = false; break;
                }
                return true;
            case ui::ConfigKeyAction::EditNext:  // also ENTER on a value row (never inert, ADR-0092)
                switch (sel) {
                    case 0: break;  // Target: navigation row, nothing to edit in place (ADR-0092)
                    case 1: if (configMode_ < 1) configMode_++; break;
                    case 2: configPortal_ = (configPortal_ == 0) ? 1 : 0; break;
                    case 3: {
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
                    case 4:
                        // Warn on enable: deauth against the real AP is the known
                        // stability risk in this attack (drops clients, can wedge
                        // the softAP). Fire only on the off->on edge, not on repeat.
                        if (!configDeauth_) {
                            ToastManager::getInstance().show(
                                "Deauth may destabilize the AP & drop clients",
                                ToastType::WARNING, ToastPriority::PRIORITY_MEDIUM, 3000);
                        }
                        configDeauth_ = true;
                        break;
                    case 5: if (configDeauthInterval_ < 2) configDeauthInterval_++; break;
                    case 6: configLogSD_ = true; break;
                }
                return true;
            case ui::ConfigKeyAction::Activate:
                return startWithConfig();  // ENTER on the terminal START row
            case ui::ConfigKeyAction::Navigate:
                // Target row: route to the WiFi scanner to pick a target; it returns via
                // ATTACK_TARGET_SELECTED -> setTarget (like handshake, ADR-0092).
                ScreenManager::getInstance().navigateTo(ScreenId::SCANNER);
                return true;
            case ui::ConfigKeyAction::Popup:
            case ui::ConfigKeyAction::None:
                break;  // no dedicated popup row (Fixed SSID edits via EditNext); fall through
        }
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
                credentialList_.setItemCount(0);  // reset scroll to the top
                return true;
            }
            break;
            
        case 'l':
        case 'L':
            if (screenState_ == EvilTwinScreenState::RUNNING ||
                screenState_ == EvilTwinScreenState::SNIFFER) {
                screenState_ = EvilTwinScreenState::CLIENTS;
                clientList_.setSelectedIndex(0);
                return true;
            }
            break;
            
        case '\n':  // ENTER: open the per-client action menu (CLIENTS view only)
        case '\r':
            if (screenState_ == EvilTwinScreenState::CLIENTS) {
                openClientActionMenu();
                return true;
            }
            break;

        case ';':  // Up
            if (screenState_ == EvilTwinScreenState::CREDENTIALS) {
                // Scroll-only log: the model clamps at the top (no wrap), ADR-0133.
                credentialList_.setItemCount((int)evilTwin_.getCredentials().size());
                credentialList_.moveUp();
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CLIENTS) {
                // The model wraps at the ends; render feeds it the live client count each frame.
                clientList_.moveUp();
                return true;
            }
            break;

        case '.':  // Down
            if (screenState_ == EvilTwinScreenState::CREDENTIALS) {
                // Scroll-only log: the model clamps at the bottom (no wrap), ADR-0133.
                credentialList_.setItemCount((int)evilTwin_.getCredentials().size());
                credentialList_.moveDown();
                return true;
            }
            if (screenState_ == EvilTwinScreenState::CLIENTS) {
                clientList_.moveDown();
                return true;
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
    
    // Target is now the first selectable list row (ENTER routes to the scanner, ADR-0092),
    // not a separate header, so the list starts right below the status bar.
    const int16_t listTop = 26;
    const int16_t availableHeight = config::SCREEN_HEIGHT - listTop - ui::FOOTER_HEIGHT - 4;

    const char* optionLabels[] = {
        "Target:", "Mode:", "Portal:", "Fixed SSID:", "Deauth:", "Interval:", "Log SD:"
    };

    // The shared component owns scroll, the proportional thumb, the selection band and keeping the
    // cursor on screen (replaces the hand-rolled offset + the ^/v glyphs); the callback paints one
    // row (the terminal start row is the shared ActionButton, slice-0089).
    configList_.setItemCount(CONFIG_OPTION_COUNT);
    ScrollableList::render(canvas, configList_, 0, listTop, canvas.width(), availableHeight,
        theme::LIST_ROW_HEIGHT, [this, &optionLabels](Canvas& c, int idx, int16_t rowY, bool selected) {
            if (idx == CONFIG_OPTION_COUNT - 1) {
                ui::ActionButton::render(c, rowY, "START TWIN", selected);
                return;
            }
            char valueStr[28] = "";
            switch (idx) {
                case 0: {
                    // Target row value: the current SSID, or the scanner placeholder. It shares
                    // the row with the "Target:" label (7 chars from x=8); at FONT_CELL_WIDTH=6 on
                    // a 240px row a right-aligned value has room for ~29 chars before it reaches
                    // the label, so cap the SSID at 26 (an SSID can be up to 32). The 21-char
                    // "(select from scanner)" placeholder is well under the cap and shows in full.
                    constexpr size_t TARGET_VALUE_MAX = 26;
                    const char* t = getSSIDDisplay();
                    if (strlen(t) > TARGET_VALUE_MAX) {
                        strncpy(valueStr, t, TARGET_VALUE_MAX - 3);
                        strcpy(valueStr + (TARGET_VALUE_MAX - 3), "...");
                    } else {
                        strncpy(valueStr, t, sizeof(valueStr) - 1);
                        valueStr[sizeof(valueStr) - 1] = '\0';
                    }
                    break;
                }
                case 1: strncpy(valueStr, getModeStr(), sizeof(valueStr)); break;
                case 2:
                    strncpy(valueStr, configMode_ == 0 ? getPortalStr() : "---", sizeof(valueStr));
                    break;
                case 3:
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
                case 4: strncpy(valueStr, configDeauth_ ? "ON" : "OFF", sizeof(valueStr)); break;
                case 5:
                    strncpy(valueStr, configDeauth_ ? getDeauthIntervalStr() : "---", sizeof(valueStr));
                    break;
                case 6: strncpy(valueStr, configLogSD_ ? "Yes" : "No", sizeof(valueStr)); break;
            }
            ui::ConfigRow::render(c, rowY, optionLabels[idx], valueStr);
        },
        !footerHints_.hasFocus());

    // Footer hints. The 'M' hint is a fixed label (no appended value): the live mode already
    // shows on the Mode row, and appending it here overflowed the footer (slice-0093).
    footerHints_.setHints({
        {'M', "Mode", true},
        {'P', "Portal", configMode_ == 0},
        {'S', "SSID", true},
        {'\r', "Start", configList_.selectedIndex() == CONFIG_OPTION_COUNT - 1}
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
    
    if (creds.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(10, 50);
        canvas.print("No credentials captured");
    } else {
        // Each credential is a two-line item (username, then password). Scroll-only list: the
        // component owns the row budget and the proportional thumb, replacing the old triangle
        // carets + hardcoded maxVisible (ADR-0133).
        const int16_t listTop = 28;
        const int16_t availableHeight = canvas.height() - listTop - ui::FOOTER_HEIGHT;
        constexpr int16_t CRED_ROW_HEIGHT = 24;
        credentialList_.setItemCount((int)creds.size());
        ScrollableList::render(canvas, credentialList_, 0, listTop, canvas.width(), availableHeight,
            CRED_ROW_HEIGHT, [&creds](Canvas& c, int idx, int16_t rowY, bool /*selected*/) {
                const auto& cred = creds[idx];
                c.setTextColor(theme::ACCENT());
                c.setCursor(10, rowY);
                c.print(cred.username);
                c.setTextColor(theme::TEXT_SECONDARY());
                c.setCursor(20, rowY + 10);
                c.print(cred.password);
            });
    }

    // Footer hints
    footerHints_.setHints({});
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void EvilTwinScreen::openClientActionMenu() {
    // Resolve the selected row against the live client set (the list may have shrunk since the
    // last frame). Nothing selectable -> do not open an empty menu.
    ap::APClient clients[attack::EVIL_TWIN_MAX_CLIENTS];
    uint8_t count = evilTwin_.getClients(clients, attack::EVIL_TWIN_MAX_CLIENTS);
    int sel = clientList_.selectedIndex();
    if (count == 0 || sel < 0 || sel >= static_cast<int>(count)) return;

    memcpy(contextClientMac_, clients[sel].mac, 6);

    char macStr[18];
    utils::formatMacBytes(contextClientMac_, macStr, sizeof(macStr));

    const bool blocked = evilTwin_.isClientBlocked(contextClientMac_);
    clientActionMenu_.clearItems();
    clientActionMenu_.setTitle(macStr);
    clientActionMenu_.addItem('D', "Deauth once", true);
    // The second item toggles on the client's current blocked state, so one row serves both.
    clientActionMenu_.addItem(blocked ? 'U' : 'B', blocked ? "Unblock device" : "Block device", true);
    clientActionMenu_.setOnAction([this](char action) { handleClientAction(action); });
    clientActionMenu_.show();
    needsRedraw_ = true;
}

void EvilTwinScreen::handleClientAction(char action) {
    switch (action) {
        case 'D':
        case 'd':
            // A single burst; honest copy, since the client reconnects to our open AP shortly.
            evilTwin_.deauthClientOnce(contextClientMac_);
            showSuccessToast("Deauth sent");
            break;
        case 'B':
        case 'b':
            if (evilTwin_.blockClient(contextClientMac_)) {
                showSuccessToast("Client blocked");
            } else {
                showErrorToast("Blocklist full");
            }
            break;
        case 'U':
        case 'u':
            evilTwin_.unblockClient(contextClientMac_);
            showSuccessToast("Client unblocked");
            break;
    }
    needsRedraw_ = true;
}

void EvilTwinScreen::renderClients(Canvas& canvas) {
#ifdef ESP32
    canvas.fillScreen(theme::BG_PRIMARY());

    // Read the live client set fresh each frame. The buffer is sized to the AP cap, so it can
    // hold every admitted client; the local array stays in scope for the synchronous
    // ScrollableList::render below, which paints rows out of it.
    ap::APClient clients[attack::EVIL_TWIN_MAX_CLIENTS];
    uint8_t clientCount = evilTwin_.getClients(clients, attack::EVIL_TWIN_MAX_CLIENTS);

    char centerStr[8];
    snprintf(centerStr, sizeof(centerStr), "%d", clientCount);
    ui::StatusBar::render(canvas, "Clients", centerStr);

    const int16_t itemHeight = theme::LIST_ROW_HEIGHT;
    const int16_t listY = ui::STATUS_BAR_HEIGHT + 1;                         // sit just under the bar
    const int16_t listHeight = canvas.height() - listY - ui::FOOTER_HEIGHT;  // ...down to the footer

    clientList_.setItemCount(clientCount);

    if (clientCount == 0) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(4, listY + listHeight / 2);
        canvas.print("No clients connected");
    } else {
        canvas.setTextSize(1);
        // Same read-only shared-list idiom as the wardriving network log (slice-0074): the
        // component derives the visible-row count, keeps the selection on screen, and draws the
        // scrollbar; this callback paints one client row. No ENTER action yet (target-picker
        // feature is on the ledger), so the selection is navigation-only.
        ScrollableList::render(canvas, clientList_, 0, listY, canvas.width(), listHeight, itemHeight,
            [this, &clients](Canvas& c, int idx, int16_t y, bool /*isSelected*/) {
                const auto& client = clients[idx];

                // Row text is content-coloured, not selection-coloured (slice-0063 band-only
                // policy for data lists); the component paints the selection band.
                char macStr[18];
                utils::formatMacBytes(client.mac, macStr, sizeof(macStr));
                c.setTextColor(theme::TEXT_PRIMARY());
                c.setCursor(4, y + theme::LIST_ROW_TEXT_DY);
                c.print(macStr);

                // Tag blocklisted clients so the session blocklist is visible at a glance
                // (ADR-0132); the tag sits just left of the RSSI column.
                if (evilTwin_.isClientBlocked(client.mac)) {
                    c.setTextColor(theme::ERROR());
                    c.setCursor(c.width() - 76, y + theme::LIST_ROW_TEXT_DY);
                    c.print("BLK");
                }

                c.setTextColor(theme::TEXT_SECONDARY());
                c.setCursor(c.width() - 44, y + theme::LIST_ROW_TEXT_DY);
                c.printf("%ddBm", client.rssi);
            });
    }

    // Footer hints: ENTER opens the per-client action menu (ADR-0132).
    footerHints_.setHints({
        {'\n', "Actions", clientCount > 0}
    });
    footerHints_.render(canvas);

    // The modal action menu draws over the list (its own backdrop), so it is painted last.
    if (clientActionMenu_.isVisible()) {
        clientActionMenu_.render(canvas);
    }
#else
    (void)canvas;
#endif
}

} // namespace adversary
