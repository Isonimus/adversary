/**
 * @file handshake_screen.cpp
 * @brief Handshake capture screen implementation
 */

#include "handshake_screen.h"
#include "config/config.h"
#include "hal/storage/sd_manager.h"
#include "modules/storage/capture_registry.h"
#include "modules/storage/settings_manager.h"
#include "modules/network/wpasec_service.h"
#include "modules/wifi/station_scanner.h"
#include "modules/gps/gps_manager.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast_manager.h"
#include "ui/screen_manager.h"
#include "utils/handshake_utils.h"
#include "utils/handshake_save.h"
#include "modules/storage/handshake_metadata.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#endif

namespace adversary {

HandshakeScreen::HandshakeScreen()
    : capture_(HandshakeCapture::getInstance())
    , screenState_(HandshakeScreenState::CONFIG)
    , visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , lastUpdate_(0)
    , initialized_(false)
    , hasTarget_(false)
    , targetChannel_(1)
    , configSelection_(0)
    , autoDeauth_(true)
    , deauthCount_(5)
    , timeoutSecs_(60)
    , capturing_(false)
    , handshakeSaved_(false)
{
    memset(targetBssid_, 0, 6);
    memset(targetSsid_, 0, sizeof(targetSsid_));
    memset(savedFilename_, 0, sizeof(savedFilename_));
}

HandshakeScreen::~HandshakeScreen() {
    // Unsubscribe from EventBus
    if (handshakeHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(handshakeHandlerId_);
    }
    if (stateHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(stateHandlerId_);
    }
    
    if (capturing_) {
        stopCapture();
    }
}

void HandshakeScreen::init() {
    if (initialized_) return;
    
    capture_.init();
    
    // Subscribe to EventBus for handshake capture events
    handshakeHandlerId_ = EventBus::getInstance().subscribe(
        EventType::HANDSHAKE_CAPTURED,
        [this](const EventData& evt) {
            // Build the lightweight session-list entry straight from the event
            // payload — no need to materialise a full ~1.4KB CapturedHandshake
            // just to keep SSID/BSSID/flags for the list.
            SessionHandshakeEntry hs{};
            memcpy(hs.apBssid, evt.payload.handshake.bssid, 6);
            strncpy(hs.ssid, evt.payload.handshake.ssid, 32);
            hs.ssid[32] = '\0';
            hs.channel = evt.payload.handshake.channel;
            hs.hasMsg1 = hs.hasMsg2 = hs.hasMsg3 = hs.hasMsg4 = true;
            onHandshakeCaptured(hs);
        }
    );
    
    // Subscribe to ATTACK_STATE_CHANGED for handshake state via EventBus
    if (stateHandlerId_ == 0) {
        stateHandlerId_ = EventBus::getInstance().subscribe(
            EventType::ATTACK_STATE_CHANGED,
            [this](const EventData& evt) {
                // Only handle handshake capture events (attackType 3)
                if (evt.payload.attack.attackType == static_cast<uint8_t>(AttackTypeId::HANDSHAKE)) {
                    HandshakeState state = static_cast<HandshakeState>(evt.payload.attack.newState);
                    onStateChanged(state);
                }
            }
        );
    }
    
    initialized_ = true;
    needsRedraw_ = true;
}

void HandshakeScreen::show() {
    visible_ = true;
    needsRedraw_ = true;
    
    // Always reset to CONFIG on show to clear stale capture screens
    screenState_ = HandshakeScreenState::CONFIG;
    handshakeSaved_ = false;
    
    // If GPS not detected, start non-blocking background detection
    // This avoids the 3-second blocking delay that causes UI lag
    auto& gps = GPSManager::getInstance();
    if (!gps.isDetected()) {
        gps.startBackgroundDetection();
    }
    
    footerHints_.setHints({}); // Will be set dynamically in render
    footerHints_.setFocus(false);
}

void HandshakeScreen::hide() {
    visible_ = false;

    if (capturing_) {
        stopCapture();
    }

    // Release BOTH session vectors and return their capacity to the heap.
    // Each CapturedHandshake is ~1.4KB inline (msg1-4 buffers) plus a heap
    // beaconData vector, and these grow/realloc across an Auto Hunt session —
    // a real DRAM/fragmentation cost if left resident. Previously only the
    // buffer was cleared (and .clear() keeps capacity), so the display list
    // kept the prior session's entries (stale history, count already reset)
    // and both vectors held their high-water allocation across sessions.
    // Swap-with-empty forces the backing storage to actually free.
    std::vector<SessionHandshakeEntry>().swap(sessionHandshakeBuffer_);
    std::vector<SessionHandshakeEntry>().swap(sessionHandshakes_);
    handshakeListScroll_ = 0;
    handshakeListSelection_ = 0;

    // Reset state when leaving screen
    screenState_ = HandshakeScreenState::CONFIG;
}

void HandshakeScreen::setTarget(const uint8_t* bssid, const char* ssid, uint8_t channel) {
    if (bssid) {
        memcpy(targetBssid_, bssid, 6);
    }
    if (ssid) {
        strncpy(targetSsid_, ssid, sizeof(targetSsid_) - 1);
        targetSsid_[sizeof(targetSsid_) - 1] = '\0';
    }
    targetChannel_ = channel;
    hasTarget_ = true;
    needsRedraw_ = true;
    
#ifdef ESP32
    Serial.printf("[HandshakeScreen] Target set: %s CH:%d\n", targetSsid_, targetChannel_);
#endif
}

void HandshakeScreen::update() {
    if (!visible_) return;
    
    if (capturing_) {
        if (autoHuntMode_) {
            updateAutoHunt();
        } else {
            capture_.update();
        }
        needsRedraw_ = true;  // Always redraw during capture for live stats
    }
}

bool HandshakeScreen::handleInput(char key) {
    needsRedraw_ = true;
    
    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // Dispatch action
            return true;  // Consumed (navigation/toggle)
        }
    }
    
    switch (screenState_) {
        case HandshakeScreenState::CONFIG:
            switch (key) {
                case 'M':
                case 'm':
                    autoHuntMode_ = !autoHuntMode_;
                    configSelection_ = autoHuntMode_ ? 1 : 4;
                    return true;
                    
                case ';':  // Up
                    if (configSelection_ > 0) configSelection_--;
                    return true;
                    
                case '.':  // Down
                    {
                        // Max items depends on mode: AutoHunt=2 (target, start), Manual=5 (target, 3 options, start)
                        int maxItems = autoHuntMode_ ? 2 : 5;
                        if (configSelection_ < maxItems - 1) configSelection_++;
                    }
                    return true;
                    
                case '\n':
                case '\r':
                    if (configSelection_ == 0) {
                        if (!autoHuntMode_) {
                            // Manual mode: take user to scanner to select target
                            ScreenManager::getInstance().navigateTo(ScreenId::SCANNER);
                            return true;
                        }
                        // Target row in Auto Hunt - toggle mode back to manual
                        autoHuntMode_ = !autoHuntMode_;
                        // Reset selection to start button position
                        configSelection_ = autoHuntMode_ ? 1 : 4;
                    } else if (autoHuntMode_) {
                        // Auto hunt mode - only start button at position 1
                        if (configSelection_ == 1) {
                            startCapture();  // Will start auto hunt
                        }
                    } else {
                        // Manual mode - handle options (offset by 1 due to target row)
                        switch (configSelection_) {
                            case 1:  // Auto deauth toggle
                                autoDeauth_ = !autoDeauth_;
                                break;
                            case 2:  // Deauth count
                                deauthCount_ = (deauthCount_ % 20) + 1;
                                break;
                            case 3:  // Timeout
                                timeoutSecs_ = (timeoutSecs_ == 30) ? 60 :
                                              (timeoutSecs_ == 60) ? 120 :
                                              (timeoutSecs_ == 120) ? 180 : 30;
                                break;
                            case 4:  // Start capture
                                if (hasTarget_) {
                                    startCapture();
                                }
                                break;
                        }
                    }
                    return true;
                    
                case '`':  // Back - exit to menu
                    shouldExit_ = true;
                    hide();
                    return true;
            }
            break;
            
        case HandshakeScreenState::CAPTURING:
            switch (key) {
                case '\n':
                case '\r':
                    stopCapture();
                    screenState_ = HandshakeScreenState::CONFIG;
                    return true;
                    
                case '`':  // Cancel
                    stopCapture();
                    screenState_ = HandshakeScreenState::CONFIG;
                    return true;
                    
                case 'w':
                case 'W':
                    // Add current target to whitelist and skip to next (Auto Hunt only)
                    if (autoHuntMode_ && hasTarget_) {
                        WhitelistEntry entry;
                        memcpy(entry.bssid, targetBssid_, 6);
                        entry.hasBssid = true;
                        strncpy(entry.ssid, targetSsid_, 32);
                        entry.ssid[32] = '\0';
                        entry.hasSsid = true;
                        
                        SettingsManager::getInstance().addToWhitelist(entry);
                        SettingsManager::getInstance().saveWhitelist();
#ifdef ESP32
                        Serial.printf("[AutoHunt] Whitelisted: %s\n", targetSsid_);
#endif
                        currentTargetIndex_ = -1;  // Force re-scan
                        setAutoHuntState(AutoHuntState::NEXT_TARGET);
                        needsRedraw_ = true;
                    }
                    return true;
                    
                case 'h':
                case 'H':
                    // View handshake list (Auto Hunt only)
                    if (autoHuntMode_) {
                        loadSessionHandshakes();
                        handshakeListScroll_ = 0;
                        handshakeListSelection_ = 0;
                        returnState_ = HandshakeScreenState::CAPTURING;
                        screenState_ = HandshakeScreenState::HANDSHAKE_LIST;
                    }
                    return true;
            }
            break;
            
        case HandshakeScreenState::SUCCESS:
            switch (key) {
                case '\n':
                case '\r':
                    // Save handshake
                    if (!handshakeSaved_) {
                        saveHandshake();
                    }
                    return true;
                    
                case 's':
                case 'S':
                    // New capture
                    handshakeSaved_ = false;
                    screenState_ = HandshakeScreenState::CONFIG;
                    return true;
                    
                case '`':
                    hide();
                    return true;
            }
            break;
            
        case HandshakeScreenState::TIMEOUT:
        case HandshakeScreenState::ERROR:
            switch (key) {
                case '\n':
                case '\r':
                    // Retry
                    screenState_ = HandshakeScreenState::CONFIG;
                    return true;
                    
                case '`':
                    hide();
                    return true;
            }
            break;
            
        case HandshakeScreenState::HANDSHAKE_LIST:
            switch (key) {
                case ';':  // Navigate up
                    if (handshakeListSelection_ > 0) {
                        handshakeListSelection_--;
                        if (handshakeListSelection_ < handshakeListScroll_) {
                            handshakeListScroll_ = handshakeListSelection_;
                        }
                    }
                    return true;
                    
                case '.':  // Navigate down
                    if (handshakeListSelection_ < (int)sessionHandshakes_.size() - 1) {
                        handshakeListSelection_++;
                        // Adjust scroll (assume 4 visible items)
                        const int visibleItems = 4;
                        if (handshakeListSelection_ >= handshakeListScroll_ + visibleItems) {
                            handshakeListScroll_ = handshakeListSelection_ - visibleItems + 1;
                        }
                    }
                    return true;
                    
                case 'd':
                case 'D':  // Delete handshake
                    if (!sessionHandshakes_.empty()) {
                        startDeletionConfirm();
                    }
                    return true;
                    
                case '`':  // Back to CAPTURING
                    screenState_ = returnState_;
                    return true;
            }
            break;
            
        case HandshakeScreenState::CONFIRM_DELETE:
            switch (key) {
                case 'y':
                case 'Y':  // Confirm deletion
                    executeDeletion();
                    screenState_ = HandshakeScreenState::HANDSHAKE_LIST;
                    return true;
                    
                case 'n':
                case 'N':  // Cancel deletion
                case '`':
                    deletionCandidateIndex_ = -1;
                    screenState_ = HandshakeScreenState::HANDSHAKE_LIST;
                    return true;
            }
            break;
    }
    
    return false;
}

