/**
 * @file probe_flood_screen.cpp
 * @brief Probe Flood Screen implementation
 */

#include "probe_flood_screen.h"
#include "ui/components/action_button.h"

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
        {'\n', "Change"}
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
    configList_.setItemCount(0);  // Reset selection + scroll to the top

    footerHints_.setHints({
        {' ', "Focus"},
        {'s', "Start"},
        {';', "Nav"},
        {'\n', "Change"}
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
    const int16_t lineHeight = theme::LIST_ROW_HEIGHT;
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
    
    // The start row (index CONFIG_OPTION_COUNT - 1) carries no label/value: the shared
    // ActionButton below supplies its own framed text (slice-0089).

    // The shared component derives the visible-row count from the list rect (below any target
    // line, down to the footer), keeps the selection on screen, and draws the unified thumb in
    // place of the old ^/v markers; this callback paints one option row.
    int16_t footerY = canvas.height() - ui::FOOTER_HEIGHT;
    configList_.setItemCount(CONFIG_OPTION_COUNT);
    ScrollableList::render(canvas, configList_, 0, y, screenWidth, footerY - y, lineHeight,
        [&options, labelX, valueX](Canvas& c, int idx, int16_t rowY, bool selected) {
            if (idx == CONFIG_OPTION_COUNT - 1) { // Terminal start row: shared green ActionButton (slice-0089)
                ui::ActionButton::render(c, rowY, "START FLOOD", selected);
                return;
            }
            // Config field: selected field reddens to ACCENT (the edit cue); band drawn by the
            // component and dimmed on footer focus via the focused flag below (slice-0063).
            c.setTextColor(selected ? theme::ACCENT() : theme::TEXT_PRIMARY());
            c.setCursor(labelX, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(options[idx].label);
            c.setCursor(valueX, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(options[idx].value);
        },
        !footerHints_.hasFocus());
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
        {'`', "Back"}
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
        case ',':  // Edit previous (ADR-0092)
        case '<':
            handleEdit(-1);
            return true;
        case '/':  // Edit next (ADR-0092)
        case '>':
            handleEdit(+1);
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
        configList_.setItemCount(CONFIG_OPTION_COUNT);
        configList_.moveUp();  // wraps to last; model keeps the selection on screen
    }
}

void ProbeFloodScreen::handleDown() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        configList_.setItemCount(CONFIG_OPTION_COUNT);
        configList_.moveDown();  // wraps to first; model keeps the selection on screen
    }
}

void ProbeFloodScreen::handleSelect() {
    if (screenState_ == ProbeFloodScreenState::CONFIG) {
        if (configList_.selectedIndex() == CONFIG_OPTION_COUNT - 1) {
            startAttack();
        } else {
            handleEdit(+1);  // value row: ENTER advances the field (ADR-0092)
        }
    } else if (screenState_ == ProbeFloodScreenState::RUNNING ||
               screenState_ == ProbeFloodScreenState::PAUSED) {
        stopAttack();
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Change"}
        });
    } else if (screenState_ == ProbeFloodScreenState::COMPLETED) {
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Change"}
        });
    }
}

void ProbeFloodScreen::handleEdit(int delta) {
    if (screenState_ != ProbeFloodScreenState::CONFIG) return;
    const bool fwd = delta > 0;
    switch (configList_.selectedIndex()) {
        case 0:  // Mode: ring of 4
            mode_ = static_cast<ProbeFloodMode>(
                (static_cast<int>(mode_) + (fwd ? 1 : 3)) % 4);
            break;
        case 1:  // Channel: 0 (hop), 1..14
            if (fwd) { channel_++; if (channel_ > 14) channel_ = 0; }
            else { channel_ = (channel_ == 0) ? 14 : channel_ - 1; }
            break;
        case 2:  // Interval: 25..500 ms, step 25
            if (fwd) { intervalMs_ += 25; if (intervalMs_ > 500) intervalMs_ = 25; }
            else { intervalMs_ = (intervalMs_ <= 25) ? 500 : intervalMs_ - 25; }
            break;
        case 3:  // Randomize MAC: toggle
            randomizeMac_ = !randomizeMac_;
            break;
        case 4:  // Channel Hop: toggle
            channelHopEnabled_ = !channelHopEnabled_;
            break;
    }
    needsRedraw_ = true;
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
            {'\n', "Change"}
        });
    } else {
        screenState_ = ProbeFloodScreenState::CONFIG;
        footerHints_.setHints({
            {' ', "Focus"},
            {'s', "Start"},
            {';', "Nav"},
            {'\n', "Change"}
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
