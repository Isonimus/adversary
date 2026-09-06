/**
 * @file scanner_screen.cpp
 * @brief WiFi Scanner screen implementation
 */

#include "scanner_screen.h"
#include <algorithm>

namespace adversary {

ScannerScreen::ScannerScreen()
    : m_scanner(WiFiScanner::getInstance())
    , m_selectedIndex(0)
    , m_scrollOffset(0)
    , m_sortMode(SortMode::SIGNAL)
    , m_active(false)
    , m_shouldExit(false)
    , m_scanning(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_lastNetworkCount(0)
#ifdef ESP32
    , m_canvas(nullptr)
    , m_canvasInitialized(false)
#endif
{
}

ScannerScreen::~ScannerScreen() {
    deinit();
}

void ScannerScreen::deinit() {
    hide();
    if (m_canvas) {
        m_canvas->deleteSprite();
        delete m_canvas;
        m_canvas = nullptr;
    }
}

void ScannerScreen::init() {
    m_scanner.init();
    m_selectedIndex = 0;
    m_scrollOffset = 0;
    m_needsRedraw = true;
    m_lastNetworkCount = 0;
    m_lastUpdate = 0;  // Force redraw on next render
    
    // Delete old canvas to force recreation with fresh display reference
    if (m_canvas) {
        m_canvas->deleteSprite();
        delete m_canvas;
        m_canvas = nullptr;
    }
    m_canvasInitialized = false;
}

void ScannerScreen::setActive(bool active) {
    if (active) {
        startScan();
    } else {
        stopScan();
    }
}

void ScannerScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_selectedIndex = 0;
    m_scrollOffset = 0;
    
    // Initialize footer hints (Enter/Back are universal, not shown)
    footerHints_.setHints({
        {'R', "Rescan", true},
        {'S', "Sort:SIG", true}
    });
    footerHints_.setFocus(false);
    
    // Subscribe to scan completion event
    if (m_scanHandlerId == 0) {
        m_scanHandlerId = EventBus::getInstance().subscribe(
            EventType::WIFI_SCAN_COMPLETED,
            [this](const EventData& evt) {
                if (m_active) {
                    m_scanning = false;
                    applySort();
                    m_needsRedraw = true;
                    
                    // Keep selection in bounds
                    if (m_selectedIndex >= m_scanner.getNetworkCount() && m_scanner.getNetworkCount() > 0) {
                        m_selectedIndex = m_scanner.getNetworkCount() - 1;
                    }
                }
            }
        );
    }
    
    startScan();
}

void ScannerScreen::hide() {
    stopScan();

    // Unsubscribe from EventBus
    if (m_scanHandlerId != 0) {
        EventBus::getInstance().unsubscribe(m_scanHandlerId);
        m_scanHandlerId = 0;
    }

    // Release the WiFiScanner singleton's AP list — it outlives this screen and
    // would otherwise stay pinned in heap until the next scan. The Scanner→attack
    // handoff (Deauth/Handshake/Evil Twin) copies the chosen target into
    // ScreenParams, so it doesn't need the list after we navigate away. Mirrors
    // WardrivingScreen::hide().
    m_scanner.deinit();
}

void ScannerScreen::update() {
    if (!m_active) return;
    
    m_scanner.update();
    
    // Check if network count changed (e.g. from continuous scanning)
    if (m_scanner.getNetworkCount() != m_lastNetworkCount) {
        m_lastNetworkCount = m_scanner.getNetworkCount();
        m_needsRedraw = true;
    }
    
    m_scanning = m_scanner.isScanning();
}

bool ScannerScreen::handleInput(char key) {
    m_needsRedraw = true;
    
    // Handle password popup input
    if (m_passwordPopup.isVisible()) {
        return m_passwordPopup.handleInput(key);
    }
    
    // Handle action menu input
    if (m_actionMenu.isVisible()) {
        if (m_actionMenu.handleInput(key)) {
            return true;
        }
    }
    
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // Dispatch action
            m_needsRedraw = true;
            return true;  // Consumed (navigation/toggle)
        }
    }
    
    switch (key) {
        case ';':  // Up navigation (Cardputer keyboard)
        case 'w':
        case 'W':
            navigateUp();
            return true;
        
        case '.':  // Down navigation (Cardputer keyboard)    
            navigateDown();
            return true;
            
        case 'r':  // Rescan
        case 'R':
            toggleScan();
            return true;
            
        case 's':  // Sort
        case 'S':
            cycleSortMode();
            return true;
            
        case '\n':
        case '\r':
            selectCurrent();
            return true;
            
        // Arrow key handling (if using raw key codes)
        case 0x1B: // ESC - exit to menu
        case '`':  // Backtick (Cardputer ESC)
            if (!m_actionMenu.isVisible()) {
                m_shouldExit = true;
                return true;
            }
            return false;
            
        default:
            // Navigation with number keys for quick jump
            if (key >= '0' && key <= '9') {
                size_t target = (key == '0') ? 9 : (key - '1');
                if (target < m_scanner.getNetworkCount()) {
                    m_selectedIndex = target;
                    // Adjust scroll
                    if (m_selectedIndex < m_scrollOffset) {
                        m_scrollOffset = m_selectedIndex;
                    } else if (m_selectedIndex >= m_scrollOffset + VISIBLE_ENTRIES) {
                        m_scrollOffset = m_selectedIndex - VISIBLE_ENTRIES + 1;
                    }
                }
                return true;
            }
            break;
    }
    
    return false;
}

