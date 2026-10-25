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
    , scrollOffset_(0)
{
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
    scrollOffset_ = 0;

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

    const int totalLines = WHATS_NEW_LINE_COUNT;
    const int visibleLines = (config::SCREEN_HEIGHT - HEADER_HEIGHT - ui::FOOTER_HEIGHT) / LINE_HEIGHT;
    const int maxScroll = (totalLines > visibleLines) ? (totalLines - visibleLines) : 0;

    char action = 0;
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
    int16_t y = HEADER_HEIGHT + 4;
    const int16_t screenHeight = canvas.height();
    const int16_t x = 4;

    canvas.setTextSize(1);

    const int totalLines = WHATS_NEW_LINE_COUNT;
    for (int i = scrollOffset_; i < totalLines; i++) {
        // Stop before a line would overlap the footer.
        if (y + LINE_HEIGHT > screenHeight - ui::FOOTER_HEIGHT) break;

        const char* line = WHATS_NEW_LINES[i];

        if (i == 0) {
            // Release title line: the live version (config::VERSION, never hard-coded into the
            // notes body per ADR-0121) + the codename from whats_new.h line 0, composed by
            // chained prints on one line. The version shows only here now (the status bar no
            // longer carries a version subtitle), so it reads as one title (slice-0124).
            canvas.setTextColor(theme::ACCENT());
            canvas.setCursor(x, y);
            canvas.print("v");
            canvas.print(config::VERSION);
            canvas.print(" - ");
            canvas.print(line);
            y += LINE_HEIGHT;
            continue;
        } else if (strcmp(line, "---") == 0) {
            canvas.drawLine(x, y + 3, canvas.width() - x, y + 3, theme::TEXT_DISABLED());
            y += LINE_HEIGHT;
            continue;
        } else if (line[0] == '\0') {
            y += LINE_HEIGHT;
            continue;
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }

        canvas.setCursor(x, y);
        canvas.print(line);
        y += LINE_HEIGHT;
    }

    const int visibleLines = (screenHeight - HEADER_HEIGHT - ui::FOOTER_HEIGHT) / LINE_HEIGHT;
    if (totalLines > visibleLines) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(canvas.width() - 30, HEADER_HEIGHT + 2);
        canvas.printf("%d/%d", scrollOffset_ + 1, totalLines);
    }
}

} // namespace adversary