void HandshakeScreen::startCapture() {
    // Auto Hunt mode - start scanning
    if (autoHuntMode_) {
#ifdef ESP32
        Serial.println("[HandshakeScreen] Starting Auto Hunt mode");
#endif
        // Reset stats
        handshakesCaptured_ = 0;
        pmkidsCaptured_ = 0;
        networksScanned_ = 0;
        networksSkipped_ = 0;
        currentTargetIndex_ = -1;
        attemptedCount_ = 0;  // Clear attempted networks for new session
        attemptedHead_ = 0;   // reset ring write index
        hasTarget_ = false;

        // Reset the session handshake list too — a new hunt is a new session.
        // Without this, stopping then restarting Auto Hunt (same screen instance,
        // so hide()/dtor never ran) left the prior run's captures in the list
        // even though the count above resets to 0. swap-with-empty also frees the
        // ~1.4KB-per-entry + beacon-heap capacity for the fresh run.
        std::vector<SessionHandshakeEntry>().swap(sessionHandshakeBuffer_);
        std::vector<SessionHandshakeEntry>().swap(sessionHandshakes_);
        handshakeListScroll_ = 0;
        handshakeListSelection_ = 0;
        
        // Start in SCANNING state
        autoState_ = AutoHuntState::SCANNING;
        autoStateStart_ = millis();
        
        capturing_ = true;
        handshakeSaved_ = false;
        screenState_ = HandshakeScreenState::CAPTURING;
        
        // Start WiFi scanning
        WiFi.scanNetworks(true);  // Async scan
        return;
    }
    
    // Manual mode - need target
    if (!hasTarget_) {
#ifdef ESP32
        Serial.println("[HandshakeScreen] No target set");
#endif
        return;
    }
    
    HandshakeCaptureConfig config;
    memcpy(config.targetBssid, targetBssid_, 6);
    strncpy(config.targetSsid, targetSsid_, 32);
    config.channel = targetChannel_;
    config.autoDeauth = autoDeauth_;
    config.deauthCount = deauthCount_;
    config.timeoutMs = timeoutSecs_ * 1000;
    config.captureAllClients = true;
    
    if (capture_.start(config)) {
        capturing_ = true;
        handshakeSaved_ = false;
        screenState_ = HandshakeScreenState::CAPTURING;
        
#ifdef ESP32
        Serial.println("[HandshakeScreen] Capture started");
#endif
    } else {
        screenState_ = HandshakeScreenState::ERROR;
    }
}