void ScannerScreen::navigateUp() {
    int n = (int)m_scanner.getNetworkCount();
    if (n <= 0) return;
    int sel = ((int)m_selectedIndex - 1 + n) % n;  // wraps to last
    m_selectedIndex = (size_t)sel;
    m_needsRedraw = true;
    if (sel < (int)m_scrollOffset) m_scrollOffset = sel;
    else if (sel >= (int)m_scrollOffset + VISIBLE_ENTRIES) m_scrollOffset = sel - VISIBLE_ENTRIES + 1;
}

void ScannerScreen::navigateDown() {
    int n = (int)m_scanner.getNetworkCount();
    if (n <= 0) return;
    int sel = ((int)m_selectedIndex + 1) % n;  // wraps to first
    m_selectedIndex = (size_t)sel;
    m_needsRedraw = true;
    if (sel >= (int)m_scrollOffset + VISIBLE_ENTRIES) m_scrollOffset = sel - VISIBLE_ENTRIES + 1;
    else if (sel < (int)m_scrollOffset) m_scrollOffset = sel;
}

void ScannerScreen::selectCurrent() {
    const NetworkInfo* net = m_scanner.getNetwork(m_selectedIndex);
    if (net) {
        // Store selected network for use when action is chosen
        m_selectedNetwork = *net;
        
        // Setup and show the action menu
        setupActionMenu();
        m_actionMenu.show();
        m_needsRedraw = true;
    }
}

void ScannerScreen::setupActionMenu() {
    m_actionMenu.clearItems();
    
    // Use network info as title (no subtitle)
    static char titleStr[48];
    snprintf(titleStr, sizeof(titleStr), "%s (CH:%d)", 
             m_selectedNetwork.ssid.c_str(), m_selectedNetwork.channel);
    m_actionMenu.setTitle(titleStr);
    m_actionMenu.setSubtitle(nullptr);
    
    // Add available attack options
    m_actionMenu.addItem('D', "Deauth", true);
    m_actionMenu.addItem('H', "Handshake Capture", true);
    m_actionMenu.addItem('T', "Evil Twin AP", true);
    m_actionMenu.addItem('P', "Probe Flood", true);
    
    // Connect/Disconnect option
    bool isConnected = WiFiConnection::getInstance().isConnectedTo(m_selectedNetwork.ssid.c_str());
    if (isConnected) {
        m_actionMenu.addItem('C', "Disconnect", true);
    } else {
        m_actionMenu.addItem('C', "Connect", true);
    }
    
    // Set action callback
    m_actionMenu.setOnAction([this](char action) {
        handleAction(action);
    });
    
    // Set dismiss callback to force redraw
    m_actionMenu.setOnDismiss([this]() {
        m_needsRedraw = true;
    });
}

