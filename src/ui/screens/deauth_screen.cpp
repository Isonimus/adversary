/**
 * @file deauth_screen.cpp
 * @brief Deauth Attack UI screen implementation
 */

#include "deauth_screen.h"
#include "ui/components/config_row.h"
#include "ui/components/config_input.h"
#include "ui/screen_manager.h"  // navigate to the scanner from the Target row (slice-0109)

namespace adversary {

DeauthScreen::DeauthScreen()
    : visible_(false)
    , initialized_(false)
    , needsRedraw_(true)
    , shouldExit_(false)
    , lastUpdate_(0)
    , screenState_(DeauthScreenState::CONFIG)
    , hasTarget_(false)
    , targetChannel_(1)
    , targetType_(DeauthTargetType::ALL_CLIENTS)
    , reasonCode_(DeauthReason::DEAUTH_LEAVING)
    , packetCount_(100)
    , delayMs_(50)
    , sendDisassoc_(true)
    , canvas_(nullptr)
    , canvasInitialized_(false)
{
    memset(targetBssid_, 0, 6);
    memset(targetSsid_, 0, sizeof(targetSsid_));
    
    // Initialize footer hints (FooterHint takes char key, not string)
    footerHints_.setHints({
        {' ', "Focus"},
        {'s', "Start", false},
        {';', "Nav"},
        {'\n', "Change"}
    });
}

DeauthScreen::~DeauthScreen()
{
    if (canvas_) {
        canvas_->deleteSprite();
        delete canvas_;
        canvas_ = nullptr;
    }
}

void DeauthScreen::init()
{
    if (initialized_) return;
    
    // Initialize deauth module
    DeauthAttack::getInstance().init();
    
    initialized_ = true;
    needsRedraw_ = true;
}

void DeauthScreen::show()
{
    visible_ = true;
    needsRedraw_ = true;
    shouldExit_ = false;
    
    // If no target, start in config
    if (!hasTarget_) {
        screenState_ = DeauthScreenState::CONFIG;
    }
    
    // Update footer hints based on state
    if (screenState_ == DeauthScreenState::CONFIG) {
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start", hasTarget_},
            {';', "Nav"},
            {'\n', "Change"}
        });
    }
    // Subscribe for attack state changes
    if (stateHandlerId_ == 0) {
        stateHandlerId_ = EventBus::getInstance().subscribe(
            EventType::ATTACK_STATE_CHANGED,
            [this](const EventData& event) {
                // Only process Deauth attack events (type 0)
                if (event.payload.attack.attackType != static_cast<uint8_t>(AttackTypeId::DEAUTH)) return;
                
                DeauthState newState = static_cast<DeauthState>(event.payload.attack.newState);
                
                if (screenState_ == DeauthScreenState::RUNNING &&
                    newState == DeauthState::COMPLETED) {
                    screenState_ = DeauthScreenState::RESULTS;
                    needsRedraw_ = true;
                }
            }
        );
    }
}

void DeauthScreen::hide()
{
    visible_ = false;
    
    // Stop attack if running
    if (DeauthAttack::getInstance().isRunning()) {
        DeauthAttack::getInstance().stop();
    }
    
    // Unsubscribe from EventBus
    if (stateHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(stateHandlerId_);
        stateHandlerId_ = 0;
    }
}

void DeauthScreen::setTarget(const uint8_t* bssid, const char* ssid, uint8_t channel)
{
    if (bssid) {
        memcpy(targetBssid_, bssid, 6);
    }
    if (ssid) {
        strncpy(targetSsid_, ssid, sizeof(targetSsid_) - 1);
        targetSsid_[sizeof(targetSsid_) - 1] = '\0';
    }
    targetChannel_ = channel;
    hasTarget_ = true;
    needsRedraw_ = true;
}

bool DeauthScreen::isAttackRunning() const
{
    return DeauthAttack::getInstance().isRunning() || 
           DeauthAttack::getInstance().isPaused();
}