void HandshakeScreen::stopCapture() {
    capture_.stop();
    
    // If in Auto Hunt mode, ensure scan is cleaned up
    if (autoHuntMode_) {
        WiFi.scanDelete();
    }
    
    // CRITICAL: Force WiFi OFF to free driver memory (~30KB)
    // This safeguards against any state where WiFi was left ON (e.g. after scan)
    WiFi.mode(WIFI_OFF);
    delay(50);
    
    capturing_ = false;
    
#ifdef ESP32
    Serial.println("[HandshakeScreen] Capture stopped (WiFi OFF)");
#endif
}

void HandshakeScreen::saveHandshake() {
    // Use unified save module for consistent behavior
    auto result = handshake_save::saveHandshakeToPcap(capture_, pcapWriter_, true);
    
    if (result.success) {
        strncpy(savedFilename_, result.filename, sizeof(savedFilename_) - 1);
        handshakeSaved_ = true;
        
        // PMKID is written into the canonical "{ssid}.22000" by saveHandshakeToPcap()
        // (alongside the WPA*01 line), so no separate legacy "_XXYYZZ.22000" write.
        if (capture_.hasPMKID()) {
            pmkidsCaptured_++;
        }
        
#ifdef ESP32
        // Update WPA-SEC status if incomplete
        const auto& hs = capture_.getHandshake();
        if (!hs.isWpaSecValid()) {
            const char* slash = strrchr(savedFilename_, '/');
            const char* basename = slash ? slash + 1 : savedFilename_;
            WpaSecService::getInstance().setStatus(basename, WpaSecStatus::INCOMPLETE);
        }
#endif
    }
}

void HandshakeScreen::savePMKID22000(const CapturedPMKID& pmkid) {
#ifdef ESP32
    // Format: WPA*02*PMKID*MAC_AP*MAC_CLIENT*ESSID_HEX***
    // Mode 02 = PMKID
    
    // Create directory if needed
    if (!SD.exists(config::SD_HANDSHAKES_PATH)) {
        SD.mkdir(config::SD_HANDSHAKES_PATH);
    }
    
    // Generate filename
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/%s_%02X%02X%02X.22000",
             config::SD_HANDSHAKES_PATH,
             pmkid.ssid,
             pmkid.bssid[3], pmkid.bssid[4], pmkid.bssid[5]);
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("[PMKID] Failed to open .22000 file");
        return;
    }
    
    // WPA*02*PMKID*
    file.print("WPA*02*");
    
    // PMKID (16 bytes hex)
    for (int i = 0; i < 16; i++) {
        file.printf("%02x", pmkid.pmkid[i]);
    }
    file.print("*");
    
    // MAC_AP (6 bytes hex)
    for (int i = 0; i < 6; i++) {
        file.printf("%02x", pmkid.bssid[i]);
    }
    file.print("*");
    
    // MAC_CLIENT (6 bytes hex)
    for (int i = 0; i < 6; i++) {
        file.printf("%02x", pmkid.staMac[i]);
    }
    file.print("*");
    
    // ESSID (hex encoded)
    for (size_t i = 0; i < strlen(pmkid.ssid); i++) {
        file.printf("%02x", (uint8_t)pmkid.ssid[i]);
    }
    
    // Empty fields for message pair and additional nonces
    file.print("***\n");
    
    file.close();
    
    Serial.printf("[PMKID] Saved .22000 to: %s\n", filename);
#else
    (void)pmkid;
#endif
}

void HandshakeScreen::onHandshakeCaptured(const SessionHandshakeEntry& hs) {
    (void)hs;

#ifdef ESP32
    Serial.printf("[HandshakeScreen] Handshake captured for %s\n", hs.ssid);
#endif
    
    // Add to session buffer for handshake list view (autohunt mode)
    if (autoHuntMode_) {
        // Check for duplicates (same BSSID)
        bool isDuplicate = false;
        for (const auto& existing : sessionHandshakeBuffer_) {
            if (memcmp(existing.apBssid, hs.apBssid, 6) == 0) {
                isDuplicate = true;
                break;
            }
        }
        
        if (!isDuplicate) {
            sessionHandshakeBuffer_.push_back(hs);
#ifdef ESP32
            Serial.printf("[HandshakeScreen] Added to session buffer (%zu total)\n",
                          sessionHandshakeBuffer_.size());
#endif
        }
    }
    
    // Transition to success screen (manual mode) or continue hunting (auto mode)
    if (autoHuntMode_) {
        handshakesCaptured_++;
        // Auto hunt continues to next target
        setAutoHuntState(AutoHuntState::NEXT_TARGET);
    } else {
        // Manual mode - show success screen
        capturing_ = false;
        screenState_ = HandshakeScreenState::SUCCESS;
    }
    
    needsRedraw_ = true;
}