void ScannerScreen::handleAction(char action) {
    m_needsRedraw = true;
    
    switch (action) {
        case 'D':  // Deauth Attack
        case 'd':
            Serial.printf("[Scanner] Deauth selected for %s\n", m_selectedNetwork.ssid.c_str());
            if (m_onNetworkAction) {
                m_onNetworkAction(m_selectedNetwork, NetworkAction::DEAUTH);
            } else if (m_onNetworkSelected) {
                // Legacy callback
                m_onNetworkSelected(m_selectedNetwork);
            }
            break;
            
        case 'H':  // Handshake Capture
        case 'h':
            Serial.printf("[Scanner] Handshake capture selected for %s\n", m_selectedNetwork.ssid.c_str());
            if (m_onNetworkAction) {
                m_onNetworkAction(m_selectedNetwork, NetworkAction::HANDSHAKE);
            }
            break;
            
        case 'T':  // Evil Twin
        case 't':
            Serial.printf("[Scanner] Evil Twin selected for %s\n", m_selectedNetwork.ssid.c_str());
            if (m_onNetworkAction) {
                m_onNetworkAction(m_selectedNetwork, NetworkAction::EVIL_TWIN);
            }
            break;
            
        case 'P':  // Probe Flood
        case 'p':
            Serial.printf("[Scanner] Probe Flood selected for %s\n", m_selectedNetwork.ssid.c_str());
            if (m_onNetworkAction) {
                m_onNetworkAction(m_selectedNetwork, NetworkAction::PROBE_FLOOD);
            }
            break;
            
        case 'C':  // Connect/Disconnect
        case 'c': {
            bool isConnected = WiFiConnection::getInstance().isConnectedTo(m_selectedNetwork.ssid.c_str());
            if (isConnected) {
                WiFiConnection::getInstance().disconnect();
                m_needsRedraw = true;
            } else {
                // Check if network is open (no password needed)
                if (m_selectedNetwork.security == WiFiSecurity::OPEN) {
                    Serial.printf("[Scanner] Auto-connecting to open network: %s\n", m_selectedNetwork.ssid.c_str());
                    WiFiConnection::getInstance().connect(
                        m_selectedNetwork.ssid.c_str(), 
                        "", 
                        m_selectedNetwork.channel
                    );
                    break;
                }
                
                // Check if we have saved credentials for this network
                if (SettingsManager::getInstance().hasCredentialForSSID(m_selectedNetwork.ssid.c_str())) {
                    const char* savedPass = SettingsManager::getInstance().getPasswordForSSID(m_selectedNetwork.ssid.c_str());
                    if (savedPass && savedPass[0] != '\0') {
                        Serial.printf("[Scanner] Auto-connecting with saved credentials: %s\n", m_selectedNetwork.ssid.c_str());
                        WiFiConnection::getInstance().connect(
                            m_selectedNetwork.ssid.c_str(), 
                            savedPass, 
                            m_selectedNetwork.channel
                        );
                        break;
                    }
                }
                
                // No saved password - show password popup
                static char popupTitle[48];
                snprintf(popupTitle, sizeof(popupTitle), "Connect to %s", m_selectedNetwork.ssid.c_str());
                
                // Configure callbacks before showing
                m_passwordPopup.setOnSubmit([this](const char* password) {
                    Serial.printf("[Scanner] Connecting to %s\n", m_selectedNetwork.ssid.c_str());
                    WiFiConnection::getInstance().connect(
                        m_selectedNetwork.ssid.c_str(), 
                        password, 
                        m_selectedNetwork.channel
                    );
                });
                
                m_passwordPopup.setOnCancel([this]() {
                    m_needsRedraw = true;
                });
                
                m_passwordPopup.show(popupTitle, "", true); // isPassword=true
            }
            break;
        }
            
        case 'I':  // Info
        case 'i':
            Serial.printf("[Scanner] Network Info: SSID=%s, BSSID=%02X:%02X:%02X:%02X:%02X:%02X, CH=%d, RSSI=%d\n",
                          m_selectedNetwork.ssid.c_str(),
                          m_selectedNetwork.bssid[0], m_selectedNetwork.bssid[1],
                          m_selectedNetwork.bssid[2], m_selectedNetwork.bssid[3],
                          m_selectedNetwork.bssid[4], m_selectedNetwork.bssid[5],
                          m_selectedNetwork.channel, m_selectedNetwork.rssi);
            if (m_onNetworkAction) {
                m_onNetworkAction(m_selectedNetwork, NetworkAction::INFO);
            }
            break;
    }
}

void ScannerScreen::toggleScan() {
    if (m_scanning) {
        stopScan();
    } else {
        startScan();
    }
}

void ScannerScreen::startScan() {
    m_active = true;
    m_needsRedraw = true;
    
    if (m_scanning) return; // Idempotent
    
    // Start scan without callback - we listen for EventBus event
    if (m_scanner.startScan()) {
        m_scanning = true;
    }
}

