/**
 * @file about_screen.cpp
 * @brief AboutScreen implementation - converted from header-only template
 */

#include "about_screen.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

const char* AboutScreen::MANIFESTO_LINES[] = {
    "THE ADVERSARY MANIFESTO",
    "",
    "To understand your defender,",
    "you must think like the attacker.",
    "",
    "We are security researchers,",
    "red teamers, and ethical hackers.",
    "We wield the tools of adversaries",
    "not to break, but to strengthen.",
    "",
    "TRANSPARENCY over obscurity.",
    "Knowledge is not a weapon when",
    "shared with those who defend.",
    "",
    "ACCESSIBILITY over gatekeeping.",
    "Advanced tools belong in the",
    "hands of those who seek to learn,",
    "to test, to improve security.",
    "",
    "RESPONSIBILITY over power.",
    "With every capability comes the",
    "duty to use it ethically, legally,",
    "and with full understanding of",
    "consequences.",
    "",
    "We test our own networks.",
    "We document vulnerabilities.",
    "We practice responsible disclosure.",
    "We make the digital world safer.",
    "",
    "This is not a weapon.",
    "This is a lens to see weakness",
    "before the truly malicious do.",
    "",
    "Think like an Adversary.",
    "Build like a Defender.",
    "",
    "---",
    "Adversary Project",
    "Open Source Security Research",
    "Built with M5Stack Cardputer",
};

int AboutScreen::getManifestoLineCount() {
    return sizeof(MANIFESTO_LINES) / sizeof(MANIFESTO_LINES[0]);
}

AboutScreen::AboutScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , scrollOffset_(0)
{
}

AboutScreen::~AboutScreen() {
    deinit();
}

void AboutScreen::init() {
    // Initialization if needed
}

void AboutScreen::deinit() {
    hide();
}

void AboutScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    scrollOffset_ = 0;
    
    // Initialize footer hints
    footerHints_.setHints({
        {'`', "Exit"}
    });
}

void AboutScreen::hide() {
    visible_ = false;
}

void AboutScreen::update() {
    // About screen has no dynamic updates
}
bool AboutScreen::handleInput(char key) {
    needsRedraw_ = true;
    
    // Handle footer hints first
    char action = 0;
    int totalLines = getManifestoLineCount();
    // Calculate visible lines (integer floor)
    int visibleLines = (config::SCREEN_HEIGHT - HEADER_HEIGHT - ui::FOOTER_HEIGHT) / LINE_HEIGHT;
    int maxScroll = (totalLines > visibleLines) ? (totalLines - visibleLines) : 0;

    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action == '`' || action == '\n') {
            shouldExit_ = true;
        } else if (action == ';') {
            if (scrollOffset_ > 0) scrollOffset_--;
        } else if (action == '.') {
            if (scrollOffset_ < maxScroll) scrollOffset_++;
        }
        return true;
    }
    
    switch (key) {
        case ';':  // Up
            if (scrollOffset_ > 0) scrollOffset_--;
            return true;
            
        case '.':  // Down
            if (scrollOffset_ < maxScroll) scrollOffset_++;
            return true;
            
        case '`':
        case '\n':
        case '\r':
            shouldExit_ = true;
            return true;
    }
    
    return false;
}

void AboutScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    drawHeader(canvas);
    drawContent(canvas);
    
    // Draw footer hints
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void AboutScreen::drawHeader(Canvas& canvas) {
    ui::StatusBar::render(canvas, "ABOUT", nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void AboutScreen::drawContent(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 4;
    int16_t screenHeight = canvas.height();
    int16_t x = 4;
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    int totalLines = getManifestoLineCount();
    
    for (int i = scrollOffset_; i < totalLines; i++) {
        // STRICT CHECK: Break if the NEXT line would even PARTIALLY overlap the footer
        if (y + LINE_HEIGHT > screenHeight - ui::FOOTER_HEIGHT) break;
        
        const char* line = MANIFESTO_LINES[i];
        
        // Style different line types
        if (strcmp(line, "THE ADVERSARY MANIFESTO") == 0) {
            canvas.setTextColor(theme::ACCENT());
        } else if (strstr(line, "TRANSPARENCY") || strstr(line, "ACCESSIBILITY") || 
                   strstr(line, "RESPONSIBILITY")) {
            canvas.setTextColor(theme::WARNING());
        } else if (strcmp(line, "---") == 0) {
            canvas.drawLine(x, y + 3, canvas.width() - x, y + 3, theme::TEXT_DISABLED());
            y += LINE_HEIGHT;
            continue;
        } else if (strcmp(line, "") == 0) {
            y += LINE_HEIGHT;
            continue;
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        
        canvas.setCursor(x, y);
        canvas.print(line);
        y += LINE_HEIGHT;
    }
    
    int visibleLines = (screenHeight - HEADER_HEIGHT - ui::FOOTER_HEIGHT) / LINE_HEIGHT;
    if (totalLines > visibleLines) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(canvas.width() - 30, HEADER_HEIGHT + 2);
        canvas.printf("%d/%d", scrollOffset_ + 1, totalLines);
    }
}

} // namespace adversary
