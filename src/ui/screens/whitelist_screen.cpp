/**
 * @file whitelist_screen.cpp
 * @brief WhitelistScreen implementation - converted from header-only template
 */

#include "whitelist_screen.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

WhitelistScreen::WhitelistScreen()
    : visible_(false)
    , needsRedraw_(true)
    , shouldExit_(false)
{
}

WhitelistScreen::~WhitelistScreen() {
    hide();
}

void WhitelistScreen::init() {
    loadWhitelist();
}

void WhitelistScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    confirmDialog_.hide();
    loadWhitelist();
}

void WhitelistScreen::hide() {
    visible_ = false;
}

void WhitelistScreen::update() {
    // No continuous updates needed
}

void WhitelistScreen::loadWhitelist() {
    entries_.clear();
    const auto& whitelist = SettingsManager::getInstance().getWhitelist();
    for (const auto& entry : whitelist) {
        entries_.push_back(entry);
    }
    
    // The model clamps the selection onto the (possibly shrunk) list.
    entryList_.setItemCount((int)entries_.size());
}

void WhitelistScreen::deleteSelectedEntry() {
    int selected = entryList_.selectedIndex();
    if (selected < 0 || selected >= static_cast<int16_t>(entries_.size())) {
        return;
    }

    // Remove from SettingsManager
    SettingsManager::getInstance().removeFromWhitelist(selected);
    SettingsManager::getInstance().saveWhitelist();

    // Reload list
    loadWhitelist();
}

void WhitelistScreen::showDeleteConfirm() {
    int selected = entryList_.selectedIndex();
    if (entries_.empty() || selected < 0 || selected >= static_cast<int16_t>(entries_.size())) {
        return;
    }

    const auto& entry = entries_[selected];
    char detail[64] = {0};
    if (entry.hasSsid && strlen(entry.ssid) > 0) {
        snprintf(detail, sizeof(detail), "%s", entry.ssid);
    } else if (entry.hasBssid) {
        utils::formatMacBytes(entry.bssid, detail, sizeof(detail));
    } else {
        snprintf(detail, sizeof(detail), "(invalid entry)");
    }

    confirmDialog_.setQuestion("Delete whitelist entry?");
    confirmDialog_.setSeverity(ui::ConfirmDialog::Severity::Destructive);
    confirmDialog_.clearDetails();
    confirmDialog_.addDetail(detail);
    confirmDialog_.setOnConfirm([this]() {
        deleteSelectedEntry();
        needsRedraw_ = true;
    });
    confirmDialog_.setOnCancel([this]() { needsRedraw_ = true; });
    confirmDialog_.show();
}

bool WhitelistScreen::handleInput(char key) {
    if (!visible_) return false;

    needsRedraw_ = true;

    // The confirm overlay, when up, owns all input (ADR-0085).
    if (confirmDialog_.isVisible()) {
        return confirmDialog_.handleInput(key);
    }

    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // Dispatch action
            needsRedraw_ = true;
            return true;  // Consumed (navigation/toggle)
        }
    }

    switch (key) {
        case '`':  // Back
            shouldExit_ = true;
            return true;

        case ';':  // Up
            entryList_.setItemCount((int)entries_.size());
            entryList_.moveUp();  // wraps to last; model keeps the selection on screen
            return true;

        case '.':  // Down
            entryList_.setItemCount((int)entries_.size());
            entryList_.moveDown();  // wraps to first; model keeps the selection on screen
            return true;

        case 'd':
        case 'D':
            showDeleteConfirm();
            return true;
    }

    return false;
}

void WhitelistScreen::render(Canvas& canvas) {
    if (!visible_) return;

    canvas.fillScreen(theme::BG_PRIMARY());

    drawHeader(canvas, "WHITELIST");
    drawList(canvas);
    drawFooter(canvas);

    // Confirm overlay floats above the list (ADR-0085).
    confirmDialog_.render(canvas);

    needsRedraw_ = false;
}

void WhitelistScreen::drawHeader(Canvas& canvas, const char* title) {
    ui::StatusBar::render(canvas, title);
}

void WhitelistScreen::drawFooter(Canvas& canvas) {
    footerHints_.setHints({
        {'d', "Delete", !entries_.empty()}
    });
    footerHints_.render(canvas);
}

void WhitelistScreen::drawList(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 4;

    if (entries_.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("(no entries)");
        return;
    }

    canvas.setTextSize(1);

    // The shared component derives the visible-row count from the list rect (header to footer),
    // keeps the selection on screen, and draws the unified thumb; this callback paints one row.
    int16_t listHeight = canvas.height() - y - ui::FOOTER_HEIGHT;
    entryList_.setItemCount((int)entries_.size());
    ScrollableList::render(canvas, entryList_, 0, y, canvas.width(), listHeight, ROW_HEIGHT,
        [this](Canvas& c, int idx, int16_t rowY, bool /*selected*/) {
            const auto& entry = entries_[idx];
            // Selection band drawn by the component; text is content-coloured (slice-0063).
            c.setTextColor(theme::TEXT_PRIMARY());

            c.setCursor(8, rowY + theme::LIST_ROW_TEXT_DY);  // shared single-line centre (slice-0074)

            // Display SSID first, then BSSID
            char displayText[64] = {0};
            if (entry.hasSsid && strlen(entry.ssid) > 0) {
                snprintf(displayText, sizeof(displayText), "%s", entry.ssid);
                if (entry.hasBssid) {
                    // Append shortened BSSID
                    char bssidStr[20];
                    snprintf(bssidStr, sizeof(bssidStr), " [%02X:%02X:%02X]",
                             entry.bssid[0], entry.bssid[1], entry.bssid[2]);
                    strncat(displayText, bssidStr, sizeof(displayText) - strlen(displayText) - 1);
                }
            } else if (entry.hasBssid) {
                utils::formatMacBytes(entry.bssid, displayText, sizeof(displayText));
            } else {
                snprintf(displayText, sizeof(displayText), "(invalid entry)");
            }

            c.print(displayText);
        });

    // Draw entry count on right
    static char countBuf[24];  // sized for the format's worst case
    snprintf(countBuf, sizeof(countBuf), "%d/%d", entryList_.selectedIndex() + 1, (int)entries_.size());
    footerHints_.setRightContent(countBuf);
}

} // namespace adversary