void ScannerScreen::stopScan() {
    m_active = false;
    m_scanning = false;
    m_scanner.stopScan();
    m_scanner.clearResults();
    
    // CRITICAL MEMORY MANAGEMENT:
    // Turn WiFi OFF completely to release ~30KB driver memory.
    WiFi.mode(WIFI_OFF);
    delay(50); 
    
    m_needsRedraw = true;
}

void ScannerScreen::cycleSortMode() {
    switch (m_sortMode) {
        case SortMode::SIGNAL:
            m_sortMode = SortMode::CHANNEL;
            break;
        case SortMode::CHANNEL:
            m_sortMode = SortMode::SSID;
            break;
        case SortMode::SSID:
            m_sortMode = SortMode::SIGNAL;
            break;
    }
    applySort();
    m_needsRedraw = true;
}

void ScannerScreen::applySort() {
    switch (m_sortMode) {
        case SortMode::SIGNAL:
            m_scanner.sortBySignal();
            break;
        case SortMode::CHANNEL:
            m_scanner.sortByChannel();
            break;
        case SortMode::SSID:
            m_scanner.sortBySSID();
            break;
    }
}

// =============================================================================
// Rendering
// =============================================================================

void ScannerScreen::render(Canvas& canvas) {
    if (!m_active) return;
    
#ifdef ESP32
    uint32_t now = millis();
    if (!m_needsRedraw && (now - m_lastUpdate) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    m_lastUpdate = now;
    m_needsRedraw = false;
    
    int16_t screenWidth = canvas.width();
    int16_t screenHeight = canvas.height();
    
    const auto& networks = m_scanner.getNetworks();
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    drawHeader(canvas);
    
    int16_t contentY = HEADER_HEIGHT;
    int16_t footerHeight = ui::FOOTER_HEIGHT;
    int16_t contentHeight = screenHeight - HEADER_HEIGHT - footerHeight;
    size_t maxVisible = contentHeight / ENTRY_HEIGHT;
    
    if (networks.empty()) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setTextSize(1);
        
        const char* msg = m_scanning ? "Scanning..." : "No networks found";
        int16_t textWidth = static_cast<int16_t>(strlen(msg) * 6);
        canvas.setCursor((screenWidth - textWidth) / 2, contentY + contentHeight / 2 - 4);
        canvas.print(msg);
        
        if (m_scanning) {
            static uint8_t spin = 0;
            const char spinChars[] = "|/-\\";
            canvas.setCursor((screenWidth - textWidth) / 2 - 12, contentY + contentHeight / 2 - 4);
            canvas.print(spinChars[spin++ % 4]);
        }
    } else {
        size_t endIndex = std::min(m_scrollOffset + maxVisible, networks.size());
        
        for (size_t i = m_scrollOffset; i < endIndex; i++) {
            int16_t entryY = contentY + (i - m_scrollOffset) * ENTRY_HEIGHT;
            bool isSelected = (i == m_selectedIndex);
            drawNetworkEntry(canvas, networks[i], entryY, isSelected);
        }
        
        // Scroll indicators
        if (m_scrollOffset > 0) {
            canvas.fillTriangle(screenWidth - 8, contentY + 4,
                                 screenWidth - 12, contentY + 10,
                                 screenWidth - 4, contentY + 10,
                                 theme::ACCENT());
        }
        if (endIndex < networks.size()) {
            int16_t bottomY = contentY + contentHeight - 4;
            canvas.fillTriangle(screenWidth - 8, bottomY,
                                 screenWidth - 12, bottomY - 6,
                                 screenWidth - 4, bottomY - 6,
                                 theme::ACCENT());
        }
    }
    
    // Update sort hint label with current criteria
    const char* sortLabel;
    switch (m_sortMode) {
        case SortMode::SIGNAL:  sortLabel = "Sort:SIG"; break;
        case SortMode::CHANNEL: sortLabel = "Sort:CH"; break;
        case SortMode::SSID:    sortLabel = "Sort:ABC"; break;
        default:                sortLabel = "Sort:???"; break;
    }
    footerHints_.setHints({
        {'R', "Rescan", true},
        {'S', sortLabel, true}
    });
    footerHints_.render(canvas);
    
    // Overlays
    if (m_actionMenu.isVisible()) {
        m_actionMenu.render(canvas);
    }
    if (m_passwordPopup.isVisible()) {
        m_passwordPopup.render(canvas);
    }
#else
    (void)canvas;
#endif
}

