/**
 * @file probe_flood_screen.cpp
 * @brief Probe Flood Screen implementation
 */

#include "probe_flood_screen.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

ProbeFloodScreen::ProbeFloodScreen()
    : visible_(false)
    , initialized_(false)
    , needsRedraw_(true)
    , shouldExit_(false)
    , lastUpdate_(0)
    , screenState_(ProbeFloodScreenState::CONFIG)
    , configSelection_(0)
    , scrollOffset_(0)
    , mode_(ProbeFloodMode::RANDOM_SSIDS)
    , channel_(0)  // 0 = hopping
    , intervalMs_(50)
    , randomizeMac_(true)
    , channelHopEnabled_(true)
{
    memset(targetSsid_, 0, sizeof(targetSsid_));
    
    // Initialize footer hints
    footerHints_.setHints({
        {' ', "Focus"},
        {'s', "Start"},
        {';', "Nav"},
        {'\n', "Toggle"}
    });
}

ProbeFloodScreen::~ProbeFloodScreen() {
    hide();
}

void ProbeFloodScreen::init() {
    if (initialized_) return;
    initialized_ = true;
}

void ProbeFloodScreen::show() {
    visible_ = true;
    needsRedraw_ = true;
    shouldExit_ = false;
    screenState_ = ProbeFloodScreenState::CONFIG;
    configSelection_ = 0;
    scrollOffset_ = 0;
    
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
                // Only handle ProbeFlood events
                if (evt.payload.attack.attackType != static_cast<uint8_t>(AttackTypeId::PROBE_FLOOD)) return;
                
                ProbeFloodState newState = static_cast<ProbeFloodState>(evt.payload.attack.newState);
                
                if (newState == ProbeFloodState::COMPLETED) {
                    screenState_ = ProbeFloodScreenState::COMPLETED;
                    needsRedraw_ = true;
                } else if (newState == ProbeFloodState::PAUSED && 
                           screenState_ != ProbeFloodScreenState::PAUSED) {
                    screenState_ = ProbeFloodScreenState::PAUSED;
                    needsRedraw_ = true;
                } else if (newState == ProbeFloodState::RUNNING && 
                           screenState_ == ProbeFloodScreenState::PAUSED) {
                    screenState_ = ProbeFloodScreenState::RUNNING;
                    needsRedraw_ = true;
                }
            }
        );
    }
}

void ProbeFloodScreen::hide() {
    if (isAttackRunning()) {
        stopAttack();
    }
    
    // Unsubscribe from EventBus
    if (stateHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(stateHandlerId_);
        stateHandlerId_ = 0;
    }
    
    visible_ = false;
}

void ProbeFloodScreen::setTarget(const char* ssid, uint8_t channel) {
    if (ssid && strlen(ssid) > 0) {
        strncpy(targetSsid_, ssid, 32);
        targetSsid_[32] = '\0';
        mode_ = ProbeFloodMode::TARGETED;
        channel_ = channel;
        channelHopEnabled_ = false;
    }
}

bool ProbeFloodScreen::isAttackRunning() const {
    return ProbeFlood::getInstance().isRunning();
}

void ProbeFloodScreen::update() {
    if (!visible_) return;
    
    ProbeFlood& pf = ProbeFlood::getInstance();
    pf.update();
    
    // State changes are now handled by EventBus subscription
    
    // Force redraw for live stats
    if (screenState_ == ProbeFloodScreenState::RUNNING || 
        screenState_ == ProbeFloodScreenState::PAUSED) {
        needsRedraw_ = true;
    }
}

// =============================================================================
// Rendering
// =============================================================================

