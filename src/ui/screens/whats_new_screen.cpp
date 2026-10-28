/**
 * @file whats_new_screen.cpp
 * @brief WhatsNewScreen implementation (ADR-0121, slice-0122).
 */

#include "whats_new_screen.h"
#include "config/whats_new.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

WhatsNewScreen::WhatsNewScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
{
    scrollModel_.setScrollOnly(true);  // read-only release notes: scroll, no selection (ADR-0133)
}

WhatsNewScreen::~WhatsNewScreen() {
    hide();
}

void WhatsNewScreen::init() {
    // No initialization needed: content is a compile-time constant array.
}

void WhatsNewScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    scrollModel_.setItemCount(0);  // reset scroll to the top
    scrollModel_.setItemCount(WHATS_NEW_LINE_COUNT);

    footerHints_.setHints({
        {'`', "Back"}
    });
}

void WhatsNewScreen::hide() {
    visible_ = false;
}

void WhatsNewScreen::update() {
    // Static content: nothing to update per frame.
}

bool WhatsNewScreen::handleInput(char key) {
    needsRedraw_ = true;

    // The model owns the clamp/no-wrap scroll math; the renderer keeps its viewport in sync each
    // frame, so here we just nudge the window up or down (ADR-0133).
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

void WhatsNewScreen::render(Canvas& canvas) {
    if (!visible_) return;

#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    drawHeader(canvas);
    drawContent(canvas);
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void WhatsNewScreen::drawHeader(Canvas& canvas) {
    // Version is not shown here: it reads once in the title line, which drawContent composes
    // as "v<VERSION> - <codename>" live (slice-0124). The status bar carries the name only.
    ui::StatusBar::render(canvas, "WHAT'S NEW", nullptr,
                          theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void WhatsNewScreen::drawContent(Canvas& canvas) {
    const int16_t listTop = HEADER_HEIGHT + 4;
    const int16_t availableHeight = canvas.height() - listTop - ui::FOOTER_HEIGHT;
    const int16_t x = 4;

    canvas.setTextSize(1);

    // One release-note line per row through the shared scroll-only list: the component owns the
    // viewport and the proportional thumb (replacing the old "N/total" readout), the model owns
    // the scroll position, and this callback styles and prints one line (ADR-0133).
    scrollModel_.setItemCount(WHATS_NEW_LINE_COUNT);
    ScrollableList::render(canvas, scrollModel_, 0, listTop, canvas.width(), availableHeight,
        LINE_HEIGHT, [x](Canvas& c, int idx, int16_t rowY, bool /*selected*/) {
            const char* line = WHATS_NEW_LINES[idx];
            if (idx == 0) {
                // Release title line: the live version (config::VERSION, never hard-coded into the
                // notes body per ADR-0121) + the codename from whats_new.h line 0, composed by
                // chained prints on one line. The version shows only here now (the status bar no
                // longer carries a version subtitle), so it reads as one title (slice-0124).
                c.setTextColor(theme::ACCENT());
                c.setCursor(x, rowY);
                c.print("v");
                c.print(config::VERSION);
                c.print(" - ");
                c.print(line);
                return;
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
