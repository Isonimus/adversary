/**
 * @file beacon_spam_screen.cpp
 * @brief Beacon Spam Screen implementation
 */

#include "beacon_spam_screen.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

BeaconSpamScreen::BeaconSpamScreen()
    : visible_(false)
    , initialized_(false)
    , needsRedraw_(true)
    , shouldExit_(false)
    , lastUpdate_(0)
    , screenState_(BeaconSpamScreenState::CONFIG)
    , configSelection_(0)
    , scrollOffset_(0)
    , mode_(BeaconSpamMode::RICKROLL)
    , channel_(1)
    , intervalMs_(100)
    , randomizeBssid_(true)
    , encryptedNetwork_(false)
    , channelHopEnabled_(false)
{
    memset(customSsid_, 0, sizeof(customSsid_));
    strcpy(customSsid_, "Adversary");
    
    // Initialize footer hints
    footerHints_.setHints({
        {' ', "Focus"},
        {'s', "Start"},
        {';', "Nav"},
        {'\n', "Toggle"}
    });
}

BeaconSpamScreen::~BeaconSpamScreen() {
    hide();
}

void BeaconSpamScreen::init() {
    if (initialized_) return;
    
#ifdef ESP32
    Serial.println("[BeaconSpamScreen] Initialized");
#endif
    
    initialized_ = true;
}

void BeaconSpamScreen::show() {
    if (!initialized_) {
        init();
    }
    
    visible_ = true;
    needsRedraw_ = true;
    shouldExit_ = false;
    screenState_ = BeaconSpamScreenState::CONFIG;
    configSelection_ = 0;
    scrollOffset_ = 0;
    
    // Update footer hints for config state
    footerHints_.setHints({
        {' ', "Focus"},
        {'s', "Start"},
        {';', "Nav"},
        {'\n', "Toggle"}
    });
    
    // Subscribe to attack state changes
    if (stateHandlerId_ == 0) {
        stateHandlerId_ = EventBus::getInstance().subscribe(
            EventType::ATTACK_STATE_CHANGED,
            [this](const EventData& evt) {
                // Only handle BeaconSpam events
                if (evt.payload.attack.attackType != static_cast<uint8_t>(AttackTypeId::BEACON_SPAM)) return;
                
                BeaconSpamState newState = static_cast<BeaconSpamState>(evt.payload.attack.newState);
                
                if (newState == BeaconSpamState::COMPLETED) {
                    screenState_ = BeaconSpamScreenState::COMPLETED;
                    needsRedraw_ = true;
                } else if (newState == BeaconSpamState::PAUSED && 
                           screenState_ != BeaconSpamScreenState::PAUSED) {
                    screenState_ = BeaconSpamScreenState::PAUSED;
                    needsRedraw_ = true;
                } else if (newState == BeaconSpamState::RUNNING && 
                           screenState_ == BeaconSpamScreenState::PAUSED) {
                    screenState_ = BeaconSpamScreenState::RUNNING;
                    needsRedraw_ = true;
                }
            }
        );
    }
    
#ifdef ESP32
    Serial.println("[BeaconSpamScreen] Shown");
#endif
}

void BeaconSpamScreen::hide() {
    if (!visible_) return;
    
    // Stop attack if running
    if (isAttackRunning()) {
        stopAttack();
    }
    
    // Unsubscribe from EventBus
    if (stateHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(stateHandlerId_);
        stateHandlerId_ = 0;
    }
    
    visible_ = false;
    
#ifdef ESP32
    Serial.println("[BeaconSpamScreen] Hidden");
#endif
}

void BeaconSpamScreen::update() {
    if (!visible_) return;
    
    // Update beacon spam module
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.update();
    
    // State changes are now handled by EventBus subscription
    
    // Force redraw during attack for live stats
    if (screenState_ == BeaconSpamScreenState::RUNNING || 
        screenState_ == BeaconSpamScreenState::PAUSED) {
        needsRedraw_ = true;
    }
}

// =============================================================================
// Rendering
// =============================================================================