void DeauthScreen::update()
{
    if (!visible_) return;
    
    // Update deauth attack
    DeauthAttack::getInstance().update();
    
    // Check if attack completed - now handled by EventBus subscription
    // Replaced by callback logic

    
    // Force redraw when running
    if (screenState_ == DeauthScreenState::RUNNING) {
        needsRedraw_ = true;
    }
}

// =============================================================================
// Rendering
// =============================================================================

void DeauthScreen::render(Canvas& canvas)
{
    if (!visible_) return;

#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (screenState_) {
        case DeauthScreenState::CONFIG:
            drawConfig(canvas);
            break;
        case DeauthScreenState::RUNNING:
            drawRunning(canvas);
            break;
        case DeauthScreenState::RESULTS:
            drawResults(canvas);
            break;
        default:
            drawConfig(canvas);
            break;
    }
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawFooter(Canvas& canvas)
{
#ifdef ESP32
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawConfig(Canvas& canvas)
{
#ifdef ESP32
    ui::StatusBar::render(canvas, "DEAUTH CONFIG");

    // Target is now the first selectable list row (ENTER routes to the scanner, ADR-0092 /
    // slice-0109), not a separate header, so the list starts right below the header, exactly
    // like Evil Twin. The old 3-line SSID/BSSID/Channel block and its separators are gone; the
    // target identity now lives in the running view where it is actually useful.
    int16_t y = HEADER_HEIGHT + 4;
    int16_t lineHeight = theme::LIST_ROW_HEIGHT;

    // Target row value: the SSID (capped so it clears the "Target" label on the shared row, an
    // SSID can be up to 32), or the shared scanner placeholder (slice-0109). The 21-char
    // placeholder is under the cap and shows in full. Cap matches evil_twin's Target row.
    constexpr size_t TARGET_VALUE_MAX = 26;
    char targetStr[28];
    if (hasTarget_ && targetSsid_[0] != '\0') {
        if (strlen(targetSsid_) > TARGET_VALUE_MAX) {
            strncpy(targetStr, targetSsid_, TARGET_VALUE_MAX - 3);
            strcpy(targetStr + (TARGET_VALUE_MAX - 3), "...");
        } else {
            strncpy(targetStr, targetSsid_, sizeof(targetStr) - 1);
            targetStr[sizeof(targetStr) - 1] = '\0';
        }
    } else {
        strncpy(targetStr, "(select from scanner)", sizeof(targetStr) - 1);
        targetStr[sizeof(targetStr) - 1] = '\0';
    }

    // Config options
    const char* targetTypeStr = "All Clients";
    if (targetType_ == DeauthTargetType::SINGLE_CLIENT) targetTypeStr = "Single Client";
    else if (targetType_ == DeauthTargetType::AP_ONLY) targetTypeStr = "AP Only";
    
    char packetStr[16];
    if (packetCount_ == 0) {
        snprintf(packetStr, sizeof(packetStr), "Infinite");
    } else {
        snprintf(packetStr, sizeof(packetStr), "%lu", (unsigned long)packetCount_);
    }
    
    char delayStr[16];
    snprintf(delayStr, sizeof(delayStr), "%lu ms", (unsigned long)delayMs_);
    
    struct ConfigOption {
        const char* label;
        const char* value;
    } options[] = {
        {"Target", targetStr},
        {"Target Type", targetTypeStr},
        {"Packets", packetStr},
        {"Delay", delayStr},
        {"Send Disassoc", sendDisassoc_ ? "Yes" : "No"},
        {"Start Attack", hasTarget_ ? ">" : "(no target)"}
    };
    
    // The shared component derives the visible-row count from the list rect (below the header,
    // down to the footer), keeps the selection on screen, and draws the unified thumb in place
    // of the old ^/v markers; this callback paints one option row.
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    configList_.setItemCount(CONFIG_OPTION_COUNT);
    ScrollableList::render(canvas, configList_, 0, y, canvas.width(), footerY - y, lineHeight,
        [&options](Canvas& c, int idx, int16_t rowY, bool /*selected*/) {
            // Every deauth row renders through the shared chrome-only ConfigRow (label left,
            // value right-aligned and dim, selection shown by the band alone): the Target
            // navigation row (value = SSID or the scanner placeholder), the value rows, and the
            // terminal Start Attack row (value ">" / "(no target)"). The deauth start-affordance
            // redesign stays its own LEDGER item (ADR-0096, slice-0106).
            ui::ConfigRow::render(c, rowY, options[idx].label, options[idx].value);
        },
        !footerHints_.hasFocus());

    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawRunning(Canvas& canvas)
{
#ifdef ESP32
    ui::StatusBar::render(canvas, "DEAUTH RUNNING");
    
    auto& attack = DeauthAttack::getInstance();
    const auto& stats = attack.getStats();
    
    int16_t y = HEADER_HEIGHT + 8;
    int16_t lineHeight = 14;
    
    // Status
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, y);
    
    if (attack.isPaused()) {
        canvas.setTextColor(theme::WARNING());
        canvas.print("PAUSED");
    } else {
        canvas.setTextColor(theme::SUCCESS());
        canvas.print("ATTACKING");
    }
    y += lineHeight + 4;
    
    // Progress bar
    float progress = 0.0f;
    if (packetCount_ > 0) {
        progress = (float)stats.packetsSent / (float)packetCount_;
    }
    drawProgressBar(canvas, 4, y, canvas.width() - 8, 10, progress);
    y += 16;
    
    // Stats
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.printf("Packets: %lu", (unsigned long)stats.packetsSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Deauths: %lu", (unsigned long)stats.deauthSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Disassocs: %lu", (unsigned long)stats.disassocSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Errors: %lu", (unsigned long)stats.errors);
    y += lineHeight + 4;
    
    // Duration
    uint32_t elapsed = millis() - stats.startTime;
    canvas.setCursor(4, y);
    canvas.printf("Time: %lu.%lus", (unsigned long)(elapsed / 1000), (unsigned long)((elapsed % 1000) / 100));
    
    // Footer with running hints
    footerHints_.setHints({
        {' ', attack.isPaused() ? "Resume" : "Pause"},
        {'\n', "Stop"},
        {'`', "Stop"}
    });
    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawResults(Canvas& canvas)
{
#ifdef ESP32
    ui::StatusBar::render(canvas, "DEAUTH RESULTS");
    
    const auto& stats = DeauthAttack::getInstance().getStats();
    
    int16_t y = HEADER_HEIGHT + 8;
    int16_t lineHeight = 14;
    
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(4, y);
    canvas.print("Attack Complete");
    y += lineHeight + 4;
    
    // Stats summary
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, y);
    canvas.printf("Total Packets: %lu", (unsigned long)stats.packetsSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Deauths Sent: %lu", (unsigned long)stats.deauthSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Disassocs Sent: %lu", (unsigned long)stats.disassocSent);
    y += lineHeight;
    
    canvas.setCursor(4, y);
    canvas.printf("Errors: %lu", (unsigned long)stats.errors);
    y += lineHeight + 4;
    
    // Duration - use current time since we don't have endTime
    uint32_t elapsed = stats.getDurationSeconds();
    canvas.setCursor(4, y);
    canvas.printf("Duration: %lus", (unsigned long)elapsed);
    
    // Footer
    footerHints_.setHints({
        {'\n', "Continue"},
        {'`', "Back"}
    });
    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawProgressBar(Canvas& canvas, int x, int y, int width, int height, float progress)
{
#ifdef ESP32
    // Background
    canvas.fillRect(x, y, width, height, theme::BG_SECONDARY());
    
    // Border
    canvas.drawRect(x, y, width, height, theme::ACCENT_DARK());
    
    // Fill
    int fillWidth = (int)((width - 2) * progress);
    if (fillWidth > 0) {
        canvas.fillRect(x + 1, y + 1, fillWidth, height - 2, theme::ACCENT());
    }
#else
    (void)canvas;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)progress;
#endif
}

// =============================================================================
// Input Handling
// =============================================================================

bool DeauthScreen::handleInput(char key)
{
    if (!visible_) return false;
    
    // Handle footer hints interaction (space toggle, navigation, dispatch)
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action != 0) {
            // Dispatch footer action
            switch (action) {
                case 's':
                case 'S':
                    if (hasTarget_ && screenState_ == DeauthScreenState::CONFIG) {
                        startAttack();
                    }
                    return true;
            }
        }
        needsRedraw_ = true;
        return true;
    }
    
    // Handle exit key
    if (key == '`' || key == 'q' || key == 'Q') {
        handleBack();
        return true;
    }
    
    // Canonical config-form input in the CONFIG state, from the shared classifier (ADR-0107).
    // Row 0 (Target) is the navigation row -> WiFi scanner; the last row is the terminal
    // "Start Attack" row (activates only with a target); the rest are value rows.
    if (screenState_ == DeauthScreenState::CONFIG) {
        const int sel = configList_.selectedIndex();
        const ui::ConfigRowKind kind =
            (sel == 0)                        ? ui::ConfigRowKind::Navigation :
            (sel == CONFIG_OPTION_COUNT - 1)  ? ui::ConfigRowKind::Terminal :
                                                ui::ConfigRowKind::Value;
        switch (ui::classifyConfigKey(key, kind)) {
            case ui::ConfigKeyAction::NavUp:
                configList_.setItemCount(CONFIG_OPTION_COUNT);
                configList_.moveUp();  // wraps to last; model keeps the selection on screen
                needsRedraw_ = true;
                return true;
            case ui::ConfigKeyAction::NavDown:
                configList_.setItemCount(CONFIG_OPTION_COUNT);
                configList_.moveDown();  // wraps to first; model keeps the selection on screen
                needsRedraw_ = true;
                return true;
            case ui::ConfigKeyAction::EditPrev:
                handleEdit(-1);
                return true;
            case ui::ConfigKeyAction::EditNext:
                handleEdit(+1);  // also ENTER on a value row (never inert, ADR-0092)
                return true;
            case ui::ConfigKeyAction::Activate:
                if (hasTarget_) startAttack();  // terminal row: guarded on having a target
                needsRedraw_ = true;
                return true;
            case ui::ConfigKeyAction::Navigate:
                // Target row: route to the WiFi scanner to pick a target (always, with or
                // without an existing one). It returns via ATTACK_TARGET_SELECTED -> setTarget
                // (like Evil Twin / handshake, ADR-0092).
                ScreenManager::getInstance().navigateTo(ScreenId::SCANNER);
                return true;
            case ui::ConfigKeyAction::Popup:
            case ui::ConfigKeyAction::None:
                break;  // deauth has no popup row; fall through to the keys below
        }
    }

    // Screen-specific input
    switch (key) {
        case '\n':  // Enter (non-CONFIG states: stop / return to config)
        case '\r':
            handleSelect();
            return true;
        case ' ':  // Space to pause/resume in running state
            if (screenState_ == DeauthScreenState::RUNNING) {
                if (DeauthAttack::getInstance().isRunning()) {
                    DeauthAttack::getInstance().pause();
                } else if (DeauthAttack::getInstance().isPaused()) {
                    DeauthAttack::getInstance().resume();
                }
                needsRedraw_ = true;
            }
            return true;
        case 's':
        case 'S':
            if (screenState_ == DeauthScreenState::CONFIG && hasTarget_) {
                startAttack();
            }
            return true;
    }
    
    return false;
}

