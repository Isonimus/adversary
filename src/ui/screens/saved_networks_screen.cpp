/**
 * @file saved_networks_screen.cpp
 * @brief Saved WiFi credentials management screen implementation
 */

#include "saved_networks_screen.h"
#include <cstdio>
#include <algorithm>

namespace adversary {

SavedNetworksScreen::SavedNetworksScreen() {
    selectedIndex_ = 0;
    scrollOffset_ = 0;
}

void SavedNetworksScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    selectedIndex_ = 0;
    scrollOffset_ = 0;
}

void SavedNetworksScreen::hide() {
    visible_ = false;
}

void SavedNetworksScreen::update() {
    if (!visible_) return;
}

void SavedNetworksScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    
    // Header
    ui::StatusBar::render(canvas, "SAVED NETWORKS", "");
    
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    
    if (creds.empty()) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setTextSize(1);
        const char* msg = "No saved networks";
        canvas.setCursor((canvas.width() - strlen(msg) * 6) / 2, canvas.height() / 2);
        canvas.print(msg);
    } else {
        int y = HEADER_HEIGHT + 4;
        int visibleCount = (canvas.height() - HEADER_HEIGHT - FOOTER_HEIGHT) / ENTRY_HEIGHT;
        
        for (int i = 0; i < visibleCount && (scrollOffset_ + i) < (int)creds.size(); i++) {
            int idx = scrollOffset_ + i;
            const auto& cred = creds[idx];
            bool selected = (idx == selectedIndex_);
            
            int entryY = y + (i * ENTRY_HEIGHT);
            
            if (selected && !footerHints_.hasFocus()) {
                canvas.fillRect(0, entryY, canvas.width(), ENTRY_HEIGHT - 2, theme::BG_SELECTED());
            }
            
            canvas.setTextSize(1);
            canvas.setTextColor(selected ? theme::TEXT_PRIMARY() : theme::TEXT_PRIMARY());
            canvas.setCursor(4, entryY + 4);
            canvas.print(cred.ssid);
            
            canvas.setCursor(4, entryY + 14);
            canvas.setTextColor(selected ? theme::TEXT_SECONDARY() : theme::TEXT_DISABLED());
            
            // Mask password
            if (cred.password[0] == '\0') {
                canvas.print("(Open Network)");
            } else {
                canvas.print("********");
            }
        }
    }
    
    // Footer Hints
    footerHints_.setHints({
        {'\n', "Action", true},
        {'`', "Back", true}
    });
    
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%d/%d", 
             creds.empty() ? 0 : selectedIndex_ + 1, 
             (int)creds.size());
    footerHints_.setRightContent(countBuf);
    footerHints_.render(canvas);

    // Overlay action menu
    if (actionMenu_.isVisible()) {
        actionMenu_.render(canvas);
    }
#endif
}

bool SavedNetworksScreen::handleInput(char key) {
    if (actionMenu_.isVisible()) {
        if (actionMenu_.handleInput(key)) {
            needsRedraw_ = true;
            return true;
        }
    }

    needsRedraw_ = true;
    
    // Footer Hints Interactive Navigation
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction) handleAction(footerAction);
        return true;
    }

    if (footerHints_.hasFocus()) {
        return false; // Swallow inputs when footer has focus
    }

    switch (key) {
        case ';': // Up
        case 'w':
        case 'W':
            navigateUp();
            return true;
        case '.': // Down
        case 's':
        case 'S':
            navigateDown();
            return true;
        case '\n':
        case '\r':
            selectCurrent();
            return true;
        case 0x1B: // ESC
        case '`':
            shouldExit_ = true;
            return true;
    }
    
    return false;
}

void SavedNetworksScreen::navigateUp() {
    int n = (int)SettingsManager::getInstance().getSavedCredentials().size();
    if (n <= 0) return;
    selectedIndex_ = (selectedIndex_ - 1 + n) % n;  // wraps to last
    if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
    else if (selectedIndex_ >= scrollOffset_ + 4) scrollOffset_ = selectedIndex_ - 3;
}

void SavedNetworksScreen::navigateDown() {
    int n = (int)SettingsManager::getInstance().getSavedCredentials().size();
    if (n <= 0) return;
    selectedIndex_ = (selectedIndex_ + 1) % n;  // wraps to first
    // Adjust scroll offset (assume 4 visible items as a safe guess)
    if (selectedIndex_ >= scrollOffset_ + 4) scrollOffset_ = selectedIndex_ - 3;
    else if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
}

void SavedNetworksScreen::selectCurrent() {
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    if (selectedIndex_ >= 0 && selectedIndex_ < (int)creds.size()) {
        selectedCred_ = creds[selectedIndex_];
        setupActionMenu();
        actionMenu_.show();
    }
}

void SavedNetworksScreen::setupActionMenu() {
    actionMenu_.clearItems();
    actionMenu_.setTitle(selectedCred_.ssid);
    actionMenu_.addItem('C', "Connect", true);
    actionMenu_.addItem('F', "Forget Network", true);
    
    actionMenu_.setOnAction([this](char action) {
        handleAction(action);
    });
}

void SavedNetworksScreen::handleAction(char action) {
    switch (action) {
        case 'C':
        case 'c':
            Serial.printf("[SavedNetworks] Connecting to %s...\n", selectedCred_.ssid);
            WiFiConnection::getInstance().connect(selectedCred_.ssid, selectedCred_.password);
            break;
        case 'F':
        case 'f':
            Serial.printf("[SavedNetworks] Forgetting %s\n", selectedCred_.ssid);
            SettingsManager::getInstance().removeWiFiCredential(selectedIndex_);
            SettingsManager::getInstance().save();
            if (selectedIndex_ >= (int)SettingsManager::getInstance().getSavedCredentials().size() && selectedIndex_ > 0) {
                selectedIndex_--;
            }
            break;
    }
    needsRedraw_ = true;
}

} // namespace adversary
