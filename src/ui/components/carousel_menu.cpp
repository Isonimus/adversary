#include "carousel_menu.h"
#include "toast_manager.h"

namespace adversary {

CarouselMenu::CarouselMenu() : 
    selection_(0), 
    offset_(0), 
    targetOffset_(0), 
    lastAnimTime_(0) 
{
}

void CarouselMenu::setItems(const std::vector<CarouselItem>& items) {
    items_ = items;
    selection_ = 0;
    offset_ = 0;
    targetOffset_ = 0;
    lastAnimTime_ = millis();
    lastAnimTime_ = millis();
    // preloadIcons(); // Removed: Direct rendering from Flash saves 27KB RAM
    
    // Initialize footer hints
    footerHints_.setHints({
        {'\n', "Select", true}, // Enter
        {' ', "Nav", true}      // Space (toggle focus) - actually Nav is implied by ;/.
    });
    // We want to show navigation keys. FooterHints usually shows actions.
    // For navigation (Carousel), usually we show directions in the middle or just "Nav"
    // Let's match the old static string: ";/. :Nav  ENTER:Select"
    // But FooterHints is interactive. 
    // Let's add explicit actions if any, or just informational.
    // Actually, Carousel is simple: Select is the main action. 
    // And Navigation is done via keys.
    // Let's use:
    footerHints_.setHints({
        {'\n', "Select", true}
    });
    // And maybe update right content to say "Nav: ;/."?
    // footerHints_.setRightContent("Nav: ;/.");
    // Or just let FooterHints handle the standard actions.
    
    // Actually, checking the user requirement: "with the footer focus and navigation/action trigger pattern"
    // This implies interactive footer.
    // Let's go with standard SELECT for now.
    footerHints_.setHints({
        {'\n', "Open", true}
    });
}



bool CarouselMenu::handleInput(char key) {
    // Handle footer focus/input first
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction) {
            // If footer dispatched an action (e.g. Enter), handle it
            // For Carousel, '\n' from footer means "Select current item"
            // (since we only have one action in hints usually)
            if (footerAction == '\n') {
                select();
            }
            return true;
        }
        return true; // Consumed by footer navigation (focus toggle, etc)
    }

    switch (key) {
        case ';': // Left (Cardputer)
        case ',': // Left alternate
            prev();
            return true;
        case '.': // Right (Cardputer)
        case '/': // Right alternate
            next();
            return true;
        case '\n': // Enter
            select();
            return true;
        default:
            return false;
    }
}

bool CarouselMenu::handleAction(InputAction action) {
    switch (action) {
        case InputAction::LEFT:
        case InputAction::UP:
            prev();
            return true;
        case InputAction::RIGHT:
        case InputAction::DOWN:
            next();
            return true;
        case InputAction::SELECT:
            select();
            return true;
        default:
            return false;
    }
}

void CarouselMenu::next() {
    if (items_.empty() || targetOffset_ != 0) return;
    
    int nextIdx = (selection_ + 1) % items_.size();
    int startIdx = selection_;
    
    // Find next enabled item
    while (nextIdx != startIdx && !items_[nextIdx].enabled) {
        nextIdx = (nextIdx + 1) % items_.size();
    }
    
    if (nextIdx == startIdx) return; // No other enabled items
    
    targetOffset_ = -1.0f; // Animate to next
    lastAnimTime_ = millis();
}

void CarouselMenu::prev() {
    if (items_.empty() || targetOffset_ != 0) return;
    
    int prevIdx = (selection_ - 1 + items_.size()) % items_.size();
    int startIdx = selection_;
    
    // Find previous enabled item
    while (prevIdx != startIdx && !items_[prevIdx].enabled) {
        prevIdx = (prevIdx - 1 + items_.size()) % items_.size();
    }
    
    if (prevIdx == startIdx) return; // No other enabled items
    
    targetOffset_ = 1.0f; // Animate to prev
    lastAnimTime_ = millis();
}

void CarouselMenu::select() {
    if (items_.empty() || selection_ >= items_.size()) return;
    
    if (!items_[selection_].enabled) {
        // Show error toast if disabled and reason provided
        if (!items_[selection_].disabledReason.empty()) {
            showErrorToast(items_[selection_].disabledReason.c_str());
        }
        return;
    }
    
    if (callback_) {
        callback_(items_[selection_].actionId);
    }
}

} // namespace adversary