void DeauthScreen::handleSelect()
{
    switch (screenState_) {
        case DeauthScreenState::CONFIG:
            // ENTER in CONFIG is handled by the classifier in handleInput; this path is only
            // reached in the attack states below.
            break;

        case DeauthScreenState::RUNNING:
            // Stop attack
            stopAttack();
            break;
            
        case DeauthScreenState::RESULTS:
            // Return to config
            screenState_ = DeauthScreenState::CONFIG;
            needsRedraw_ = true;
            break;
            
        default:
            break;
    }
}

void DeauthScreen::handleEdit(int delta)
{
    if (screenState_ != DeauthScreenState::CONFIG) return;
    const bool fwd = delta > 0;
    switch (configList_.selectedIndex()) {
        // case 0 (Target) is a navigation row: nothing to edit in place (ADR-0092).
        case 1:  // Target type: 3-state ring ALL -> SINGLE -> AP_ONLY
            if (fwd) {
                targetType_ = (targetType_ == DeauthTargetType::ALL_CLIENTS)  ? DeauthTargetType::SINGLE_CLIENT
                            : (targetType_ == DeauthTargetType::SINGLE_CLIENT) ? DeauthTargetType::AP_ONLY
                            :                                                    DeauthTargetType::ALL_CLIENTS;
            } else {
                targetType_ = (targetType_ == DeauthTargetType::ALL_CLIENTS) ? DeauthTargetType::AP_ONLY
                            : (targetType_ == DeauthTargetType::AP_ONLY)     ? DeauthTargetType::SINGLE_CLIENT
                            :                                                  DeauthTargetType::ALL_CLIENTS;
            }
            break;
        case 2:  // Packet count ring: 0(infinite), 50, 100, 500, 1000
            if (fwd) {
                if (packetCount_ == 0) packetCount_ = 50;
                else if (packetCount_ < 100) packetCount_ = 100;
                else if (packetCount_ < 500) packetCount_ = 500;
                else if (packetCount_ < 1000) packetCount_ = 1000;
                else packetCount_ = 0;
            } else {
                if (packetCount_ == 0) packetCount_ = 1000;
                else if (packetCount_ <= 50) packetCount_ = 0;
                else if (packetCount_ <= 100) packetCount_ = 50;
                else if (packetCount_ <= 500) packetCount_ = 100;
                else packetCount_ = 500;
            }
            break;
        case 3:  // Delay ring: 10, 50, 100, 200 ms
            if (fwd) {
                if (delayMs_ <= 10) delayMs_ = 50;
                else if (delayMs_ <= 50) delayMs_ = 100;
                else if (delayMs_ <= 100) delayMs_ = 200;
                else delayMs_ = 10;
            } else {
                if (delayMs_ <= 10) delayMs_ = 200;
                else if (delayMs_ <= 50) delayMs_ = 10;
                else if (delayMs_ <= 100) delayMs_ = 50;
                else delayMs_ = 100;
            }
            break;
        case 4:  // Send disassoc: toggle (prev and next both flip)
            sendDisassoc_ = !sendDisassoc_;
            break;
    }
    needsRedraw_ = true;
}

