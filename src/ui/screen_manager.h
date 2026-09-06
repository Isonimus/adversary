/**
 * @file screen_manager.h
 * @brief Centralized screen lifecycle and input management
 * 
 * Manages all application screens, handling:
 * - Screen registration and switching
 * - Update/render loop coordination  
 * - Input routing to active screen
 * - Exit flag handling
 */

#pragma once

#include "screens/screen_interface.h"
#include <functional>
#include <map>

// M5Canvas is defined via screen_interface.h -> M5GFX.h

namespace adversary {

/**
 * @brief Centralized manager for all application screens
 * 
 * Eliminates the need for giant switch statements in main.cpp by
 * providing a unified interface for screen lifecycle management.
 */
class ScreenManager {
public:
    /**
     * @brief Get singleton instance
     */
    static ScreenManager& getInstance();
    
    // Prevent copying
    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;
    
    // =========================================================================
    // Screen Registration
    // =========================================================================
    
    /**
     * @brief Register a screen with the manager
     * @param id Screen identifier
     * @param screen Pointer to screen (manager does NOT take ownership)
     */
    void registerScreen(ScreenId id, IScreen* screen);
    
    /**
     * @brief Unregister a screen
     */
    void unregisterScreen(ScreenId id);
    
    /**
     * @brief Get a registered screen by ID
     * @return Screen pointer or nullptr if not found
     */
    IScreen* getScreen(ScreenId id);
    
    // =========================================================================
    // Factory-based Lazy Loading
    // =========================================================================
    
    /**
     * @brief Factory function type for lazy screen creation
     */
    using ScreenFactory = std::function<IScreen*()>;
    
    /**
     * @brief Register a factory function for lazy/on-demand screen creation
     *
     * When navigated to, the screen will be heap-allocated via the factory
     * and deleted when the user returns to the menu.
     * @param id Screen identifier
     * @param factory Callable that returns a new heap-allocated IScreen
     */
    void registerFactory(ScreenId id, ScreenFactory factory);
    
    // =========================================================================
    // Active Screen Management
    // =========================================================================
    
    /**
     * @brief Set the active screen
     * @param id Screen to activate
     * @return true if screen exists and was activated
     */
    bool setActiveScreen(ScreenId id, bool suppressShow = false);
    
    /**
     * @brief Get the currently active screen ID
     */
    ScreenId getActiveScreenId() const { return activeScreenId_; }
    
    /**
     * @brief Get the currently active screen
     * @return Active screen pointer or nullptr
     */
    IScreen* getActiveScreen();
    
    /**
     * @brief Check if currently on menu screen
     */
    bool isOnMenu() const { return activeScreenId_ == ScreenId::MENU; }
    
    /**
     * @brief Navigate to a screen with parameters (for cross-screen data passing)
     *
     * Used when transitioning e.g. Scanner → Deauth with a pre-selected target.
     * Creates the screen via its factory (if registered), calls setParams(), then show().
     */
    bool navigateWithParams(ScreenId id, const IScreen::ScreenParams& params);
    
    // =========================================================================
    // Update/Render Cycle
    // =========================================================================
    
    /**
     * @brief Update the active screen
     * 
     * Calls update() on the active screen.
     */
    void update();
    
    /**
     * @brief Render the active screen to canvas
     * @param canvas The Canvas to render to
     * 
     * Calls render() on active screen, then renders toast overlay.
     */
    void render(Canvas& canvas);
    
    /**
     * @brief Request the active screen to redraw
     * 
     * Used by periodic timer to keep status bar badges fresh.
     */
    void requestRedraw();
    
    /**
     * @brief Push rendered canvas to display
     * @param canvas The canvas to push
     */
    void pushToDisplay(Canvas& canvas);
    
    // =========================================================================
    // Input Handling
    // =========================================================================
    
    /**
     * @brief Route input to active screen
     * @param key Pressed key character
     * @return true if input was handled
     */
    bool handleInput(char key);
    
    // =========================================================================
    // Exit Flag Management
    // =========================================================================
    
    /**
     * @brief Check if active screen wants to return to menu
     */
    bool shouldReturnToMenu() const;
    
    /**
     * @brief Return to menu and clear exit flag
     */
    void returnToMenu();

    /**
     * @brief High-level navigation helper
     * @param id The screen to navigate to
     */
    void navigateTo(ScreenId id);
    
private:
    ScreenManager() = default;
    ~ScreenManager() = default;
    
    std::map<ScreenId, IScreen*> screens_;          ///< Statically-registered screens (not owned)
    std::map<ScreenId, ScreenFactory> factories_;   ///< Lazily-loaded screen factories
    ScreenId activeScreenId_ = ScreenId::MENU;
    IScreen* activeScreen_ = nullptr;
    bool ownsActiveScreen_ = false;                 ///< true when screen was created by a factory
};

} // namespace adversary