void ProbeFloodScreen::render(Canvas& canvas)
{
    if (!visible_) return;

#ifdef ESP32
    uint32_t now = millis();
    if (!needsRedraw_ && (now - lastUpdate_) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    lastUpdate_ = now;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (screenState_) {
        case ProbeFloodScreenState::CONFIG:
            drawHeader(canvas, "Probe Flood");
            drawConfig(canvas);
            break;
            
        case ProbeFloodScreenState::RUNNING:
            drawHeader(canvas, "Flooding...");
            drawRunning(canvas);
            break;
            
        case ProbeFloodScreenState::PAUSED:
            drawHeader(canvas, "Paused");
            drawRunning(canvas);
            break;
            
        case ProbeFloodScreenState::COMPLETED:
            drawHeader(canvas, "Completed");
            drawStats(canvas);
            break;
    }
    
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void ProbeFloodScreen::drawHeader(Canvas& canvas, const char* title)
{
#ifdef ESP32
    const char* modeName = getModeName(mode_);
    ui::StatusBar::render(canvas, title, modeName, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
#else
    (void)canvas;
    (void)title;
#endif
}

void ProbeFloodScreen::drawConfig(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 4;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 14;
    const int16_t labelX = 8;
    const int16_t valueX = 100;
    
    canvas.setTextSize(1);
    
    // Show target info when in Targeted mode
    if (mode_ == ProbeFloodMode::TARGETED) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(labelX, y);
        canvas.print("Target: ");
        if (strlen(targetSsid_) > 0) {
            canvas.setTextColor(theme::TEXT_PRIMARY());
            canvas.print(targetSsid_);
        } else {
            canvas.setTextColor(theme::WARNING());
            canvas.print("(Select from scanner)");
        }
        y += lineHeight;
    }
    
    // Config options
    struct ConfigOption {
        const char* label;
        char value[20];
    };
    
    ConfigOption options[CONFIG_OPTION_COUNT];
    
    options[0].label = "Mode:";
    strncpy(options[0].value, getModeName(mode_), 19);
    options[0].value[19] = '\0';
    
    options[1].label = "Channel:";
    if (channel_ == 0) {
        strncpy(options[1].value, "Hopping", 19);
    } else {
        snprintf(options[1].value, 20, "%d", channel_);
    }
    options[1].value[19] = '\0';
    
    options[2].label = "Interval:";
    snprintf(options[2].value, 20, "%lu ms", (unsigned long)intervalMs_);
    
    options[3].label = "Rand MAC:";
    strncpy(options[3].value, randomizeMac_ ? "Yes" : "No", 19);
    options[3].value[19] = '\0';
    
    options[4].label = "Ch Hopping:";
    strncpy(options[4].value, channelHopEnabled_ ? "Yes" : "No", 19);
    options[4].value[19] = '\0';
    
    options[5].label = "[START]";
    options[5].value[0] = '\0';
    
    // Calculate visible area
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    int maxVisibleItems = (footerY - y) / lineHeight;
    if (maxVisibleItems < 1) maxVisibleItems = 1;
    
    // Draw visible options
    for (int i = scrollOffset_; i < CONFIG_OPTION_COUNT && (i - scrollOffset_) < maxVisibleItems; i++) {
        bool selected = (i == configSelection_);
        
        if (selected) {
            uint16_t highlightColor = footerHints_.hasFocus() 
                ? theme::BG_SECONDARY() 
                : theme::BG_SELECTED();
            canvas.fillRect(0, y - 2, screenWidth, lineHeight, highlightColor);
        }
        
        canvas.setTextColor(selected ? theme::ACCENT() : theme::TEXT_PRIMARY());
        
        if (i == 5) {
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
    
    // Scroll indicators
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

void ProbeFloodScreen::drawRunning(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 8;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = 16;
    
    ProbeFlood& pf = ProbeFlood::getInstance();
    const ProbeFloodStats& stats = pf.getStats();
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    // Current SSID
    canvas.setCursor(8, y);
    canvas.print("SSID: ");
    canvas.setTextColor(theme::ACCENT());
    const char* currentSsid = pf.getCurrentSSID();
    if (currentSsid && strlen(currentSsid) > 0) {
        if (strlen(currentSsid) > 24) {
            char truncated[28];
            strncpy(truncated, currentSsid, 24);
            truncated[24] = '\0';
            strcat(truncated, "...");
            canvas.print(truncated);
        } else {
            canvas.print(currentSsid);
        }
    } else {
        canvas.print("<wildcard>");
    }
    y += lineHeight + 4;
    
    // Stats
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(8, y);
    canvas.printf("Probes Sent: %lu", (unsigned long)stats.probesSent);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Rate: %.1f/sec", stats.getProbesPerSecond());
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Duration: %lu sec", (unsigned long)(stats.duration / 1000));
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Channel: %d", stats.currentChannel);
    
    // Status indicator
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    y = footerY - 20;
    if (screenState_ == ProbeFloodScreenState::RUNNING) {
        canvas.fillCircle(screenWidth / 2, y, 6, theme::SUCCESS());
    } else {
        canvas.fillCircle(screenWidth / 2, y, 6, theme::WARNING());
    }
    
    // Update footer hints
    footerHints_.setHints({
        {' ', screenState_ == ProbeFloodScreenState::PAUSED ? "Resume" : "Pause"},
        {'\n', "Stop"},
        {'`', "Back"}
    });
#else
    (void)canvas;
#endif
}

void ProbeFloodScreen::drawStats(Canvas& canvas)
{
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 8;
    const int16_t lineHeight = 16;
    
    ProbeFlood& pf = ProbeFlood::getInstance();
    const ProbeFloodStats& stats = pf.getStats();
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    
    canvas.setCursor(8, y);
    canvas.print("Attack Complete!");
    y += lineHeight + 4;
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    canvas.setCursor(8, y);
    canvas.printf("Total Probes: %lu", (unsigned long)stats.probesSent);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("SSIDs Used: %lu", (unsigned long)stats.ssidsUsed);
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Duration: %lu sec", (unsigned long)(stats.duration / 1000));
    y += lineHeight;
    
    canvas.setCursor(8, y);
    canvas.printf("Avg Rate: %.1f/sec", stats.getProbesPerSecond());
    
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

bool ProbeFloodScreen::handleInput(char key) {
    if (!visible_) return false;
    
    // Handle footer hints interaction
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action != 0) {
            switch (action) {
                case 's':
                case 'S':
                    if (screenState_ == ProbeFloodScreenState::CONFIG) {
                        startAttack();
                    }
                    return true;
            }
        }
        needsRedraw_ = true;
        return true;
    }
    
    needsRedraw_ = true;
    
    switch (key) {
        case ';':  // Up
            handleUp();
            return true;
        case '.':  // Down
            handleDown();
            return true;
        case '\n':  // Enter
        case '\r':
            handleSelect();
            return true;
        case '`':  // ESC/Back
            handleBack();
            return true;
        case ' ':  // Space - pause/resume
            if (screenState_ == ProbeFloodScreenState::RUNNING ||
                screenState_ == ProbeFloodScreenState::PAUSED) {
                togglePause();
            }
            return true;
        case 's':
        case 'S':
            if (screenState_ == ProbeFloodScreenState::CONFIG) {
                startAttack();
            }
            return true;
        default:
            return false;
    }
}

void ProbeFloodScreen::handleUp() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        configSelection_--;
        if (configSelection_ < 0) {
            configSelection_ = CONFIG_OPTION_COUNT - 1;
            scrollOffset_ = CONFIG_OPTION_COUNT - VISIBLE_CONFIG_ITEMS;
            if (scrollOffset_ < 0) scrollOffset_ = 0;
        }
        if (configSelection_ < scrollOffset_) {
            scrollOffset_ = configSelection_;
        }
    }
}

void ProbeFloodScreen::handleDown() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        configSelection_++;
        if (configSelection_ >= CONFIG_OPTION_COUNT) {
            configSelection_ = 0;
            scrollOffset_ = 0;
        }
        if (configSelection_ >= scrollOffset_ + VISIBLE_CONFIG_ITEMS) {
            scrollOffset_ = configSelection_ - VISIBLE_CONFIG_ITEMS + 1;
        }
    }
}

