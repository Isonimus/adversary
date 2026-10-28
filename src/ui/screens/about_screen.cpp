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
{
    scrollModel_.setScrollOnly(true);  // read-only manifesto: scroll, no selection (ADR-0133)
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
    scrollModel_.setItemCount(0);  // reset scroll to the top
    scrollModel_.setItemCount(getManifestoLineCount());

    // Initialize footer hints
    footerHints_.setHints({
        {'`', "Back"}
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

    // Handle footer hints first. The model owns the clamp/no-wrap scroll math; the renderer keeps
    // its viewport in sync each frame, so here we just nudge the window up or down (ADR-0133).
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action == '`' || action == '\n') {
            shouldExit_ = true;
        } else if (action == ';') {
            scrollModel_.moveUp();
        } else if (action == '.') {
            scrollModel_.moveDown();
        }
        return true;
    }

    switch (key) {
        case ';':  // Up
            scrollModel_.moveUp();
            return true;

        case '.':  // Down
            scrollModel_.moveDown();
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
    const int16_t listTop = HEADER_HEIGHT + 4;
    const int16_t availableHeight = canvas.height() - listTop - ui::FOOTER_HEIGHT;
    const int16_t x = 4;

    canvas.setTextSize(1);

    // One manifesto line per row, drawn through the shared scroll-only list: the component owns
    // the viewport and the proportional thumb (replacing the old "N/total" readout), the model
    // owns the scroll position, and this callback only styles and prints one line (ADR-0133).
    scrollModel_.setItemCount(getManifestoLineCount());
    ScrollableList::render(canvas, scrollModel_, 0, listTop, canvas.width(), availableHeight,
        LINE_HEIGHT, [x](Canvas& c, int idx, int16_t rowY, bool /*selected*/) {
            const char* line = MANIFESTO_LINES[idx];
            if (strcmp(line, "THE ADVERSARY MANIFESTO") == 0) {
                c.setTextColor(theme::ACCENT());
            } else if (strstr(line, "TRANSPARENCY") || strstr(line, "ACCESSIBILITY") ||
                       strstr(line, "RESPONSIBILITY")) {
                c.setTextColor(theme::WARNING());
            } else if (strcmp(line, "---") == 0) {
                c.drawLine(x, rowY + 3, c.width() - x, rowY + 3, theme::TEXT_DISABLED());
                return;
            } else if (line[0] == '\0') {
                return;  // blank spacer row
            } else {
                c.setTextColor(theme::TEXT_SECONDARY());
            }
            c.setCursor(x, rowY);
            c.print(line);
        });
}

} // namespace adversary