void BeaconSpamScreen::render(Canvas& canvas)
{
    if (!visible_) return;

#ifdef ESP32
    // Rate-limit redraws
    uint32_t now = millis();
    if (!needsRedraw_ && (now - lastUpdate_) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    lastUpdate_ = now;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Render based on state
    switch (screenState_) {
        case BeaconSpamScreenState::CONFIG:
            drawHeader(canvas, "Beacon Spam");
            drawConfig(canvas);
            break;
            
        case BeaconSpamScreenState::RUNNING:
            drawHeader(canvas, "Spamming...");
            drawRunning(canvas);
            break;
            
        case BeaconSpamScreenState::PAUSED:
            drawHeader(canvas, "Paused");
            drawRunning(canvas);
            break;
            
        case BeaconSpamScreenState::COMPLETED:
            drawHeader(canvas, "Completed");
            drawStats(canvas);
            break;
    }
    
    // Always render footer hints
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void BeaconSpamScreen::drawHeader(Canvas& canvas, const char* title)
{
#ifdef ESP32
    // Show mode name as center content
    const char* modeName = getModeName(mode_);
    ui::StatusBar::render(canvas, title, modeName, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
#else
    (void)canvas;
    (void)title;
#endif
}

void BeaconSpamScreen::drawConfig(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 4;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 14;
    const int16_t labelX = 8;
    const int16_t valueX = 100;
    
    canvas.setTextSize(1);
    
    // Config options with labels and values
    struct ConfigOption {
        const char* label;
        char value[20];
    };
    
    ConfigOption options[CONFIG_OPTION_COUNT];
    
    // Mode
    options[0].label = "Mode:";
    strncpy(options[0].value, getModeName(mode_), 19);
    options[0].value[19] = '\0';
    
    // Channel
    options[1].label = "Channel:";
    snprintf(options[1].value, 20, "%d", channel_);
    
    // Interval
    options[2].label = "Interval:";
    snprintf(options[2].value, 20, "%lu ms", (unsigned long)intervalMs_);
    
    // Randomize BSSID
    options[3].label = "Rand BSSID:";
    strncpy(options[3].value, randomizeBssid_ ? "Yes" : "No", 19);
    options[3].value[19] = '\0';
    
    // Encrypted
    options[4].label = "Encrypted:";
    strncpy(options[4].value, encryptedNetwork_ ? "WPA2" : "Open", 19);
    options[4].value[19] = '\0';
    
    // Channel Hop
    options[5].label = "Ch Hopping:";
    strncpy(options[5].value, channelHopEnabled_ ? "Yes" : "No", 19);
    options[5].value[19] = '\0';
    
    // Start button
    options[6].label = "[START]";
    options[6].value[0] = '\0';
    
    // Calculate visible area
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    int maxVisibleItems = (footerY - y) / lineHeight;
    if (maxVisibleItems < 1) maxVisibleItems = 1;
    
    // Draw visible options with scroll offset
    for (int i = scrollOffset_; i < CONFIG_OPTION_COUNT && (i - scrollOffset_) < maxVisibleItems; i++) {
        bool selected = (i == configSelection_);
        
        if (selected) {
            // Dim highlight when footer has focus
            uint16_t highlightColor = footerHints_.hasFocus() 
                ? theme::BG_SECONDARY() 
                : theme::BG_SELECTED();
            canvas.fillRect(0, y - 2, screenWidth, lineHeight, highlightColor);
        }
        
        canvas.setTextColor(selected ? theme::ACCENT() : theme::TEXT_PRIMARY());
        
        if (i == 6) {
            // Start button - centered
            int textWidth = strlen(options[i].label) * 6;
            canvas.setCursor((screenWidth - textWidth) / 2, y);
            canvas.print(options[i].label);
        } else {
            canvas.setCursor(labelX, y);
            canvas.print(options[i].label);
            canvas.setCursor(valueX, y);
            canvas.print(options[i].value);
        }
        
        y += lineHeight;
    }
    
    // Draw scroll indicators if needed
    if (scrollOffset_ > 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(screenWidth - 12, HEADER_HEIGHT + 4);
        canvas.print("^");
    }
    if (scrollOffset_ + maxVisibleItems < CONFIG_OPTION_COUNT) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(screenWidth - 12, footerY - lineHeight);
        canvas.print("v");
    }
#else
    (void)canvas;
#endif
}

void BeaconSpamScreen::drawRunning(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 8;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 16;
    
    BeaconSpam& bs = BeaconSpam::getInstance();
    const BeaconSpamStats& stats = bs.getStats();
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    // Current SSID
    canvas.setCursor(8, y);
    canvas.print("SSID: ");
    canvas.setTextColor(theme::ACCENT());
    const char* currentSsid = bs.getCurrentSSID();
    // Truncate if too long
    if (strlen(currentSsid) > 24) {
        char truncated[28];
        strncpy(truncated, currentSsid, 24);
        truncated[24] = '\0';
        strcat(truncated, "...");
        canvas.print(truncated);
    } else {
        canvas.print(currentSsid);
    }
    y += lineHeight + 4;
    
    // Stats
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(8, y);
    canvas.printf("Beacons Sent: %lu", (unsigned long)stats.beaconsSent);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Rate: %.1f/sec", stats.getBeaconsPerSecond());
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Duration: %lu sec", (unsigned long)(stats.duration / 1000));
    y += lineHeight;
    
    canvas.setCursor(8, y);
    const BeaconSpamConfig& cfg = bs.getConfig();
    if (channelHopEnabled_) {
        canvas.printf("Channel: %d (Hopping)", cfg.channel);
    } else {
        canvas.printf("Channel: %d", cfg.channel);
    }
    
    // Status indicator
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    y = footerY - 20;
    if (screenState_ == BeaconSpamScreenState::RUNNING) {
        canvas.fillCircle(screenWidth / 2, y, 6, theme::SUCCESS());
    } else {
        canvas.fillCircle(screenWidth / 2, y, 6, theme::WARNING());
    }
    
    // Update footer hints for running state
    footerHints_.setHints({
        {' ', screenState_ == BeaconSpamScreenState::PAUSED ? "Resume" : "Pause"},
        {'\n', "Stop"},
        {'`', "Back"}
    });
#else
    (void)canvas;
#endif
}

void BeaconSpamScreen::drawStats(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 8;
    const int16_t lineHeight = 16;
    
    BeaconSpam& bs = BeaconSpam::getInstance();
    const BeaconSpamStats& stats = bs.getStats();
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    
    canvas.setCursor(8, y);
    canvas.print("Attack Complete!");
    y += lineHeight + 4;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(8, y);
    canvas.printf("Total Beacons: %lu", (unsigned long)stats.beaconsSent);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("SSIDs Used: %lu", (unsigned long)stats.ssidsUsed);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Duration: %lu sec", (unsigned long)(stats.duration / 1000));
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Avg Rate: %.1f/sec", stats.getBeaconsPerSecond());
    
    // Update footer hints for completed state
    footerHints_.setHints({
        {'\n', "Continue"},
        {'`', "Exit"}
    });
#else
    (void)canvas;
#endif
}

// =============================================================================
// Input Handling
// =============================================================================

bool BeaconSpamScreen::handleInput(char key) {
    if (!visible_) return false;
    
    // Handle footer hints interaction
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action != 0) {
            // Dispatch footer action
            switch (action) {
                case 's':
                case 'S':
                    if (screenState_ == BeaconSpamScreenState::CONFIG) {
                        startAttack();
                    }
                    return true;
            }
        }
        needsRedraw_ = true;
        return true;
    }
    
    switch (key) {
        case ';':  // Up
            handleUp();
            return true;
            
        case '.':  // Down
            handleDown();
            return true;
            
        case '\n':  // Enter/Select
        case '\r':
            handleSelect();
            return true;
            
        case '`':   // Back
            handleBack();
            return true;
            
        case ' ':   // Space - pause/resume
            if (screenState_ == BeaconSpamScreenState::RUNNING ||
                screenState_ == BeaconSpamScreenState::PAUSED) {
                togglePause();
            }
            return true;
            
        case 's':
        case 'S':
            if (screenState_ == BeaconSpamScreenState::CONFIG) {
                startAttack();
            }
            return true;
            
        default:
            return false;
    }
}

