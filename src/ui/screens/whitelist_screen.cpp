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
    , state_(WhitelistScreenState::LIST)
    , selectedIndex_(0)
    , scrollOffset_(0)
    , deleteConfirmYes_(false)
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
    state_ = WhitelistScreenState::LIST;
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
    
    // Reset selection if needed
    if (selectedIndex_ >= static_cast<int16_t>(entries_.size())) {
        selectedIndex_ = entries_.empty() ? 0 : entries_.size() - 1;
    }
}

void WhitelistScreen::deleteSelectedEntry() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int16_t>(entries_.size())) {
        return;
    }
    
    // Remove from SettingsManager
    SettingsManager::getInstance().removeFromWhitelist(selectedIndex_);
    SettingsManager::getInstance().saveWhitelist();
    
    // Reload list
    loadWhitelist();
}

bool WhitelistScreen::handleInput(char key) {
    if (!visible_) return false;
    
    needsRedraw_ = true;
    
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // Dispatch action
            needsRedraw_ = true;
            return true;  // Consumed (navigation/toggle)
        }
    }
    
    switch (state_) {
        case WhitelistScreenState::LIST:
            switch (key) {
                case '`':  // Back
                    shouldExit_ = true;
                    return true;
                    
                case ';':  // Up
                    if (!entries_.empty()) {
                        selectedIndex_--;
                        if (selectedIndex_ < 0) {
                            selectedIndex_ = entries_.size() - 1;
                        }
                    }
                    return true;
                    
                case '.':  // Down
                    if (!entries_.empty()) {
                        selectedIndex_++;
                        if (selectedIndex_ >= static_cast<int16_t>(entries_.size())) {
                            selectedIndex_ = 0;
                        }
                    }
                    return true;
                    
                case 'd':
                case 'D':
                    if (!entries_.empty() && selectedIndex_ < static_cast<int16_t>(entries_.size())) {
                        state_ = WhitelistScreenState::DELETE_CONFIRM;
                        deleteConfirmYes_ = false;
                    }
                    return true;
            }
            break;
            
        case WhitelistScreenState::DELETE_CONFIRM:
            switch (key) {
                case '`':  // Cancel
                    state_ = WhitelistScreenState::LIST;
                    return true;
                    
                case ',':  // Left
                case ';':
                    deleteConfirmYes_ = true;
                    return true;
                    
                case '/':  // Right
                case '.':
                    deleteConfirmYes_ = false;
                    return true;
                    
                case '\n':
                case '\r':
                    if (deleteConfirmYes_) {
                        deleteSelectedEntry();
                    }
                    state_ = WhitelistScreenState::LIST;
                    return true;
            }
            break;
    }
    
    return false;
}

void WhitelistScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (state_) {
        case WhitelistScreenState::LIST:
            drawHeader(canvas, "WHITELIST");
            drawList(canvas);
            drawFooter(canvas);
            break;
            
        case WhitelistScreenState::DELETE_CONFIRM:
            drawHeader(canvas, "DELETE ENTRY?");
            drawDeleteConfirm(canvas);
            break;
    }
    
    needsRedraw_ = false;
}

void WhitelistScreen::drawHeader(Canvas& canvas, const char* title) {
    ui::StatusBar::render(canvas, title);
}

void WhitelistScreen::drawFooter(Canvas& canvas) {
    if (state_ == WhitelistScreenState::LIST) {
        footerHints_.setHints({
            {'d', "Delete", !entries_.empty()}
        });
    } else {
        footerHints_.setHints({});
    }
    footerHints_.render(canvas);
}

void WhitelistScreen::drawList(Canvas& canvas) {
    int16_t contentHeight = canvas.height() - HEADER_HEIGHT - ui::FOOTER_HEIGHT;
    int16_t visibleRows = contentHeight / ROW_HEIGHT;
    int16_t y = HEADER_HEIGHT + 4;
    
    if (entries_.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("(no entries)");
        return;
    }
    
    // Adjust scroll to keep selection visible
    if (selectedIndex_ < scrollOffset_) {
        scrollOffset_ = selectedIndex_;
    } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
        scrollOffset_ = selectedIndex_ - visibleRows + 1;
    }
    
    canvas.setTextSize(1);
    
    for (int16_t i = 0; i < visibleRows && (scrollOffset_ + i) < static_cast<int16_t>(entries_.size()); i++) {
        int16_t idx = scrollOffset_ + i;
        const auto& entry = entries_[idx];
        bool selected = (idx == selectedIndex_);
        
        // Draw selection highlight
        if (selected) {
            canvas.fillRect(0, y - 1, canvas.width(), ROW_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        
        canvas.setCursor(8, y);
        
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
        
        canvas.print(displayText);
        y += ROW_HEIGHT;
    }
    
    // Draw entry count on right
    static char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%d/%d", selectedIndex_ + 1, (int)entries_.size());
    footerHints_.setRightContent(countBuf);
}

void WhitelistScreen::drawDeleteConfirm(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 20;
    int16_t centerX = canvas.width() / 2;
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    
    // Show entry to delete
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int16_t>(entries_.size())) {
        const auto& entry = entries_[selectedIndex_];
        
        canvas.setCursor(8, y);
        canvas.print("Remove from whitelist:");
        y += 16;
        
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(8, y);
        if (entry.hasSsid && strlen(entry.ssid) > 0) {
            canvas.print(entry.ssid);
        } else if (entry.hasBssid) {
            char bssidStr[18];
            utils::formatMacBytes(entry.bssid, bssidStr, sizeof(bssidStr));
            canvas.print(bssidStr);
        }
    }
    
    y += 30;
    
    // Draw Yes/No buttons
    int16_t btnWidth = 50;
    int16_t btnHeight = 20;
    int16_t spacing = 20;
    int16_t yesX = centerX - btnWidth - spacing / 2;
    int16_t noX = centerX + spacing / 2;
    
    // Yes button
    uint16_t yesBg = deleteConfirmYes_ ? theme::ACCENT() : theme::BG_SECONDARY();
    uint16_t yesText = deleteConfirmYes_ ? theme::BG_PRIMARY() : theme::TEXT_SECONDARY();
    canvas.fillRoundRect(yesX, y, btnWidth, btnHeight, 4, yesBg);
    canvas.setTextColor(yesText);
    canvas.setCursor(yesX + 15, y + 6);
    canvas.print("Yes");
    
    // No button
    uint16_t noBg = !deleteConfirmYes_ ? theme::ACCENT() : theme::BG_SECONDARY();
    uint16_t noText = !deleteConfirmYes_ ? theme::BG_PRIMARY() : theme::TEXT_SECONDARY();
    canvas.fillRoundRect(noX, y, btnWidth, btnHeight, 4, noBg);
    canvas.setTextColor(noText);
    canvas.setCursor(noX + 18, y + 6);
    canvas.print("No");
    
    // Footer for confirm screen
    drawFooter(canvas);
}

} // namespace adversary
