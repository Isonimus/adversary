/**
 * @file saved_networks_screen.cpp
 * @brief Saved WiFi credentials management screen implementation
 */

#include "saved_networks_screen.h"
#include "utils/log.h"
#include <cstdio>
#include <algorithm>

namespace adversary {

SavedNetworksScreen::SavedNetworksScreen() {
}

void SavedNetworksScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    confirmDialog_.hide();
    list_.setItemCount((int)SettingsManager::getInstance().getSavedCredentials().size());
    list_.setSelectedIndex(0);
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
        // The shared component paints the selected-row band (dimmed when the footer holds focus)
        // and the thumb, and derives the visible-row count from the rect (killing the old
        // hardcoded-"4" drift); this callback draws the two content lines (slice-0063/0065).
        int16_t y = HEADER_HEIGHT + 4;
        int16_t listH = (int16_t)(canvas.height() - y - FOOTER_HEIGHT);
        list_.setItemCount((int)creds.size());
        ScrollableList::render(canvas, list_, 0, y, canvas.width(), listH, ENTRY_HEIGHT,
            [&creds](Canvas& c, int idx, int16_t rowY, bool selected) {
                const auto& cred = creds[idx];
                c.setTextSize(1);
                c.setTextColor(theme::TEXT_PRIMARY());
                c.setCursor(4, rowY + 4);
                c.print(cred.ssid);

                // Selected row brightens the masked password from disabled to secondary.
                c.setCursor(4, rowY + 14);
                c.setTextColor(selected ? theme::TEXT_SECONDARY() : theme::TEXT_DISABLED());
                if (cred.password[0] == '\0') {
                    c.print("(Open Network)");
                } else {
                    c.print("********");
                }
            },
            !footerHints_.hasFocus());
    }
    
    // Footer Hints
    footerHints_.setHints({
        {'\n', "Action", true},
        {'`', "Back", true}
    });
    
    char countBuf[24];  // sized for the format's worst case
    snprintf(countBuf, sizeof(countBuf), "%d/%d",
             creds.empty() ? 0 : list_.selectedIndex() + 1,
             (int)creds.size());
    footerHints_.setRightContent(countBuf);
    footerHints_.render(canvas);

    // Overlay action menu
    if (actionMenu_.isVisible()) {
        actionMenu_.render(canvas);
    }
    // Confirm overlay floats above everything (ADR-0085).
    confirmDialog_.render(canvas);
#endif
}

bool SavedNetworksScreen::handleInput(char key) {
    // The confirm overlay, when up, owns all input (ADR-0085).
    if (confirmDialog_.isVisible()) {
        needsRedraw_ = true;
        return confirmDialog_.handleInput(key);
    }

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
    list_.setItemCount((int)SettingsManager::getInstance().getSavedCredentials().size());
    list_.moveUp();  // wraps to last; model keeps the selection on screen
}

void SavedNetworksScreen::navigateDown() {
    list_.setItemCount((int)SettingsManager::getInstance().getSavedCredentials().size());
    list_.moveDown();  // wraps to first; model keeps the selection on screen
}

void SavedNetworksScreen::selectCurrent() {
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    int sel = list_.selectedIndex();
    if (sel >= 0 && sel < (int)creds.size()) {
        selectedCred_ = creds[sel];
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
            LOG_I("SAVEDNET", "Connecting to %s...", selectedCred_.ssid);
            WiFiConnection::getInstance().connect(selectedCred_.ssid, selectedCred_.password);
            break;
        case 'F':
        case 'f':
            showForgetConfirm();  // gate the removal behind an explicit Confirm (ADR-0085)
            break;
    }
    needsRedraw_ = true;
}

void SavedNetworksScreen::showForgetConfirm() {
    const auto& creds = SettingsManager::getInstance().getSavedCredentials();
    int sel = list_.selectedIndex();
    if (sel < 0 || sel >= (int)creds.size()) return;

    confirmDialog_.setQuestion("Forget this network?");
    confirmDialog_.setSeverity(ui::ConfirmDialog::Severity::Destructive);
    confirmDialog_.clearDetails();
    confirmDialog_.addDetail(selectedCred_.ssid);
    confirmDialog_.setOnConfirm([this]() { forgetSelected(); });
    confirmDialog_.setOnCancel([this]() { needsRedraw_ = true; });
    confirmDialog_.show();
    needsRedraw_ = true;
}

void SavedNetworksScreen::forgetSelected() {
    LOG_I("SAVEDNET", "Forgetting %s", selectedCred_.ssid);
    SettingsManager::getInstance().removeWiFiCredential(list_.selectedIndex());
    SettingsManager::getInstance().save();
    // The model re-clamps the selection onto the now-shorter list.
    list_.setItemCount((int)SettingsManager::getInstance().getSavedCredentials().size());
    needsRedraw_ = true;
}

} // namespace adversary