void BeaconSpamScreen::handleUp() {
    if (screenState_ == BeaconSpamScreenState::CONFIG) {
        if (configSelection_ > 0) {
            configSelection_--;
            // Adjust scroll if selection moves above visible area
            if (configSelection_ < scrollOffset_) {
                scrollOffset_ = configSelection_;
            }
        } else {
            configSelection_ = CONFIG_OPTION_COUNT - 1;
            scrollOffset_ = CONFIG_OPTION_COUNT - VISIBLE_CONFIG_ITEMS;
            if (scrollOffset_ < 0) scrollOffset_ = 0;
        }
        needsRedraw_ = true;
    }
}

void BeaconSpamScreen::handleDown() {
    if (screenState_ == BeaconSpamScreenState::CONFIG) {
        if (configSelection_ < CONFIG_OPTION_COUNT - 1) {
            configSelection_++;
            // Adjust scroll if selection moves below visible area
            if (configSelection_ >= scrollOffset_ + VISIBLE_CONFIG_ITEMS) {
                scrollOffset_ = configSelection_ - VISIBLE_CONFIG_ITEMS + 1;
            }
        } else {
            configSelection_ = 0;
            scrollOffset_ = 0;
        }
        needsRedraw_ = true;
    }
}

void BeaconSpamScreen::handleSelect() {
    switch (screenState_) {
        case BeaconSpamScreenState::CONFIG:
            switch (configSelection_) {
                case 0:  // Mode - cycle through modes
                    mode_ = static_cast<BeaconSpamMode>(
                        (static_cast<int>(mode_) + 1) % 6);
                    break;
                    
                case 1:  // Channel - cycle 1-14
                    channel_ = (channel_ % 14) + 1;
                    break;
                    
                case 2:  // Interval - cycle through presets
                    if (intervalMs_ <= 50) intervalMs_ = 100;
                    else if (intervalMs_ <= 100) intervalMs_ = 200;
                    else if (intervalMs_ <= 200) intervalMs_ = 500;
                    else intervalMs_ = 50;
                    break;
                    
                case 3:  // Randomize BSSID
                    randomizeBssid_ = !randomizeBssid_;
                    break;
                    
                case 4:  // Encrypted
                    encryptedNetwork_ = !encryptedNetwork_;
                    break;
                    
                case 5:  // Channel Hop
                    channelHopEnabled_ = !channelHopEnabled_;
                    break;
                    
                case 6:  // Start button
                    startAttack();
                    break;
            }
            needsRedraw_ = true;
            break;
            
        case BeaconSpamScreenState::RUNNING:
        case BeaconSpamScreenState::PAUSED:
            // Stop attack
            stopAttack();
            screenState_ = BeaconSpamScreenState::COMPLETED;
            needsRedraw_ = true;
            break;
            
        case BeaconSpamScreenState::COMPLETED:
            // Go back to config
            screenState_ = BeaconSpamScreenState::CONFIG;
            // Reset footer hints
            footerHints_.setHints({
                {' ', "Focus"},
                {'s', "Start"},
                {';', "Nav"},
                {'\n', "Toggle"}
            });
            needsRedraw_ = true;
            break;
    }
}

