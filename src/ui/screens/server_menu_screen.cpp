/**
 * @file server_menu_screen.cpp
 * @brief Server Mode submenu implementation
 */

#include "server_menu_screen.h"
#include "../theme.h"
#include "modules/server/server_manager.h"
#include "../screen_manager.h"

#ifdef ESP32
#include <Arduino.h>
#endif

// Canvas management for server mode (free 64KB for network operations)
extern void adversary_ui_purge_canvas();
extern void adversary_ui_restore_canvas();

namespace adversary {

ServerMenuScreen::ServerMenuScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , selection_(0)
    , serverRunning_(false)
{
}

ServerMenuScreen::~ServerMenuScreen() {
    deinit();
}

void ServerMenuScreen::init() {
    selection_ = 0;
    needsRedraw_ = true;
    serverRunning_ = ServerManager::getInstance().isRunning();
    
    // Update menu labels based on server state
    if (serverRunning_) {
        menuLabels_[0] = "Stop Server";
        menuLabels_[1] = "Server Status";
    } else {
        menuLabels_[0] = "Start Server";
        menuLabels_[1] = "Server Status";
    }
}

void ServerMenuScreen::deinit() {
    // Nothing to clean up yet
}

void ServerMenuScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    
    footerHints_.setHints({});
    footerHints_.setFocus(false);
    
#ifdef ESP32
    Serial.println("[ServerMenu] Screen shown");
#endif
}

void ServerMenuScreen::hide() {
    visible_ = false;
    // Stop server if still running (defensive — normal path stops in handleInput)
    if (serverRunning_) {
        ServerManager::getInstance().stop();
        serverRunning_ = false;
        adversary_ui_restore_canvas();
    }
}

void ServerMenuScreen::update() {
    // Check if server state changed externally
    bool isRunning = ServerManager::getInstance().isRunning();
    if (isRunning != serverRunning_) {
        serverRunning_ = isRunning;
        needsRedraw_ = true;
    }
}

void ServerMenuScreen::render(Canvas& canvas) {
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    drawHeader(canvas);
    drawMenu(canvas);
    drawFooter(canvas);
    
    // Render action menu on top if visible
    actionMenu_.render(canvas);
#else
    (void)canvas;
#endif
}

void ServerMenuScreen::drawHeader(Canvas& canvas) {
    ui::StatusBar::render(canvas, "SERVER MODE", nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void ServerMenuScreen::drawMenu(Canvas& canvas) {
#ifdef ESP32
    int startY = HEADER_HEIGHT + 8;
    
    // Get device name for AP display
    auto& settings = SettingsManager::getInstance().get();
    
    // Show device name (will be AP SSID)
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(8, startY);
    
    if (serverRunning_) {
        canvas.printf("IP: %s", ServerManager::getInstance().getIPAddress());
        canvas.setCursor(canvas.width() - 60, startY);
        canvas.printf("Clients: %d", ServerManager::getInstance().getConnectedStations());
    } else {
        canvas.printf("AP SSID: %s", settings.system.deviceName);
    }
    startY += 16;
    
    // Draw menu items
    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = startY + (i * ITEM_HEIGHT);
        
        // Selection highlight
        if (i == selection_) {
            canvas.fillRect(4, y - 2, canvas.width() - 8, ITEM_HEIGHT, theme::BG_SECONDARY());
            canvas.setTextColor(theme::ACCENT());
        } else {
            canvas.setTextColor(theme::TEXT_PRIMARY());
        }
        
        // Draw item label
        canvas.setTextSize(1);
        canvas.setCursor(12, y + 4);
        
        // Special handling based on item
        if (i == 0) {
            // Start/Stop Server
            if (serverRunning_) {
                canvas.print("Stop Server");
            } else {
                canvas.print("Start Server");
            }
        } else if (i == 1) {
            // Server Status
            canvas.print("Server Status");
            // Show status indicator
            if (serverRunning_) {
                canvas.setTextColor(theme::SUCCESS());
                canvas.print(" [RUNNING]");
            } else {
                canvas.setTextColor(theme::TEXT_SECONDARY());
                canvas.print(" [STOPPED]");
            }
        } else if (i == 2) {
            // Configure Auth
            canvas.print("Configure Auth");
            if (settings.system.dashboardAuthEnabled) {
                canvas.setTextColor(theme::SUCCESS());
                canvas.print(" [ON]");
            } else {
                canvas.setTextColor(theme::TEXT_SECONDARY());
                canvas.print(" [OFF]");
            }
        }
    }
#else
    (void)canvas;
#endif
}

void ServerMenuScreen::drawFooter(Canvas& canvas) {
    footerHints_.render(canvas);
}

bool ServerMenuScreen::handleInput(char key) {
    needsRedraw_ = true;
    
    // Route to action menu if visible
    if (actionMenu_.isVisible()) {
        if (actionMenu_.handleInput(key)) {
            needsRedraw_ = true;
            return true;
        }
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
        case ';': // Up (wraps)
        case 'w':
            selection_ = (selection_ + MENU_ITEMS - 1) % MENU_ITEMS;
            return true;

        case '.': // Down (wraps)
        case 's':
            selection_ = (selection_ + 1) % MENU_ITEMS;
            return true;
            
        case '\r': // Enter
        case '\n':
            // Handle menu selection
            switch (selection_) {
                case 0: // Start/Stop Server
                    if (serverRunning_) {
                        ServerManager::getInstance().stop();
                        serverRunning_ = false;
                        adversary_ui_restore_canvas();
                        ToastManager::getInstance().show("Server Stopped", ToastType::INFO);
                    } else {
                        adversary_ui_purge_canvas();
                        if (ServerManager::getInstance().start()) {
                            serverRunning_ = true;
                            ToastManager::getInstance().show("Server Started", ToastType::SUCCESS);
                        } else {
                            adversary_ui_restore_canvas();
                            ToastManager::getInstance().show("Server Failed", ToastType::ERROR);
                        }
                    }
                    break;
                    
                case 1: // Server Status
                    if (!serverRunning_) {
                        ToastManager::getInstance().show("Server not running", ToastType::INFO);
                    } else {
                        ScreenManager::getInstance().setActiveScreen(ScreenId::SERVER_STATUS);
                    }
                    break;
                    
                case 2: // Configure Auth - go to settings
                    ToastManager::getInstance().show("Use Settings > Dashboard Server", ToastType::INFO);
                    break;
            }
            return true;
            
        case '`': // Back
            if (serverRunning_) {
                ServerManager::getInstance().stop();
                serverRunning_ = false;
                adversary_ui_restore_canvas();
            }
            shouldExit_ = true;
            return true;
    }
    
    return false;
}

} // namespace adversary