void ScannerScreen::drawHeader(Canvas& canvas) {
#ifdef ESP32
    char centerStr[24];
    if (m_scanning) {
        snprintf(centerStr, sizeof(centerStr), "* %d APs", (int)m_scanner.getNetworkCount());
    } else {
        snprintf(centerStr, sizeof(centerStr), "%d APs", (int)m_scanner.getNetworkCount());
    }
    
    ui::StatusBar::render(canvas, "WiFi Scanner", centerStr);
#else
    (void)canvas;
#endif
}

// drawStatusBar removed - replaced by FooterHints component

void ScannerScreen::drawNetworkEntry(Canvas& canvas, const NetworkInfo& net,
                                      int16_t y, bool selected) {
#ifdef ESP32
    int16_t screenWidth = canvas.width();
    
    if (selected) {
        canvas.fillRect(0, y, screenWidth, ENTRY_HEIGHT - 1, theme::BG_SELECTED());
    }
    
    drawSignalBars(canvas, 4, y + 4, net.rssi, 
                   selected ? theme::TEXT_PRIMARY() : theme::ACCENT());
    
    canvas.setTextColor(selected ? theme::TEXT_PRIMARY() : theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    canvas.setCursor(24, y + 4);
    
    if (net.isHidden || net.ssid.empty()) {
        canvas.setTextColor(selected ? theme::TEXT_SECONDARY() : theme::TEXT_DISABLED());
        canvas.print("<Hidden>");
    } else {
        if (net.ssid.length() > 18) {
            canvas.print(net.ssid.substr(0, 15).c_str());
            canvas.print("...");
        } else {
            canvas.print(net.ssid.c_str());
        }
    }
    
    canvas.setTextColor(selected ? theme::TEXT_SECONDARY() : theme::TEXT_DISABLED());
    canvas.setCursor(24, y + 14);
    
    char info[32];
    snprintf(info, sizeof(info), "CH:%02d %s %ddBm", 
             net.channel, net.getSecurityString(), net.rssi);
    canvas.print(info);
    
    // Lock icon for encrypted
    if (net.security != WiFiSecurity::OPEN) {
        canvas.setCursor(screenWidth - 10, y + 8);
        canvas.setTextColor(theme::WARNING());
        canvas.print("*");
    }
    
    // Capture indicators
    int16_t badgeY = y + (ENTRY_HEIGHT - 8) / 2;
    int indicatorX = screenWidth - 40;
    
    if (!net.isHidden && !net.ssid.empty()) {
        // Whitelisted - white W
        if (SettingsManager::getInstance().isWhitelisted(net.bssid, net.ssid.c_str())) {
            canvas.setCursor(indicatorX, badgeY);
            canvas.setTextColor(theme::TEXT_PRIMARY());
            canvas.print("W");
            indicatorX += 8;
        }
        
        // Saved password - green C
        if (SettingsManager::getInstance().hasCredentialForSSID(net.ssid.c_str())) {
            canvas.setCursor(indicatorX, badgeY);
            canvas.setTextColor(theme::SUCCESS());
            canvas.print("C");
            indicatorX += 8;
        }
        
        const auto& registry = CaptureRegistry::getInstance();
        // Handshake - orange H
        if (registry.hasHandshake(net.ssid.c_str())) {
            canvas.setCursor(indicatorX, badgeY);
            canvas.setTextColor(theme::WARNING());
            canvas.print("H");
            indicatorX += 8;
        }
        // Sniffed credentials - orange S
        if (registry.hasCredentials(net.ssid.c_str())) {
            canvas.setCursor(indicatorX, badgeY);
            canvas.setTextColor(theme::WARNING());
            canvas.print("S");
        }
    }
#else
    (void)canvas;
    (void)net;
    (void)y;
    (void)selected;
#endif
}

void ScannerScreen::drawSignalBars(Canvas& canvas, int16_t x, int16_t y,
                                    int8_t rssi, uint16_t color) {
#ifdef ESP32
    uint8_t quality = 0;
    if (rssi >= -60) quality = 4;
    else if (rssi >= -70) quality = 3;
    else if (rssi >= -80) quality = 2;
    else if (rssi >= -90) quality = 1;
    
    for (int i = 0; i < 4; i++) {
        int16_t barHeight = 4 + i * 3;
        int16_t barY = y + 12 - barHeight;
        uint16_t barColor = (i < static_cast<int>(quality)) ? color : theme::TEXT_DISABLED();
        canvas.fillRect(x + i * 4, barY, 3, barHeight, barColor);
    }
#else
    (void)canvas;
    (void)x;
    (void)y;
    (void)rssi;
    (void)color;
#endif
}

} // namespace adversary