void BeaconSpamScreen::handleBack() {
    switch (screenState_) {
        case BeaconSpamScreenState::CONFIG:
            // Exit to menu
            shouldExit_ = true;
            break;
            
        case BeaconSpamScreenState::RUNNING:
        case BeaconSpamScreenState::PAUSED:
            // Stop and go to config
            stopAttack();
            screenState_ = BeaconSpamScreenState::CONFIG;
            footerHints_.setHints({
                {' ', "Focus"},
                {'s', "Start"},
                {';', "Nav"},
                {'\n', "Toggle"}
            });
            needsRedraw_ = true;
            break;
            
        case BeaconSpamScreenState::COMPLETED:
            // Go back to config
            screenState_ = BeaconSpamScreenState::CONFIG;
            footerHints_.setHints({
                {' ', "Focus"},
                {'s', "Start"},
                {';', "Nav"},
                {'\n', "Toggle"}
            });
            needsRedraw_ = true;
            break;
    }
}

bool BeaconSpamScreen::isAttackRunning() const {
    return screenState_ == BeaconSpamScreenState::RUNNING ||
           screenState_ == BeaconSpamScreenState::PAUSED;
}

void BeaconSpamScreen::startAttack() {
    BeaconSpamConfig config;
    config.mode = mode_;
    config.channel = channel_;
    config.intervalMs = intervalMs_;
    config.randomizeBssid = randomizeBssid_;
    config.encryptedNetwork = encryptedNetwork_;
    config.channelHopEnabled = channelHopEnabled_;
    config.maxBeacons = 0;  // Infinite
    
    if (mode_ == BeaconSpamMode::SINGLE_SSID) {
        config.setSsid(customSsid_);
    }
    
    BeaconSpam& bs = BeaconSpam::getInstance();
    if (bs.start(config)) {
        screenState_ = BeaconSpamScreenState::RUNNING;
        needsRedraw_ = true;
        
#ifdef ESP32
        Serial.println("[BeaconSpamScreen] Attack started");
#endif
    }
}

void BeaconSpamScreen::stopAttack() {
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.stop();
    
#ifdef ESP32
    Serial.println("[BeaconSpamScreen] Attack stopped");
#endif
}

void BeaconSpamScreen::togglePause() {
    BeaconSpam& bs = BeaconSpam::getInstance();
    bs.togglePause();
    
    if (bs.isPaused()) {
        screenState_ = BeaconSpamScreenState::PAUSED;
    } else {
        screenState_ = BeaconSpamScreenState::RUNNING;
    }
    needsRedraw_ = true;
}

const char* BeaconSpamScreen::getModeName(BeaconSpamMode mode) const {
    switch (mode) {
        case BeaconSpamMode::SINGLE_SSID:   return "Single";
        case BeaconSpamMode::RANDOM_SSIDS:  return "Random";
        case BeaconSpamMode::SSID_LIST:     return "Custom";
        case BeaconSpamMode::RICKROLL:      return "RickRoll";
        case BeaconSpamMode::FUNNY:         return "Funny";
        case BeaconSpamMode::OFFENSIVE:     return "Offensive";
        default:                            return "Unknown";
    }
}

} // namespace adversary
