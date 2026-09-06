/**
 * @file screen_manager.cpp
 * @brief ScreenManager implementation
 */

#include "screen_manager.h"
#include "components/toast_manager.h"

// Canvas type comes from screen_manager.h -> screen_interface.h -> canvas_types.h

namespace adversary {

ScreenManager& ScreenManager::getInstance() {
    static ScreenManager instance;
    return instance;
}

void ScreenManager::registerScreen(ScreenId id, IScreen* screen) {
    if (screen) {
        screens_[id] = screen;
#ifdef ESP32
        Serial.printf("[ScreenManager] Registered screen: %s\n", screen->getName());
#endif
    }
}

void ScreenManager::unregisterScreen(ScreenId id) {
    screens_.erase(id);
}

IScreen* ScreenManager::getScreen(ScreenId id) {
    auto it = screens_.find(id);
    return (it != screens_.end()) ? it->second : nullptr;
}

void ScreenManager::registerFactory(ScreenId id, ScreenFactory factory) {
    factories_[id] = factory;
#ifdef ESP32
    Serial.printf("[ScreenManager] Registered factory for screen id: %d\n", static_cast<int>(id));
#endif
}

bool ScreenManager::setActiveScreen(ScreenId id, bool suppressShow) {
    // Menu is special - not a registered IScreen
    if (id == ScreenId::MENU) {
        if (activeScreen_) {
            activeScreen_->hide();
            if (ownsActiveScreen_) {
                delete activeScreen_;
            }
        }
        activeScreen_ = nullptr;
        activeScreenId_ = ScreenId::MENU;
        ownsActiveScreen_ = false;
        return true;
    }
    
    // Check factory registry first (lazy-loaded screens)
    auto factIt = factories_.find(id);
    if (factIt != factories_.end()) {
        // Hide and free previous screen
        if (activeScreen_) {
            activeScreen_->hide();
            if (ownsActiveScreen_) {
                delete activeScreen_;
            }
        }
        // Create new screen via factory
        activeScreen_ = factIt->second();
        activeScreenId_ = id;
        ownsActiveScreen_ = true;
        if (!suppressShow) {
            activeScreen_->init();
            activeScreen_->show();
        }
#ifdef ESP32
        Serial.printf("[ScreenManager] Factory-created: %s%s\n",
            activeScreen_->getName(), suppressShow ? " (show deferred)" : " (showing)");
#endif
        return true;
    }
    
    // Fall back to statically-registered screens
    auto it = screens_.find(id);
    if (it == screens_.end()) {
#ifdef ESP32
        Serial.printf("[ScreenManager] Screen not found: %d\n", static_cast<int>(id));
#endif
        return false;
    }
    
    // Hide current screen (delete if owned)
    if (activeScreen_) {
        activeScreen_->hide();
        if (ownsActiveScreen_) {
            delete activeScreen_;
        }
    }
    
    // Activate new screen (not owned)
    activeScreen_ = it->second;
    activeScreenId_ = id;
    ownsActiveScreen_ = false;
    if (!suppressShow) {
        activeScreen_->show();
    }
    
#ifdef ESP32
    Serial.printf("[ScreenManager] Switched to: %s\n", activeScreen_->getName());
#endif

    return true;
}

IScreen* ScreenManager::getActiveScreen() {
    return activeScreen_;
}

void ScreenManager::update() {
    if (activeScreen_) {
        activeScreen_->update();
    }
}

void ScreenManager::render(Canvas& canvas) {
    if (activeScreen_) {
        activeScreen_->render(canvas);
        
        // Render toast overlay on top
        ToastManager::getInstance().render(canvas);
    }
}

void ScreenManager::requestRedraw() {
    if (activeScreen_) {
        activeScreen_->requestRedraw();
    }
}

void ScreenManager::pushToDisplay(Canvas& canvas) {
#ifdef ESP32
    canvas.pushSprite(0, 0);
#else
    (void)canvas;
#endif
}

bool ScreenManager::handleInput(char key) {
    if (activeScreen_) {
        return activeScreen_->handleInput(key);
    }
    return false;
}

bool ScreenManager::shouldReturnToMenu() const {
    if (activeScreen_) {
        return activeScreen_->shouldExitToMenu();
    }
    return false;
}

void ScreenManager::returnToMenu() {
    if (activeScreen_) {
        activeScreen_->resetExitFlag();
        activeScreen_->hide();
        if (ownsActiveScreen_) {
            delete activeScreen_;
        }
    }
    activeScreen_ = nullptr;
    activeScreenId_ = ScreenId::MENU;
    ownsActiveScreen_ = false;
}

void ScreenManager::navigateTo(ScreenId id) {
    setActiveScreen(id);
}

bool ScreenManager::navigateWithParams(ScreenId id, const IScreen::ScreenParams& params) {
    // Create screen without showing first, so we can inject params before first render
    if (!setActiveScreen(id, /*suppressShow=*/true)) {
        return false;
    }
    if (activeScreen_) {
        activeScreen_->setParams(params);  // inject target data
        activeScreen_->init();             // one-time setup with params available
        activeScreen_->show();             // now render with correct target
    }
    return true;
}

} // namespace adversary