void DeauthScreen::handleBack()
{
    switch (screenState_) {
        case DeauthScreenState::RUNNING:
            stopAttack();
            screenState_ = DeauthScreenState::CONFIG;
            needsRedraw_ = true;
            break;
        case DeauthScreenState::RESULTS:
            screenState_ = DeauthScreenState::CONFIG;
            needsRedraw_ = true;
            break;
        default:
            // Signal to return to menu
            shouldExit_ = true;
            break;
    }
}

void DeauthScreen::startAttack()
{
    DeauthConfig config;
    config.setApBssid(targetBssid_);
    config.targetType = targetType_;
    config.reason = reasonCode_;
    config.packetCount = packetCount_;
    config.delayMs = delayMs_;
    config.channel = targetChannel_;
    config.sendDisassoc = sendDisassoc_;
    
    // Use a test BSSID if no target set
    if (!hasTarget_) {
        uint8_t testBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        config.setApBssid(testBssid);
    }
    
    if (DeauthAttack::getInstance().start(config)) {
        screenState_ = DeauthScreenState::RUNNING;
        needsRedraw_ = true;
    }
}

void DeauthScreen::stopAttack()
{
    DeauthAttack::getInstance().stop();
    screenState_ = DeauthScreenState::RESULTS;
    needsRedraw_ = true;
}

} // namespace adversary