void HandshakeScreen::onStateChanged(HandshakeState state) {
    needsRedraw_ = true;
    
    // In Auto Hunt mode, don't change screen state on timeout/error
    // The state machine handles transitions internally
    if (autoHuntMode_) {
        return;
    }
    
    switch (state) {
        case HandshakeState::CRACKABLE:
        case HandshakeState::COMPLETE:
            // Handled by onHandshakeCaptured
            break;
            
        case HandshakeState::TIMEOUT:
            capturing_ = false;
            screenState_ = HandshakeScreenState::TIMEOUT;
            break;
            
        case HandshakeState::ERROR:
            capturing_ = false;
            screenState_ = HandshakeScreenState::ERROR;
            break;
            
        default:
            break;
    }
}

void HandshakeScreen::setAutoHuntState(AutoHuntState newState) {
#ifdef ESP32
    const char* stateNames[] = {"SCANNING", "LOCKING", "ATTACKING", "WAITING", "NEXT_TARGET", "IDLE_SCAN"};
    Serial.printf("[AutoHunt] State: %s\n", stateNames[static_cast<int>(newState)]);
    
    // Start station scanning when entering LOCKING state
    // This runs during the progress bar, making the transition seamless
    if (newState == AutoHuntState::LOCKING) {
        StationScanner::getInstance().startForAP(targetBssid_, targetChannel_, AUTO_LOCK_TIME);
    }
#endif
    autoState_ = newState;
    autoStateStart_ = millis();
}