void ProbeFloodScreen::handleSelect() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        switch (configSelection_) {
            case 0:  // Mode
                mode_ = static_cast<ProbeFloodMode>((static_cast<int>(mode_) + 1) % 4);
                break;
            case 1:  // Channel
                channel_++;
                if (channel_ > 14) channel_ = 0;  // 0 = hopping
                break;
            case 2:  // Interval
                intervalMs_ += 25;
                if (intervalMs_ > 500) intervalMs_ = 25;
                break;
            case 3:  // Randomize MAC
                randomizeMac_ = !randomizeMac_;
                break;
            case 4:  // Channel Hop
                channelHopEnabled_ = !channelHopEnabled_;
                break;
            case 5:  // Start
                startAttack();
                break;
        }
    } else if (screenState_ == ProbeFloodScreenState::RUNNING ||
               screenState_ == ProbeFloodScreenState::PAUSED) {
        stopAttack();
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Toggle"}
        });
    } else if (screenState_ == ProbeFloodScreenState::COMPLETED) {
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Toggle"}
        });
    }
}

void ProbeFloodScreen::handleBack() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        shouldExit_ = true;
    } else if (screenState_ == ProbeFloodScreenState::RUNNING ||
               screenState_ == ProbeFloodScreenState::PAUSED) {
        stopAttack();
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Toggle"}
        });
    } else {
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Toggle"}
        });
    }
}

void ProbeFloodScreen::startAttack() {
    ProbeFloodConfig config;
    config.mode = mode_;
    config.channel = channel_;
    config.intervalMs = intervalMs_;
    config.randomizeMac = randomizeMac_;
    config.channelHopEnabled = channelHopEnabled_;
    
    if (mode_ == ProbeFloodMode::TARGETED) {
        config.setSsid(targetSsid_);
    }
    
    ProbeFlood& pf = ProbeFlood::getInstance();
    if (pf.start(config)) {
        screenState_ = ProbeFloodScreenState::RUNNING;
    }
}

void ProbeFloodScreen::stopAttack() {
    ProbeFlood::getInstance().stop();
}

void ProbeFloodScreen::togglePause() {
    ProbeFlood::getInstance().togglePause();
}

const char* ProbeFloodScreen::getModeName(ProbeFloodMode mode) const {
    return getProbeFloodModeName(mode);
}

} // namespace adversary
