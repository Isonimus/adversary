/**
 * @file screen_interface.h
 * @brief Common interface for all screens in The Adversary
 * 
 * Defines the contract that all screens must implement for the ScreenManager
 * to handle update/render/input routing uniformly.
 */

#pragma once

#include <cstdint>
#include "../../hal/display/canvas_types.h"

namespace adversary {

/**
 * @brief Screen identifier enum
 * 
 * Used by ScreenManager to identify and switch between screens.
 */
enum class ScreenId : uint8_t {
    MENU,
    SCANNER,
    SNIFFER,
    DEAUTH,
    HANDSHAKE,
    EVIL_TWIN,
    KARMA,
    BEACON_SPAM,
    PROBE_FLOOD,
    CAPTURES,
    SETTINGS,
    ABOUT,
    WHITELIST,
    WARDRIVING,
    BLE_SCANNER,
    RFID,
    INFRARED_TVB_GONE,
    INFRARED_RECORD,
    BLE_SPAM,
    SAVED_NETWORKS,
    BLE_APPLE_ATTACK,
    BLE_BAD_BLE,
    BLE_SPOOF,
    SERVER_MENU,
    SERVER_STATUS,
    USB_BADUSB,
    HID_MOUSE_JIGGLER,
    RADIO
};

/**
 * @brief Abstract interface for all application screens
 * 
 * All screens should implement this interface to allow the ScreenManager
 * to handle update/render/input uniformly without giant switch statements.
 */
class IScreen {
public:
    virtual ~IScreen() = default;
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    
    /**
     * @brief Called when screen becomes active
     */
    virtual void show() = 0;
    
    /**
     * @brief One-time initialization of screen resources
     * 
     * Called by ScreenManager after factory creation (and after setParams()
     * if navigating with parameters). Override to set up canvases, start
     * scanning, allocate buffers, etc. Default is a no-op.
     * 
     * Lifecycle order: factory() → setParams() → init() → show()
     */
    virtual void init() {}
    
    /**
     * @brief Called when screen becomes inactive
     */
    virtual void hide() = 0;
    
    /**
     * @brief Check if screen is currently visible
     */
    virtual bool isVisible() const = 0;
    
    // =========================================================================
    // Update/Render Cycle
    // =========================================================================
    
    /**
     * @brief Update screen logic (called every frame)
     * 
     * Override to update internal state, animations, etc.
     * Default implementation does nothing.
     */
    virtual void update() {}
    
    /**
     * @brief Render screen to the provided canvas
     * @param canvas The Canvas to render to (M5Canvas/LGFX_Sprite depending on platform)
     */
    virtual void render(Canvas& canvas) = 0;
    
    /**
     * @brief Request a redraw on the next render cycle
     * 
     * Called periodically by the main loop to ensure status bar badges
     * (GPS, battery, heap, etc.) stay up-to-date even when the screen
     * has no user input. Override in screens that use a needsRedraw_ guard.
     */
    virtual void requestRedraw() {}
    
    // =========================================================================
    // Cross-Screen Parameters (for factory-based lazy loading)
    // =========================================================================

    /**
     * @brief Parameters passed when navigating to a screen from another screen
     *
     * Used for cross-screen navigation with data (e.g., Scanner → Deauth with
     * a pre-selected BSSID and SSID). Only populated when using
     * ScreenManager::navigateWithParams().
     */
    struct ScreenParams {
        uint8_t bssid[6] = {};
        char ssid[33] = {};
        uint8_t channel = 0;
    };

    /**
     * @brief Apply cross-screen navigation parameters
     *
     * Called by ScreenManager::navigateWithParams() before show().
     * Override in screens that accept target data (e.g., DeauthScreen).
     */
    virtual void setParams(const ScreenParams& /*params*/) {}
    
    // =========================================================================
    // Input Handling
    // =========================================================================
    
    /**
     * @brief Handle keyboard input
     * @param key The pressed key character
     * @return true if input was handled, false to bubble up to ScreenManager
     */
    virtual bool handleInput(char key) = 0;
    
    // =========================================================================
    // Exit Management
    // =========================================================================
    
    /**
     * @brief Check if screen wants to return to menu
     * 
     * ScreenManager checks this after update() and triggers menu return.
     */
    virtual bool shouldExitToMenu() const { return false; }
    
    /**
     * @brief Reset the exit flag after ScreenManager handles it
     */
    virtual void resetExitFlag() {}
    
    // =========================================================================
    // Screen Identity
    // =========================================================================
    
    /**
     * @brief Get human-readable screen name (for debug/logging)
     */
    virtual const char* getName() const = 0;
    
    /**
     * @brief Get the screen's unique identifier
     */
    virtual ScreenId getId() const = 0;
};

} // namespace adversary