bool HandshakeScreen::selectNextTarget() {
#ifdef ESP32
    // Get scan results
    int numNetworks = WiFi.scanComplete();
    if (numNetworks <= 0) {
        return false;
    }
    
    networksScanned_ = numNetworks;
    networksSkipped_ = 0;  // Reset per scan iteration
    
    // Find best valid target
    int bestIndex = -1;
    int bestScore = -1;
    
    for (int i = 0; i < numNetworks; i++) {
        // Skip if already captured
        String ssid = WiFi.SSID(i);
        uint8_t* bssid = WiFi.BSSID(i);
        
        if (CaptureRegistry::getInstance().hasHandshake(ssid.c_str())) {
            networksSkipped_++;
            continue;
        }
        
        // Skip if whitelisted
        if (SettingsManager::getInstance().isWhitelisted(bssid, ssid.c_str())) {
            networksSkipped_++;
            continue;
        }
        
        // Skip open networks (no handshake to capture)
        wifi_auth_mode_t authMode = WiFi.encryptionType(i);
        if (authMode == WIFI_AUTH_OPEN) {
            networksSkipped_++;
            continue;
        }
        
        // Skip networks we've already attempted in this session
        if (wasNetworkAttempted(bssid)) {
            networksSkipped_++;
            continue;
        }
        
        // Calculate priority score (signal + encryption preference)
        int score = WiFi.RSSI(i) + 100;  // Normalize RSSI to positive
        if (authMode == WIFI_AUTH_WPA2_PSK) score += 10;  // Prefer WPA2
        
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    
    if (bestIndex < 0) {
        return false;  // No valid target found
    }
    
    // Set target
    currentTargetIndex_ = bestIndex;
    memcpy(targetBssid_, WiFi.BSSID(bestIndex), 6);
    strncpy(targetSsid_, WiFi.SSID(bestIndex).c_str(), 32);
    targetSsid_[32] = '\0';
    targetChannel_ = WiFi.channel(bestIndex);
    hasTarget_ = true;
    
    // Mark this network as attempted so we don't retry it this session
    markNetworkAttempted(targetBssid_);
    
    Serial.printf("[AutoHunt] Selected target: %s (ch%d, RSSI:%ld)\n", 
                  targetSsid_, targetChannel_, (long)WiFi.RSSI(bestIndex));
    
    return true;
#else
    return false;
#endif
}

void HandshakeScreen::updateAutoHunt() {
#ifdef ESP32
    uint32_t now = millis();
    uint32_t elapsed = now - autoStateStart_;
    
    switch (autoState_) {
        case AutoHuntState::SCANNING:
            // Wait for scan to complete or timeout
            if (WiFi.scanComplete() >= 0 || elapsed >= AUTO_SCAN_TIME) {
                if (selectNextTarget()) {
                    setAutoHuntState(AutoHuntState::LOCKING);
                } else {
                    // No valid targets, go to idle scan
                    setAutoHuntState(AutoHuntState::IDLE_SCAN);
                }
            }
            break;
            
        case AutoHuntState::LOCKING:
            // Lock to channel and start capture
            // Station scanner is running during this time (started in setAutoHuntState)
            StationScanner::getInstance().update();
            
            if (elapsed >= AUTO_LOCK_TIME) {
                // Stop station scanner - results will be used by capture_.start()
                StationScanner::getInstance().stop();
                
                // Start handshake capture on this target
                HandshakeCaptureConfig config;
                memcpy(config.targetBssid, targetBssid_, 6);
                strncpy(config.targetSsid, targetSsid_, 32);
                config.channel = targetChannel_;
                config.autoDeauth = true;
                config.deauthCount = 5;
                config.timeoutMs = AUTO_ATTACK_TIME;
                config.captureAllClients = true;
                
                if (capture_.start(config)) {
                    setAutoHuntState(AutoHuntState::ATTACKING);
                } else {
                    // Failed to start, try next target
                    setAutoHuntState(AutoHuntState::NEXT_TARGET);
                }
            }
            break;
            
        case AutoHuntState::ATTACKING:
            // Update capture - success is handled by onHandshakeCaptured callback
            capture_.update();
            
            // Only handle timeout here - success triggers callback
            if (elapsed >= AUTO_ATTACK_TIME) {
                // Timeout on this target, move to next
                capture_.stop();
                setAutoHuntState(AutoHuntState::WAITING);
            }
            break;
            
        case AutoHuntState::WAITING:
            // Brief pause before next target
            // Save any captured handshake here (before NEXT_TARGET runs stop())
            if (!handshakeSaved_ && capture_.isComplete()) {
                saveHandshake();
            }
            if (elapsed >= AUTO_WAIT_TIME) {
                setAutoHuntState(AutoHuntState::NEXT_TARGET);
            }
            break;
            
        case AutoHuntState::NEXT_TARGET:
            // Save handshake if one was captured (deferred from callback)
            if (!handshakeSaved_ && capture_.isComplete()) {
                saveHandshake();
            }
            capture_.stop();
            
            // Reset saved flag for next target
            handshakeSaved_ = false;
            
            // Clear scan results and rescan
            WiFi.scanDelete();
            WiFi.scanNetworks(true);  // Async scan
            setAutoHuntState(AutoHuntState::SCANNING);
            break;
            
        case AutoHuntState::IDLE_SCAN:
            // No valid targets - wait then rescan
            if (elapsed >= AUTO_IDLE_TIME) {
                WiFi.scanDelete();
                WiFi.scanNetworks(true);
                setAutoHuntState(AutoHuntState::SCANNING);
            }
            break;
    }
#endif
}

void HandshakeScreen::markNetworkAttempted(const uint8_t* bssid) {
    // Ring buffer: when full, overwrite the OLDEST entry instead of silently
    // dropping (the old behavior, which made attempted-tracking go deaf after 16
    // APs → same targets re-selected forever, skip count stuck). Skip if already
    // tracked so re-marks don't waste a slot.
    if (wasNetworkAttempted(bssid)) return;
    memcpy(attemptedBssids_[attemptedHead_], bssid, 6);
    attemptedHead_ = (attemptedHead_ + 1) % MAX_ATTEMPTED_NETWORKS;
    if (attemptedCount_ < MAX_ATTEMPTED_NETWORKS) attemptedCount_++;
#ifdef ESP32
    Serial.printf("[AutoHunt] Marked network as attempted (%d/%d tracked)\n",
                  attemptedCount_, MAX_ATTEMPTED_NETWORKS);
#endif
}

bool HandshakeScreen::wasNetworkAttempted(const uint8_t* bssid) const {
    for (uint8_t i = 0; i < attemptedCount_; i++) {
        if (memcmp(attemptedBssids_[i], bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Handshake List Helpers
// ============================================================================

void HandshakeScreen::loadSessionHandshakes() {
    // Simply copy from session buffer (populated during autohunt via onHandshakeCaptured)
    sessionHandshakes_ = sessionHandshakeBuffer_;
    
    // Reset selection if out of bounds
    if (handshakeListSelection_ >= (int)sessionHandshakes_.size()) {
        handshakeListSelection_ = std::max(0, (int)sessionHandshakes_.size() - 1);
    }
    
#ifdef ESP32
    Serial.printf("[HandshakeScreen] Loaded %zu handshakes from session buffer\n", 
                  sessionHandshakes_.size());
#endif
}

void HandshakeScreen::startDeletionConfirm() {
    if (handshakeListSelection_ >= 0 && handshakeListSelection_ < (int)sessionHandshakes_.size()) {
        deletionCandidateIndex_ = handshakeListSelection_;
        screenState_ = HandshakeScreenState::CONFIRM_DELETE;
        needsRedraw_ = true;
    }
}

void HandshakeScreen::executeDeletion() {
#ifdef ESP32
    if (deletionCandidateIndex_ < 0 || deletionCandidateIndex_ >= (int)sessionHandshakes_.size()) {
        deletionCandidateIndex_ = -1;
        return;
    }
    
    const auto& hs = sessionHandshakes_[deletionCandidateIndex_];

    // Filenames are deterministic from the sanitized SSID ({SSID}.pcap), so derive
    // the path directly instead of scanning the directory and reading a per-file
    // JSON to match. The JSON sidecars are gone — the manifest is the source of
    // truth, and removeHandshake() tombstones the backing record + drops the row.
    char pcapPath[128];
    filename_utils::getHandshakePath(hs.ssid, pcapPath, sizeof(pcapPath));

    bool success = SD.remove(pcapPath);
    if (success) {
        CaptureRegistry::getInstance().removeHandshake(hs.ssid);
    }

    if (success) {
        // Show success toast
        ToastManager::getInstance().show("Handshake deleted", ToastType::SUCCESS, ToastPriority::PRIORITY_LOW);
        
        // Remove from session buffer
        sessionHandshakeBuffer_.erase(
            sessionHandshakeBuffer_.begin() + deletionCandidateIndex_);
        
        // Refresh list
        loadSessionHandshakes();
    } else {
        // Show error toast
        ToastManager::getInstance().show("Delete failed", ToastType::ERROR, ToastPriority::PRIORITY_MEDIUM);
    }
#endif
    
    deletionCandidateIndex_ = -1;
    needsRedraw_ = true;
}

// ============================================================================
// Rendering
// ============================================================================


void HandshakeScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
#ifdef ESP32
    uint32_t now = millis();
    if (!needsRedraw_ && (now - lastUpdate_) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    lastUpdate_ = now;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    switch (screenState_) {
        case HandshakeScreenState::CONFIG:
            drawHeader(canvas, "Handshake Capture");
            drawConfig(canvas);
            break;
            
        case HandshakeScreenState::CAPTURING:
            if (autoHuntMode_) {
                drawHeader(canvas, "Auto Hunt");
                drawAutoHuntCapturing(canvas);
            } else {
                drawHeader(canvas, "Capturing...");
                drawCapturing(canvas);
            }
            break;
            
        case HandshakeScreenState::SUCCESS:
            drawHeader(canvas, "Success!");
            drawSuccess(canvas);
            break;
            
        case HandshakeScreenState::TIMEOUT:
            drawHeader(canvas, "Timeout");
            drawTimeout(canvas);
            break;
            
        case HandshakeScreenState::ERROR:
            drawHeader(canvas, "Error");
            canvas.setTextColor(theme::ERROR());
            canvas.setCursor(10, 50);
            canvas.print("Capture failed");
            break;
            
        case HandshakeScreenState::HANDSHAKE_LIST:
            drawHeader(canvas, "Captured Handshakes");
            drawHandshakeList(canvas);
            break;
            
        case HandshakeScreenState::CONFIRM_DELETE:
            drawHeader(canvas, "Confirm Delete");
            drawDeleteConfirm(canvas);
            break;
    }

    // Set dynamic hints based on state
    switch (screenState_) {
        case HandshakeScreenState::CONFIG:
            footerHints_.setHints({
                {'M', autoHuntMode_ ? "Mode - AUTO" : "Mode - MANUAL", true},
                {'\r', "Start", true}
            });
            break;
        case HandshakeScreenState::CAPTURING:
            footerHints_.setHints({
                {'\r', "Stop", true},
                {'w', "Whitelist", autoHuntMode_ && hasTarget_},
                {'h', "History", autoHuntMode_}
            });
            break;
        case HandshakeScreenState::SUCCESS:
            footerHints_.setHints({
                {'\r', handshakeSaved_ ? "Saved" : "Save", !handshakeSaved_},
                {'s', "New", true}
            });
            break;
        case HandshakeScreenState::TIMEOUT:
        case HandshakeScreenState::ERROR:
            footerHints_.setHints({
                {'\r', "Retry", true}
            });
            break;
        case HandshakeScreenState::HANDSHAKE_LIST:
            footerHints_.setHints({
                {'d', "Delete", !sessionHandshakes_.empty()}
            });
            break;
        case HandshakeScreenState::CONFIRM_DELETE:
            footerHints_.setHints({
                {'y', "Yes", true},
                {'n', "No", true}
            });
            break;
    }
    
    // Add captures count to right content in Auto Hunt
    if (autoHuntMode_ && screenState_ == HandshakeScreenState::CAPTURING) {
        static char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%u/%u", handshakesCaptured_, pmkidsCaptured_);
        footerHints_.setRightContent(countBuf);
    } else {
        footerHints_.setRightContent("");
    }
    
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}


void HandshakeScreen::drawHeader(Canvas& canvas, const char* title) {
    // Use StatusBar with success color (green) for handshake capture
    ui::StatusBar::render(canvas, title, nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}





void HandshakeScreen::drawConfig(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 4;
    int16_t screenWidth = canvas.width();
    
    // Target mode toggle (first item)
    bool targetSelected = (configSelection_ == 0);
    if (targetSelected) {
        canvas.fillRect(0, y - 1, screenWidth, 12, theme::ACCENT());
    }
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("Target: ");
    canvas.setTextColor(targetSelected ? theme::TEXT_PRIMARY() : theme::ACCENT());
    if (autoHuntMode_) {
        canvas.print("[Auto Hunt]");
    } else if (hasTarget_) {
        canvas.print(targetSsid_);
    } else {
        canvas.setTextColor(theme::WARNING());
        canvas.print("[Select from scanner]");
    }
    y += 12;
    
    // Show target details for manual mode
    if (!autoHuntMode_ && hasTarget_) {
        char bssidStr[18];
        utils::formatMacBytes(targetBssid_, bssidStr, sizeof(bssidStr));
        canvas.setCursor(4, y);
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.printf("CH:%d  %s", targetChannel_, bssidStr);
        y += 12;
    }
    
    y += 4;
    
    if (autoHuntMode_) {
        // Auto Hunt mode - show info instead of options
        canvas.setCursor(4, y);
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.print("-- Auto Hunt Settings --");
        y += 14;
        canvas.setCursor(4, y);
        canvas.print("Uses values from Settings menu");
        y += 14;
        canvas.setCursor(4, y);
        canvas.print("Scans, targets, and captures");
        y += 14;
        canvas.setCursor(4, y);
        canvas.print("handshakes automatically");
        y += 20;
        
        // Start button
        bool startSelected = (configSelection_ == 1);
        if (startSelected) {
            canvas.fillRect(0, y - 1, screenWidth, 14, theme::SUCCESS());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::SUCCESS());
        }
        canvas.setCursor(4, y);
        canvas.print(">> START AUTO HUNT <<");
    } else {
        // Manual mode - show config options
        const char* labels[] = {
            "Auto Deauth:",
            "Deauth Count:",
            "Timeout:",
            ">> START CAPTURE <<"
        };
        
        for (int i = 0; i < 4; i++) {
            bool selected = ((i + 1) == configSelection_);
            
            if (selected) {
                canvas.fillRect(0, y - 1, screenWidth, 12, theme::ACCENT());
                canvas.setTextColor(theme::TEXT_PRIMARY());
            } else {
                canvas.setTextColor(theme::TEXT_PRIMARY());
            }
            
            canvas.setCursor(4, y);
            canvas.print(labels[i]);
            
            // Draw value for first 3 items
            if (i < 3) {
                char valueStr[16];
                switch (i) {
                    case 0: snprintf(valueStr, sizeof(valueStr), "%s", autoDeauth_ ? "ON" : "OFF"); break;
                    case 1: snprintf(valueStr, sizeof(valueStr), "%d", deauthCount_); break;
                    case 2: snprintf(valueStr, sizeof(valueStr), "%lus", timeoutSecs_); break;
                }
                
                int16_t valueX = screenWidth - strlen(valueStr) * 6 - 8;
                canvas.setCursor(valueX, y);
                canvas.print(valueStr);
            }
            
            y += 14;
        }
    }
}


void HandshakeScreen::drawCapturing(Canvas& canvas) {
    int16_t screenWidth = canvas.width();
    int16_t screenHeight = canvas.height();
    int16_t centerX = screenWidth / 2;
    int16_t centerY = (HEADER_HEIGHT + screenHeight - FOOTER_HEIGHT) / 2;
    
    const auto& stats = capture_.getStats();
    const auto& hs = capture_.getHandshake();
    
    // Progress ring
    float progress = 0.0f;
    if (hs.hasMsg1) progress += 0.25f;
    if (hs.hasMsg2) progress += 0.25f;
    if (hs.hasMsg3) progress += 0.25f;
    if (hs.hasMsg4) progress += 0.25f;
    
    drawProgressRing(canvas, centerX, centerY - 10, 20, progress);
    
    // State text
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    const char* stateStr = HandshakeCapture::getStateString(capture_.getState());
    int16_t stateLen = strlen(stateStr) * 6;
    canvas.setCursor(centerX - stateLen / 2, centerY - 5);
    canvas.print(stateStr);
    
    // Stats below
    int16_t y = centerY + 20;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.printf("EAPOL: %lu  Deauth: %lu", stats.eapolFrames, stats.deauthsSent);
    
    y += 12;
    canvas.setCursor(4, y);
    canvas.printf("M1:%lu M2:%lu M3:%lu M4:%lu", 
                  stats.msg1Count, stats.msg2Count, stats.msg3Count, stats.msg4Count);
    
    // Elapsed time
    y += 12;
    uint32_t elapsed = stats.getDurationMs() / 1000;
    canvas.setCursor(4, y);
    canvas.printf("Time: %lus / %lus", elapsed, timeoutSecs_);
}


void HandshakeScreen::drawAutoHuntCapturing(Canvas& canvas) {
    int16_t screenWidth = canvas.width();
    int16_t y = HEADER_HEIGHT + 4;
    
    // State name
    const char* stateNames[] = {"SCANNING", "LOCKING", "ATTACKING", "WAITING", "NEXT_TARGET", "IDLE_SCAN"};
    const char* stateName = stateNames[static_cast<int>(autoState_)];
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    canvas.print("State: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(stateName);
    y += 12;
    
    // Current target (if any)
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Target: ");
    if (hasTarget_) {
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.print(targetSsid_);
        canvas.printf(" (ch%d)", targetChannel_);
    } else {
        canvas.setTextColor(theme::WARNING());
        canvas.print("(scanning...)");
    }
    y += 12;
    
    // Progress bar for current state
    uint32_t stateTimeout = AUTO_SCAN_TIME;
    switch (autoState_) {
        case AutoHuntState::SCANNING: stateTimeout = AUTO_SCAN_TIME; break;
        case AutoHuntState::LOCKING: stateTimeout = AUTO_LOCK_TIME; break;
        case AutoHuntState::ATTACKING: stateTimeout = AUTO_ATTACK_TIME; break;
        case AutoHuntState::WAITING: stateTimeout = AUTO_WAIT_TIME; break;
        case AutoHuntState::IDLE_SCAN: stateTimeout = AUTO_IDLE_TIME; break;
        default: stateTimeout = 1000; break;
    }
    
#ifdef ESP32
    uint32_t elapsed = millis() - autoStateStart_;
    float progress = (float)elapsed / (float)stateTimeout;
    if (progress > 1.0f) progress = 1.0f;
    
    // Progress bar
    y += 4;
    int16_t barWidth = screenWidth - 16;
    int16_t barHeight = 8;
    canvas.drawRect(8, y, barWidth, barHeight, theme::TEXT_SECONDARY());
    canvas.fillRect(8, y, (int)(barWidth * progress), barHeight, theme::ACCENT());
    
    // Time remaining
    y += barHeight + 4;
    uint32_t remaining = (stateTimeout > elapsed) ? (stateTimeout - elapsed) / 1000 : 0;
    canvas.setCursor(4, y);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.printf("%lus remaining", remaining);
#else
    (void)stateTimeout;
#endif
    
    y += 14;
    
    // EAPOL message indicators (M1, M2, M3, M4, PMKID)
    if (autoState_ == AutoHuntState::ATTACKING) {
        const auto& hs = capture_.getHandshake();
        
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(4, y);
        canvas.print("EAPOL: ");
        
        // M1 indicator
        canvas.setTextColor(hs.hasMsg1 ? theme::SUCCESS() : theme::TEXT_DISABLED());
        canvas.print("M1 ");
        
        // M2 indicator
        canvas.setTextColor(hs.hasMsg2 ? theme::SUCCESS() : theme::TEXT_DISABLED());
        canvas.print("M2 ");
        
        // M3 indicator
        canvas.setTextColor(hs.hasMsg3 ? theme::SUCCESS() : theme::TEXT_DISABLED());
        canvas.print("M3 ");
        
        // M4 indicator
        canvas.setTextColor(hs.hasMsg4 ? theme::SUCCESS() : theme::TEXT_DISABLED());
        canvas.print("M4 ");
        
        // PMKID indicator
        if (capture_.hasPMKID()) {
            canvas.setTextColor(theme::ACCENT());
            canvas.print("[PMKID]");
        }
        
        y += 12;
    }
    
    y += 4;
    
    // Session stats
    canvas.setCursor(4, y);
    canvas.setTextColor(theme::SUCCESS());
    canvas.printf("HS: %lu", handshakesCaptured_);
    canvas.setCursor(screenWidth / 2, y);
    canvas.setTextColor(theme::ACCENT());
    canvas.printf("PMKID: %lu", pmkidsCaptured_);
    y += 12;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.printf("Networks: %lu | Skipped: %lu", networksScanned_, networksSkipped_);
}


void HandshakeScreen::drawSuccess(Canvas& canvas) {
    (void)canvas.width();  // Suppress unused warning
    int16_t y = HEADER_HEIGHT + 8;
    
    const auto& hs = capture_.getHandshake();
    const auto& stats = capture_.getStats();
    auto state = capture_.getState();
    
    // Success message with WPA-SEC readiness indicator
    bool wpaSecReady = hs.isWpaSecValid();
    canvas.setTextColor(wpaSecReady ? theme::SUCCESS() : theme::WARNING());
    canvas.setTextSize(1);
    canvas.setCursor(4, y);
    if (wpaSecReady) {
        canvas.print("WPA-SEC Ready!");
    } else {
        canvas.print("Incomplete (Missing Frames)");
    }
    
    y += 14;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(4, y);
    canvas.print(hs.ssid);
    
    y += 12;
    char clientMacStr[18];
    utils::formatMacBytes(hs.clientMac, clientMacStr, sizeof(clientMacStr));
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.printf("Client: %s", clientMacStr);
    
    y += 12;
    int x = 4;
    canvas.setCursor(x, y);
    
    // Draw frame indicators (Beacon M1 M2 M3 M4)
    auto drawIndicator = [&](const char* label, bool present) {
        canvas.setTextColor(present ? theme::SUCCESS() : theme::TEXT_DISABLED());
        canvas.print(label);
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.print(" ");
    };
    
    drawIndicator("B", hs.hasBeacon());
    drawIndicator("M1", hs.hasMsg1);
    drawIndicator("M2", hs.hasMsg2);
    drawIndicator("M3", hs.hasMsg3);
    drawIndicator("M4", hs.hasMsg4);
    
    y += 12;
    canvas.setCursor(4, y);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.printf("Time: %lums", stats.getDurationMs());
    
    // Show hint for crackable state
    if (state == HandshakeState::CRACKABLE) {
        y += 12;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(4, y);
        canvas.print("Ready for hashcat/aircrack");
    }
    
    if (handshakeSaved_) {
        y += 14;
        canvas.setTextColor(theme::SUCCESS());
        canvas.setCursor(4, y);
        canvas.print("Saved!");
    }
}


void HandshakeScreen::drawTimeout(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 20;
    
    canvas.setTextColor(theme::WARNING());
    canvas.setTextSize(1);
    canvas.setCursor(10, y);
    canvas.print("No handshake captured");
    
    y += 14;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.print("Try:");
    
    y += 12;
    canvas.setCursor(15, y);
    canvas.print("- Increase timeout");
    
    y += 10;
    canvas.setCursor(15, y);
    canvas.print("- Get closer to AP");
    
    y += 10;
    canvas.setCursor(15, y);
    canvas.print("- Wait for client activity");
}


void HandshakeScreen::drawProgressRing(Canvas& canvas, int x, int y, int radius, float progress) {
    // Draw background ring
    canvas.drawCircle(x, y, radius, theme::BG_SECONDARY());
    canvas.drawCircle(x, y, radius - 1, theme::BG_SECONDARY());
    
    // Draw progress arc (simplified - just show filled segments)
    uint16_t color = (progress >= 1.0f) ? theme::SUCCESS() : theme::ACCENT();
    
    if (progress >= 0.25f) {
        // Top-right quadrant
        canvas.fillArc(x, y, radius - 2, radius, 270, 360, color);
    }
    if (progress >= 0.5f) {
        // Bottom-right quadrant
        canvas.fillArc(x, y, radius - 2, radius, 0, 90, color);
    }
    if (progress >= 0.75f) {
        // Bottom-left quadrant
        canvas.fillArc(x, y, radius - 2, radius, 90, 180, color);
    }
    if (progress >= 1.0f) {
        // Top-left quadrant
        canvas.fillArc(x, y, radius - 2, radius, 180, 270, color);
    }
}


void HandshakeScreen::drawHandshakeList(Canvas& canvas) {
    const int16_t listY = HEADER_HEIGHT + 4;
    const int16_t listHeight = canvas.height() - HEADER_HEIGHT - FOOTER_HEIGHT - 8;
    const int16_t itemHeight = 14;
    const int16_t visibleItems = listHeight / itemHeight;
    
    if (sessionHandshakes_.empty()) {
        // Empty state
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setTextSize(1);
        const char* msg = "No handshakes captured";
        int16_t msgWidth = canvas.textWidth(msg);
        canvas.setCursor((canvas.width() - msgWidth) / 2, listY + listHeight / 2);
        canvas.print(msg);
        return;
    }
    
    canvas.setTextSize(1);
    
    // Draw each visible handshake
    for (int i = 0; i < visibleItems && (handshakeListScroll_ + i) < (int)sessionHandshakes_.size(); i++) {
        int idx = handshakeListScroll_ + i;
        const auto& hs = sessionHandshakes_[idx];
        
        int16_t y = listY + (i * itemHeight);
        bool isSelected = (idx == handshakeListSelection_);
       
        // Selection highlight
        if (isSelected) {
            canvas.fillRect(0, y - 1, canvas.width(), itemHeight, theme::ACCENT());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        
        // Type badge
        const char* typeStr = utils::getHandshakeTypeString(hs);
        canvas.setCursor(4, y + 2);
        canvas.print("[");
        if (isSelected) canvas.setTextColor(theme::SUCCESS());
        else canvas.setTextColor(theme::ACCENT());
        canvas.print(typeStr);
        if (isSelected) canvas.setTextColor(theme::TEXT_PRIMARY());
        else canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.print("]");
        
        // SSID
        canvas.setCursor(40, y + 2);
        char ssid[17];
        strncpy(ssid, (const char*)hs.ssid, 16);
        ssid[16] = '\0';
        canvas.print(ssid);
        
        // Quality indicator (if available - estimating from message count)
        canvas.setCursor(canvas.width() - 30, y + 2);
        int quality = (hs.hasMsg1 && hs.hasMsg2 && hs.hasMsg3) ? 90 : (hs.hasMsg2 ? 70 : 50);
        canvas.print(quality);
        canvas.print("%");
    }
    
    // Scroll indicator
    if ((int)sessionHandshakes_.size() > visibleItems) {
        int16_t scrollBarHeight = (visibleItems * listHeight) / sessionHandshakes_.size();
        int16_t scrollBarY = listY + (handshakeListScroll_ * listHeight) / sessionHandshakes_.size();
        canvas.fillRect(canvas.width() - 3, scrollBarY, 3, scrollBarHeight, theme::ACCENT());
    }
}


void HandshakeScreen::drawDeleteConfirm(Canvas& canvas) {
    if (deletionCandidateIndex_ < 0 || deletionCandidateIndex_ >= (int)sessionHandshakes_.size()) {
        return;
    }
    
    const auto& hs = sessionHandshakes_[deletionCandidateIndex_];
    
    int16_t y = HEADER_HEIGHT + 20;
    
    // Warning icon/text
    canvas.setTextColor(theme::WARNING());
    canvas.setTextSize(2);
    canvas.setCursor((canvas.width() - canvas.textWidth("!")) / 2, y);
    canvas.print("!");
    
    y += 20;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setTextSize(1);
    const char* msg = "Delete handshake?";
    canvas.setCursor((canvas.width() - canvas.textWidth(msg)) / 2, y);
    canvas.print(msg);
    
    y += 16;
    canvas.setTextColor(theme::ACCENT());
    char ssidLine[30];
    snprintf(ssidLine, sizeof(ssidLine), "SSID: %.16s", hs.ssid);
    canvas.setCursor((canvas.width() - canvas.textWidth(ssidLine)) / 2, y);
    canvas.print(ssidLine);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setTextSize(1);
    const char* type = utils::getHandshakeTypeString(hs);
    char typeLine[20];
    snprintf(typeLine, sizeof(typeLine), "Type: %s", type);
    canvas.setCursor((canvas.width() - canvas.textWidth(typeLine)) / 2, y);
    canvas.print(typeLine);
    
    y += 16;
    canvas.setTextColor(theme::ERROR());
    const char* warn = "This cannot be undone!";
    canvas.setCursor((canvas.width() - canvas.textWidth(warn)) / 2, y);
    canvas.print(warn);
}

} // namespace adversary
