/**
 * @file deauth_screen.cpp
 * @brief Deauth Attack UI screen implementation
 */

#include "deauth_screen.h"

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
    , configSelection_(0)
    , scrollOffset_(0)
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
        {'\n', "Toggle"}
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
            {'\n', "Toggle"}
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

void DeauthScreen::drawHeader(Canvas& canvas, const char* title)
{
#ifdef ESP32
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, 4);
    canvas.print(title);
    
    // Draw separator line
    canvas.drawLine(0, HEADER_HEIGHT - 2, canvas.width(), HEADER_HEIGHT - 2, theme::ACCENT_DARK());
#else
    (void)canvas;
    (void)title;
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
    drawHeader(canvas, "DEAUTH CONFIG");
    
    int16_t y = HEADER_HEIGHT + 4;
    int16_t lineHeight = 14;
    
    // Target info
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Target: ");
    
    if (hasTarget_) {
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.print(targetSsid_);
        y += lineHeight;
        
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.printf("BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
            targetBssid_[0], targetBssid_[1], targetBssid_[2],
            targetBssid_[3], targetBssid_[4], targetBssid_[5]);
        y += lineHeight;
        
        canvas.setCursor(4, y);
        canvas.printf("Channel: %d", targetChannel_);
    } else {
        canvas.setTextColor(theme::ERROR());
        canvas.print("No target selected");
    }
    
    y += lineHeight + 4;
    
    // Draw separator
    canvas.drawLine(0, y, canvas.width(), y, theme::ACCENT_DARK());
    y += 6;
    
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
        {"Target Type", targetTypeStr},
        {"Packets", packetStr},
        {"Delay", delayStr},
        {"Send Disassoc", sendDisassoc_ ? "Yes" : "No"},
        {"Start Attack", hasTarget_ ? ">" : "(no target)"}
    };
    
    // Calculate visible area (account for footer)
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    int maxVisibleItems = (footerY - y) / lineHeight;
    if (maxVisibleItems < 1) maxVisibleItems = 1;
    
    // Draw visible config options with scroll offset
    for (int i = scrollOffset_; i < CONFIG_OPTION_COUNT && (i - scrollOffset_) < maxVisibleItems; i++) {
        bool selected = (i == configSelection_);
        
        if (selected) {
            // Dim highlight when footer has focus
            uint16_t highlightColor = footerHints_.hasFocus() 
                ? theme::BG_SECONDARY() 
                : theme::BG_SELECTED();
            canvas.fillRect(0, y, canvas.width(), lineHeight, highlightColor);
        }
        
        canvas.setTextColor(selected ? theme::ACCENT() : theme::TEXT_PRIMARY());
        canvas.setCursor(4, y + 2);
        canvas.print(options[i].label);
        
        // Right-align value
        int16_t valueWidth = strlen(options[i].value) * 6;
        canvas.setCursor(canvas.width() - valueWidth - 4, y + 2);
        canvas.print(options[i].value);
        
        y += lineHeight;
    }
    
    // Draw scroll indicators if needed
    if (scrollOffset_ > 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(canvas.width() - 12, HEADER_HEIGHT + 4);
        canvas.print("^");
    }
    if (scrollOffset_ + maxVisibleItems < CONFIG_OPTION_COUNT) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(canvas.width() - 12, footerY - lineHeight);
        canvas.print("v");
    }
    
    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawRunning(Canvas& canvas)
{
#ifdef ESP32
    drawHeader(canvas, "DEAUTH RUNNING");
    
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
        {'`', "Abort"}
    });
    drawFooter(canvas);
#else
    (void)canvas;
#endif
}

void DeauthScreen::drawResults(Canvas& canvas)
{
#ifdef ESP32
    drawHeader(canvas, "DEAUTH RESULTS");
    
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
        {'`', "Exit"}
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
    
    // Screen-specific input
    switch (key) {
        case ';':  // Up
            handleUp();
            return true;
        case '.':  // Down
            handleDown();
            return true;
        case '\n':
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

void DeauthScreen::handleUp()
{
    if (screenState_ == DeauthScreenState::CONFIG) {
        if (configSelection_ > 0) {
            configSelection_--;
            // Adjust scroll if selection moves above visible area
            if (configSelection_ < scrollOffset_) {
                scrollOffset_ = configSelection_;
            }
            needsRedraw_ = true;
        }
    }
}

void DeauthScreen::handleDown()
{
    if (screenState_ == DeauthScreenState::CONFIG) {
        if (configSelection_ < CONFIG_OPTION_COUNT - 1) {
            configSelection_++;
            // Adjust scroll if selection moves below visible area (assume ~3 visible items)
            if (configSelection_ >= scrollOffset_ + VISIBLE_CONFIG_ITEMS) {
                scrollOffset_ = configSelection_ - VISIBLE_CONFIG_ITEMS + 1;
            }
            needsRedraw_ = true;
        }
    }
}

void DeauthScreen::handleSelect()
{
    switch (screenState_) {
        case DeauthScreenState::CONFIG:
            // Toggle/modify selected option or start attack
            if (configSelection_ == CONFIG_OPTION_COUNT - 1) {
                // Last option is "Start Attack"
                if (hasTarget_) {
                    startAttack();
                }
            } else {
                // Toggle options
                switch (configSelection_) {
                    case 0:  // Target type
                        if (targetType_ == DeauthTargetType::ALL_CLIENTS) {
                            targetType_ = DeauthTargetType::SINGLE_CLIENT;
                        } else if (targetType_ == DeauthTargetType::SINGLE_CLIENT) {
                            targetType_ = DeauthTargetType::AP_ONLY;
                        } else {
                            targetType_ = DeauthTargetType::ALL_CLIENTS;
                        }
                        break;
                    case 1:  // Packet count
                        if (packetCount_ == 0) packetCount_ = 50;
                        else if (packetCount_ < 100) packetCount_ = 100;
                        else if (packetCount_ < 500) packetCount_ = 500;
                        else if (packetCount_ < 1000) packetCount_ = 1000;
                        else packetCount_ = 0;  // Infinite
                        break;
                    case 2:  // Delay
                        if (delayMs_ <= 10) delayMs_ = 50;
                        else if (delayMs_ <= 50) delayMs_ = 100;
                        else if (delayMs_ <= 100) delayMs_ = 200;
                        else delayMs_ = 10;
                        break;
                    case 3:  // Send disassoc
                        sendDisassoc_ = !sendDisassoc_;
                        break;
                }
            }
            needsRedraw_ = true;
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
