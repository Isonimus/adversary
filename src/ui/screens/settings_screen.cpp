/**
 * @file settings_screen.cpp
 * @brief SettingsScreen implementation - converted from header-only template
 */

#include "settings_screen.h"
#include "modules/system/system_manager.h"
#include "modules/system/time_manager.h"
#include "modules/storage/capture_registry.h"
#include "hal/storage/sd_manager.h"

#ifdef ESP32
#include <Arduino.h>
#include <M5Unified.h>
#endif

namespace adversary {

// Human-readable name for the multi-radio cap override values (slice-0002).
static const char* capOverrideName(int32_t v) {
    switch (v) {
        case static_cast<int32_t>(hal::CapOverride::ForceNone):       return "None";
        case static_cast<int32_t>(hal::CapOverride::ForceMultiRadio): return "Multi-Radio";
        default:                                                      return "Auto";
    }
}

SettingsScreen::SettingsScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , screenState_(SettingsScreenState::BROWSING)
    , selection_(0)
    , scrollOffset_(0)
    , itemCount_(0)
{
}

SettingsScreen::~SettingsScreen() {
    hide();
}

void SettingsScreen::buildSettingsList() {
    // Load current values into temp variables
    auto& settings = SettingsManager::getInstance().getMutable();
    tempKarmaChannel_ = settings.wireless.karmaChannel;
    tempKarmaAutoRotate_ = settings.wireless.karmaAutoRotate ? 1 : 0;
    tempKarmaRotationSpeed_ = settings.wireless.karmaRotationSpeed;
    tempKarmaCooldown_ = settings.wireless.karmaCooldownMin;
    tempDeauthBurst_ = settings.wireless.deauthBurstCount;
    tempDeauthInterval_ = settings.wireless.deauthIntervalMs;
    tempBeaconInterval_ = settings.wireless.beaconIntervalMs;
    tempWardrivingScanInterval_ = settings.wireless.wardrivingScanIntervalMs;
    tempBrightness_ = settings.display.brightness;
    tempToastPosition_ = settings.display.toastPosition;
    tempThemePreset_ = settings.display.themePreset;
    tempCapOverride_ = static_cast<int32_t>(settings.wireless.capOverride);
    tempSerialDebug_ = settings.system.serialDebug ? 1 : 0;
    tempNotifySounds_ = settings.system.notifySounds ? 1 : 0;
    tempNotifyLeds_ = settings.system.notifyLeds ? 1 : 0;
    tempShowHeapBadge_ = settings.system.showHeapBadge ? 1 : 0;
    tempDashboardAuthEnabled_ = settings.system.dashboardAuthEnabled ? 1 : 0;
    
    // Build items list
    itemCount_ = 0;
    
    // Wireless section
    items_[itemCount_++] = SettingItem::header("-- Wireless --");
    items_[itemCount_++] = SettingItem("Karma Channel", &tempKarmaChannel_, 1, 14, 1, "");
    items_[itemCount_++] = SettingItem::toggle("Karma Auto-Rotate", &tempKarmaAutoRotate_);
    items_[itemCount_++] = SettingItem("Rotation Speed", &tempKarmaRotationSpeed_, 0, 2, 1, "");
    items_[itemCount_++] = SettingItem("SSID Cooldown", &tempKarmaCooldown_, 0, 10, 1, "min");
    items_[itemCount_++] = SettingItem("Deauth Burst", &tempDeauthBurst_, 1, 20, 1, "");
    items_[itemCount_++] = SettingItem("Deauth Interval", &tempDeauthInterval_, 50, 500, 50, "ms");
    items_[itemCount_++] = SettingItem("Beacon Interval", &tempBeaconInterval_, 50, 500, 50, "ms");

    // Multi-radio expansion-cap detection override (slice-0002)
    snprintf(capOverrideLabelBuf_, sizeof(capOverrideLabelBuf_), "Cap: %s",
             capOverrideName(tempCapOverride_));
    items_[itemCount_++] = SettingItem(capOverrideLabelBuf_, &tempCapOverride_, 0, 2, 1, "");
    
    // BLE Configuration
    items_[itemCount_++] = SettingItem::header("-- BLE Configuration --");
    
    // Fixed MAC Item
    const char* fixedMac = settings.wireless.fixedBleMac;
    if (strlen(fixedMac) > 0) {
        snprintf(macLabelBuf_, sizeof(macLabelBuf_), "Fixed MAC: %s", fixedMac);
    } else {
        snprintf(macLabelBuf_, sizeof(macLabelBuf_), "Fixed MAC: Factory Default");
    }
    items_[itemCount_++] = SettingItem::action(macLabelBuf_);
    
    // BLE Name Item
    const char* bleName = settings.wireless.bleName;
    snprintf(bleNameLabelBuf_, sizeof(bleNameLabelBuf_), "Name: %s", bleName[0] ? bleName : "Universal Key");
    items_[itemCount_++] = SettingItem::action(bleNameLabelBuf_);
    
    // Wardriving section
    items_[itemCount_++] = SettingItem::header("-- Wardriving --");
    items_[itemCount_++] = SettingItem("Scan Interval", &tempWardrivingScanInterval_, 1000, 10000, 500, "ms");
    
    // Display section
    items_[itemCount_++] = SettingItem::header("-- Display --");
    items_[itemCount_++] = SettingItem("Brightness", &tempBrightness_, 10, 100, 10, "%");
    items_[itemCount_++] = SettingItem("Toast Position", &tempToastPosition_, 0, 2, 1, "");
    
    // Theme selector with label showing current theme name
    snprintf(themeLabelBuf_, sizeof(themeLabelBuf_), "Theme: %s", getThemeName(static_cast<ThemePreset>(tempThemePreset_)));
    items_[itemCount_++] = SettingItem(themeLabelBuf_, &tempThemePreset_, 0, 5, 1, "");
    
    // WiFi Connection section
    items_[itemCount_++] = SettingItem::header("-- WiFi Connection --");
    items_[itemCount_++] = SettingItem::action("Saved Networks");
    
    // System section
    items_[itemCount_++] = SettingItem::header("-- System --");
    
    // Device Name (editable via text input popup)
    auto& sysSettings = SettingsManager::getInstance().getMutable();
    snprintf(deviceNameLabel_, sizeof(deviceNameLabel_), "Device: %s", sysSettings.system.deviceName);
    items_[itemCount_++] = SettingItem::action(deviceNameLabel_);
    
    items_[itemCount_++] = SettingItem::toggle("Sound Alerts", &tempNotifySounds_);
    items_[itemCount_++] = SettingItem::toggle("LED Alerts", &tempNotifyLeds_);
    items_[itemCount_++] = SettingItem::toggle("Heap Badge", &tempShowHeapBadge_);
    items_[itemCount_++] = SettingItem::toggle("Serial Debug", &tempSerialDebug_);
    
    // Dashboard Server section
    items_[itemCount_++] = SettingItem::header("-- Dashboard Server --");
    items_[itemCount_++] = SettingItem::toggle("Enable Auth", &tempDashboardAuthEnabled_);
    
    snprintf(dashboardUserLabel_, sizeof(dashboardUserLabel_), "Username: %s", 
             sysSettings.system.dashboardUsername);
    items_[itemCount_++] = SettingItem::action(dashboardUserLabel_);
    
    if (strlen(sysSettings.system.dashboardPassword) > 0) {
        snprintf(dashboardPassLabel_, sizeof(dashboardPassLabel_), "Password: ******");
    } else {
        snprintf(dashboardPassLabel_, sizeof(dashboardPassLabel_), "Password: (not set)");
    }
    items_[itemCount_++] = SettingItem::action(dashboardPassLabel_);
    
    // API Keys section
    items_[itemCount_++] = SettingItem::header("-- API Keys --");
    
    auto& sm = SettingsManager::getInstance();
    if (sm.hasWpaSecKey()) {
        snprintf(wpaSecKeyLabel_, sizeof(wpaSecKeyLabel_), "WPA-SEC: %.8s...", sm.getWpaSecKey());
    } else {
        snprintf(wpaSecKeyLabel_, sizeof(wpaSecKeyLabel_), "WPA-SEC: Not Set");
    }
    items_[itemCount_++] = SettingItem::action(wpaSecKeyLabel_);
    
    if (sm.hasWigleKey()) {
        snprintf(wigleKeyLabel_, sizeof(wigleKeyLabel_), "WiGLE: %.8s...", sm.getWigleKey());
    } else {
        snprintf(wigleKeyLabel_, sizeof(wigleKeyLabel_), "WiGLE: Not Set");
    }
    items_[itemCount_++] = SettingItem::action(wigleKeyLabel_);

    if (sm.hasPwncrackKey()) {
        snprintf(pwncrackKeyLabel_, sizeof(pwncrackKeyLabel_), "pwncrack: %.8s...", sm.getPwncrackKey());
    } else {
        snprintf(pwncrackKeyLabel_, sizeof(pwncrackKeyLabel_), "pwncrack: Not Set");
    }
    items_[itemCount_++] = SettingItem::action(pwncrackKeyLabel_);
    
    // Time Sync section
    items_[itemCount_++] = SettingItem::header("-- Time Management --");
    snprintf(timeSyncLabelBuf_, sizeof(timeSyncLabelBuf_), "Source: %s", 
             TimeManager::getInstance().getSourceName());
    items_[itemCount_++] = SettingItem::info(timeSyncLabelBuf_);
    items_[itemCount_++] = SettingItem::action("Sync Time (NTP)");

    // Storage section
    items_[itemCount_++] = SettingItem::header("-- Storage --");
    items_[itemCount_++] = SettingItem::action("Rebuild Index");
    items_[itemCount_++] = SettingItem::action("Retry SD Mount");

    // Count selectable items (non-headers)
    selectableCount_ = 0;
    for (int i = 0; i < itemCount_; i++) {
        if (items_[i].type != SettingType::HEADER && items_[i].type != SettingType::SEPARATOR) {
            selectableCount_++;
        }
    }
}

void SettingsScreen::init() {
    buildSettingsList();
}

void SettingsScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    screenState_ = SettingsScreenState::BROWSING;
    selection_ = 1;  // Skip first header
    scrollOffset_ = 0;
    init();
}

void SettingsScreen::hide() {
    if (visible_) {
        // Save settings on exit
        auto& settings = SettingsManager::getInstance().getMutable();
        settings.wireless.karmaChannel = tempKarmaChannel_;
        settings.wireless.karmaAutoRotate = (tempKarmaAutoRotate_ != 0);
        settings.wireless.karmaRotationSpeed = tempKarmaRotationSpeed_;
        settings.wireless.karmaCooldownMin = tempKarmaCooldown_;
        settings.wireless.deauthBurstCount = tempDeauthBurst_;
        settings.wireless.deauthIntervalMs = tempDeauthInterval_;
        settings.wireless.beaconIntervalMs = tempBeaconInterval_;
        settings.wireless.wardrivingScanIntervalMs = tempWardrivingScanInterval_;
        settings.display.brightness = tempBrightness_;
        settings.display.toastPosition = tempToastPosition_;
        settings.display.themePreset = tempThemePreset_;
        settings.wireless.capOverride = static_cast<hal::CapOverride>(
            static_cast<uint8_t>(tempCapOverride_));
        settings.system.serialDebug = (tempSerialDebug_ != 0);
        settings.system.notifySounds = (tempNotifySounds_ != 0);
        settings.system.notifyLeds = (tempNotifyLeds_ != 0);
        settings.system.showHeapBadge = (tempShowHeapBadge_ != 0);
        settings.system.dashboardAuthEnabled = (tempDashboardAuthEnabled_ != 0);
        
        // Apply notification settings immediately
        NotificationManager::getInstance().setAudioEnabled(settings.system.notifySounds);
        NotificationManager::getInstance().setLedEnabled(settings.system.notifyLeds);
        
        // Apply toast position setting
        ToastManager::getInstance().setPosition(static_cast<ToastPosition>(tempToastPosition_));
        
        // Apply theme (already applied via live preview)
        ThemeManager::getInstance().setTheme(static_cast<ThemePreset>(tempThemePreset_));
        
        SettingsManager::getInstance().save();
        applyBrightness();
    }
    visible_ = false;
}

void SettingsScreen::applyBrightness() {
    SystemManager::getInstance().setDisplayBrightness(tempBrightness_);
}

void SettingsScreen::update() {
    if (!visible_) return;

    // Deferred manifest rebuild: armed by the Rebuild Index confirm dialog. Wait
    // one full render (so the "Rebuilding index..." toast is on screen) before
    // running the blocking ~20s dir scan, then report the result.
    if (rebuildPendingFrames_ > 0) {
        if (--rebuildPendingFrames_ == 0) {
            int total = CaptureRegistry::getInstance().rebuildIndex();
            char msg[32];
            snprintf(msg, sizeof(msg), "Index rebuilt: %d", total);
            ToastManager::getInstance().show(msg, ToastType::SUCCESS,
                                             ToastPriority::PRIORITY_HIGH, 2500);
            buildSettingsList();
            needsRedraw_ = true;
        }
    }
}

bool SettingsScreen::handleInput(char key) {
    needsRedraw_ = true;
    
    // Route input to popup if visible
    if (deviceNamePopup_.isVisible()) {
        return deviceNamePopup_.handleInput(key);
    }
    if (macPopup_.isVisible()) {
        return macPopup_.handleInput(key);
    }
    if (bleNamePopup_.isVisible()) {
        return bleNamePopup_.handleInput(key);
    }
    if (dashboardUserPopup_.isVisible()) {
        return dashboardUserPopup_.handleInput(key);
    }
    if (dashboardPassPopup_.isVisible()) {
        return dashboardPassPopup_.handleInput(key);
    }
    if (wpaSecKeyPopup_.isVisible()) {
        return wpaSecKeyPopup_.handleInput(key);
    }
    if (wigleKeyPopup_.isVisible()) {
        return wigleKeyPopup_.handleInput(key);
    }
    if (pwncrackKeyPopup_.isVisible()) {
        return pwncrackKeyPopup_.handleInput(key);
    }

    // Route input to ActionMenu if visible
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
    
    if (screenState_ == SettingsScreenState::BROWSING) {
        // Next selectable row in `dir`, wrapping around and skipping headers and
        // separators (so the cursor never lands on a non-actionable row).
        auto nextSelectable = [this](int from, int dir) -> int {
            if (itemCount_ <= 0) return 0;
            int idx = from;
            for (int n = 0; n < itemCount_; ++n) {
                idx += dir;
                if (idx < 0) idx = itemCount_ - 1;
                else if (idx >= itemCount_) idx = 0;
                SettingType t = items_[idx].type;
                if (t != SettingType::HEADER && t != SettingType::SEPARATOR) return idx;
            }
            return from;
        };
        switch (key) {
            case ';':  // Up (wraps to last)
                selection_ = nextSelectable(selection_, -1);
                if (selection_ < scrollOffset_) scrollOffset_ = selection_;
                else if (selection_ >= scrollOffset_ + VISIBLE_ITEMS)
                    scrollOffset_ = selection_ - VISIBLE_ITEMS + 1;
                return true;

            case '.':  // Down (wraps to first)
                selection_ = nextSelectable(selection_, 1);
                if (selection_ >= scrollOffset_ + VISIBLE_ITEMS)
                    scrollOffset_ = selection_ - VISIBLE_ITEMS + 1;
                else if (selection_ < scrollOffset_) scrollOffset_ = selection_;
                return true;
                
            case '\n':
            case '\r': {
                int idx = selection_;
                if (items_[idx].type == SettingType::TOGGLE) {
                    if (items_[idx].valuePtr) {
                        *items_[idx].valuePtr = !(*items_[idx].valuePtr);
                    }
                } else if (items_[idx].type == SettingType::NUMBER) {
                    screenState_ = SettingsScreenState::EDITING;
                } else if (items_[idx].type == SettingType::ACTION) {
                    const char* label = items_[idx].label;
                    
                    if (strcmp(label, "Saved Networks") == 0) {
                        if (onSavedNetworksRequested_) {
                            onSavedNetworksRequested_();
                        }
                    } else if (strncmp(label, "Device:", 7) == 0) {
                        auto& settings = SettingsManager::getInstance().getMutable();
                        deviceNamePopup_.show("Device Name", settings.system.deviceName, false, 31);
                        deviceNamePopup_.setOnSubmit([this](const char* text) {
                            auto& settings = SettingsManager::getInstance().getMutable();
                            strncpy(settings.system.deviceName, text, 31);
                            settings.system.deviceName[31] = '\0';
                            buildSettingsList();
                            needsRedraw_ = true;
                        });
                        deviceNamePopup_.setOnCancel([this]() {
                            needsRedraw_ = true;
                        });
                    } else if (strcmp(label, "Sync Time (NTP)") == 0) {
                        if (WiFiConnection::getInstance().isConnected()) {
                            ToastManager::getInstance().show("Syncing with NTP...", ToastType::INFO, ToastPriority::PRIORITY_HIGH);
                            if (TimeManager::getInstance().syncFromNTP()) {
                                ToastManager::getInstance().show("Time synced!", ToastType::SUCCESS, ToastPriority::PRIORITY_HIGH);
                            } else {
                                ToastManager::getInstance().show("NTP sync failed", ToastType::ERROR, ToastPriority::PRIORITY_HIGH);
                            }
                        } else {
                            ToastManager::getInstance().show("WiFi required", ToastType::ERROR, ToastPriority::PRIORITY_HIGH);
                        }
                        buildSettingsList(); // Update source info label
                        needsRedraw_ = true;
                    } else if (strcmp(label, "Rebuild Index") == 0) {
                        // Re-sync the manifest with the .pcap files on the SD card
                        // (for captures added/removed off-device). Confirm first —
                        // it re-scans the whole dir and can take ~20s.
                        actionMenu_.setTitle("Rebuild capture index?");
                        actionMenu_.setSubtitle("Re-scans SD (~20s, screen busy)");
                        actionMenu_.clearItems();
                        actionMenu_.addItem('1', "Rebuild now");
                        actionMenu_.addItem('2', "Cancel");
                        actionMenu_.setOnAction([this](char key) {
                            actionMenu_.hide();
                            needsRedraw_ = true;
                            if (key != '1') return;
                            // Show the toast now; defer the blocking ~20s rebuild
                            // a couple of frames (run in update()) so the render
                            // loop paints "Rebuilding index..." before the freeze.
                            ToastManager::getInstance().show("Rebuilding index...", ToastType::INFO,
                                                             ToastPriority::PRIORITY_HIGH, 30000);
                            rebuildPendingFrames_ = 2;
                        });
                        actionMenu_.show();
                    } else if (strcmp(label, "Retry SD Mount") == 0) {
                        // Explicit operator retry: clear any cached no-card verdict
                        // and attempt a real mount, so a card inserted after a
                        // cardless boot works without a reboot (slice-0020). Bounded
                        // (~5.6s worst case) and operator-initiated, so run inline.
                        bool mounted = SDManager::getInstance().remount(true);
                        if (mounted) {
                            // A cardless boot skipped SettingsManager::load(), so
                            // in-memory settings are compiled defaults. Reconcile the
                            // whole settings struct with the now-mounted card BEFORE
                            // this screen's exit autosave writes those defaults over
                            // the card's real values (which clobbered a saved theme).
                            // Reload every setting, re-apply the runtime-applied ones,
                            // and reseed this screen's edit buffer so hide() persists
                            // the card's values, not stale defaults.
                            SettingsManager& sm = SettingsManager::getInstance();
                            sm.load();
                            sm.loadWhitelist();
                            sm.checkApiKeyFile();
                            const auto& s = sm.get();
                            ThemeManager::getInstance().setTheme(
                                static_cast<ThemePreset>(s.display.themePreset));
                            SystemManager::getInstance().setDisplayBrightness(s.display.brightness);
                            NotificationManager::getInstance().setAudioEnabled(s.system.notifySounds);
                            NotificationManager::getInstance().setLedEnabled(s.system.notifyLeds);
                            ToastManager::getInstance().setPosition(
                                static_cast<ToastPosition>(s.display.toastPosition));
                            buildSettingsList();  // reseed temp*_ from the reloaded settings
                        }
                        ToastManager::getInstance().show(
                            mounted ? "SD mounted, settings reloaded" : "No SD card found",
                            mounted ? ToastType::SUCCESS : ToastType::WARNING,
                            ToastPriority::PRIORITY_HIGH);
                        needsRedraw_ = true;
                    } else if (strncmp(label, "Fixed MAC:", 10) == 0) {
                         actionMenu_.setTitle("Configure BLE MAC");
                         actionMenu_.clearItems();
                         actionMenu_.addItem('1', "Manual Input");
                         actionMenu_.addItem('2', "Randomize");
                         actionMenu_.addItem('3', "Factory Default");
                         
                         actionMenu_.setOnAction([this](char key) {
                             auto& settings = SettingsManager::getInstance().getMutable();
                             
                             if (key == '1') { // Manual
                                 actionMenu_.hide();
                                 macPopup_.show("Enter MAC Address", settings.wireless.fixedBleMac[0] ? settings.wireless.fixedBleMac : "00:11:22:33:44:55", false, 17);
                                 macPopup_.setOnSubmit([this](const char* text) {
                                     if (strlen(text) == 17) {
                                         auto& settings = SettingsManager::getInstance().getMutable();
                                         strncpy(settings.wireless.fixedBleMac, text, 17);
                                         settings.wireless.fixedBleMac[17] = '\0';
                                         ToastManager::getInstance().show("MAC Updated", ToastType::SUCCESS);
                                     } else {
                                         ToastManager::getInstance().show("Invalid MAC Format", ToastType::ERROR);
                                     }
                                     buildSettingsList();
                                     needsRedraw_ = true;
                                 });
                                 macPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                             } 
                             else if (key == '2') { // Randomize
                                 uint8_t mac[6];
                                 for(int i=0; i<6; i++) mac[i] = rand() % 256;
                                 mac[0] = (mac[0] & 0xFE) | 0x02; // Unicast | Local
                                 char macStr[18];
                                 snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
                                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                                 strncpy(settings.wireless.fixedBleMac, macStr, 17);
                                 settings.wireless.fixedBleMac[17] = '\0';
                                 ToastManager::getInstance().show("Random MAC Set", ToastType::SUCCESS);
                                 actionMenu_.hide();
                                 buildSettingsList();
                                 needsRedraw_ = true;
                             }
                             else if (key == '3') { // Factory
                                 settings.wireless.fixedBleMac[0] = '\0';
                                 ToastManager::getInstance().show("Reverted to Factory", ToastType::SUCCESS);
                                 actionMenu_.hide();
                                 buildSettingsList();
                                 needsRedraw_ = true;
                             }
                         });
                         
                         actionMenu_.setOnDismiss([this]() {
                             needsRedraw_ = true;
                         });
                         
                         actionMenu_.show();
                         needsRedraw_ = true;
                    } else if (strncmp(label, "Name:", 5) == 0) {
                         auto& settings = SettingsManager::getInstance().getMutable();
                         bleNamePopup_.show("BLE Device Name", settings.wireless.bleName, false, 31);
                         bleNamePopup_.setOnSubmit([this](const char* text) {
                             auto& settings = SettingsManager::getInstance().getMutable();
                             strncpy(settings.wireless.bleName, text, 32);
                             settings.wireless.bleName[32] = '\0';
                             buildSettingsList();
                             needsRedraw_ = true;
                             ToastManager::getInstance().show("Name Updated", ToastType::SUCCESS);
                         });
                         bleNamePopup_.setOnCancel([this]() {
                             needsRedraw_ = true;
                         });
                    } else if (strncmp(label, "Username:", 9) == 0) {
                         auto& settings = SettingsManager::getInstance().getMutable();
                         dashboardUserPopup_.show("Dashboard Username", settings.system.dashboardUsername, false, 31);
                         dashboardUserPopup_.setOnSubmit([this](const char* text) {
                             auto& settings = SettingsManager::getInstance().getMutable();
                             strncpy(settings.system.dashboardUsername, text, 32);
                             settings.system.dashboardUsername[32] = '\0';
                             buildSettingsList();
                             needsRedraw_ = true;
                             ToastManager::getInstance().show("Username Updated", ToastType::SUCCESS);
                         });
                         dashboardUserPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                    } else if (strncmp(label, "Password:", 9) == 0) {
                         dashboardPassPopup_.show("Dashboard Password", "", false, 31);
                         dashboardPassPopup_.setOnSubmit([this](const char* text) {
                             auto& settings = SettingsManager::getInstance().getMutable();
                             strncpy(settings.system.dashboardPassword, text, 32);
                             settings.system.dashboardPassword[32] = '\0';
                             buildSettingsList();
                             needsRedraw_ = true;
                             ToastManager::getInstance().show("Password Updated", ToastType::SUCCESS);
                         });
                         dashboardPassPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                    } else if (strncmp(label, "WPA-SEC:", 8) == 0) {
                         auto& sm = SettingsManager::getInstance();
                         wpaSecKeyPopup_.show("WPA-SEC Key", sm.getWpaSecKey(), false, 32);
                         wpaSecKeyPopup_.setOnSubmit([this](const char* text) {
                             if (strlen(text) == 0) {
                                 SettingsManager::getInstance().setWpaSecKey(nullptr);
                                 ToastManager::getInstance().show("Key Cleared", ToastType::INFO);
                             } else {
                                 SettingsManager::getInstance().setWpaSecKey(text);
                                 ToastManager::getInstance().show("Key Updated", ToastType::SUCCESS);
                             }
                             buildSettingsList();
                             needsRedraw_ = true;
                         });
                         wpaSecKeyPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                    } else if (strncmp(label, "WiGLE:", 6) == 0) {
                         auto& sm = SettingsManager::getInstance();
                         wigleKeyPopup_.show("WiGLE API Key", sm.getWigleKey(), false, 128);
                         wigleKeyPopup_.setOnSubmit([this](const char* text) {
                             if (strlen(text) == 0) {
                                 SettingsManager::getInstance().setWigleKey(nullptr);
                                 ToastManager::getInstance().show("Key Cleared", ToastType::INFO);
                             } else {
                                 SettingsManager::getInstance().setWigleKey(text);
                                 ToastManager::getInstance().show("Key Updated", ToastType::SUCCESS);
                             }
                             buildSettingsList();
                             needsRedraw_ = true;
                         });
                         wigleKeyPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                    } else if (strncmp(label, "pwncrack:", 9) == 0) {
                         auto& sm = SettingsManager::getInstance();
                         pwncrackKeyPopup_.show("pwncrack Key", sm.getPwncrackKey(), false, 96);
                         pwncrackKeyPopup_.setOnSubmit([this](const char* text) {
                             if (strlen(text) == 0) {
                                 SettingsManager::getInstance().setPwncrackKey(nullptr);
                                 ToastManager::getInstance().show("Key Cleared", ToastType::INFO);
                             } else {
                                 SettingsManager::getInstance().setPwncrackKey(text);
                                 ToastManager::getInstance().show("Key Updated", ToastType::SUCCESS);
                             }
                             buildSettingsList();
                             needsRedraw_ = true;
                         });
                         pwncrackKeyPopup_.setOnCancel([this]() { needsRedraw_ = true; });
                    }
                }
                return true;
            }
                
            case '`':
                shouldExit_ = true;
                return true;
        }
    } else if (screenState_ == SettingsScreenState::EDITING) {
        int idx = selection_;
        auto& item = items_[idx];
        switch (key) {
            case ';':  // Decrease
                if (item.valuePtr && *item.valuePtr > item.minVal) {
                    *item.valuePtr -= item.step;
                    if (*item.valuePtr < item.minVal) *item.valuePtr = item.minVal;
                    if (item.valuePtr == &tempThemePreset_) {
                        ThemeManager::getInstance().setTheme(static_cast<ThemePreset>(tempThemePreset_));
                        snprintf(themeLabelBuf_, sizeof(themeLabelBuf_), "Theme: %s", getThemeName(static_cast<ThemePreset>(tempThemePreset_)));
                    }
                    if (item.valuePtr == &tempCapOverride_) {
                        snprintf(capOverrideLabelBuf_, sizeof(capOverrideLabelBuf_), "Cap: %s", capOverrideName(tempCapOverride_));
                    }
                    if (item.valuePtr == &tempBrightness_) {
                        applyBrightness();
                    }
                }
                return true;
                
            case '.':  // Increase
                if (item.valuePtr && *item.valuePtr < item.maxVal) {
                    *item.valuePtr += item.step;
                    if (*item.valuePtr > item.maxVal) *item.valuePtr = item.maxVal;
                    if (item.valuePtr == &tempThemePreset_) {
                        ThemeManager::getInstance().setTheme(static_cast<ThemePreset>(tempThemePreset_));
                        snprintf(themeLabelBuf_, sizeof(themeLabelBuf_), "Theme: %s", getThemeName(static_cast<ThemePreset>(tempThemePreset_)));
                    }
                    if (item.valuePtr == &tempCapOverride_) {
                        snprintf(capOverrideLabelBuf_, sizeof(capOverrideLabelBuf_), "Cap: %s", capOverrideName(tempCapOverride_));
                    }
                    if (item.valuePtr == &tempBrightness_) {
                        applyBrightness();
                    }
                }
                return true;
                
            case '\n':
            case '\r':
            case '`':
                screenState_ = SettingsScreenState::BROWSING;
                return true;
        }
    }
    
    return false;
}

void SettingsScreen::render(Canvas& canvas) {
    if (!visible_) return;
    
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    drawHeader(canvas, "SETTINGS");
    drawItems(canvas);
    drawFooter(canvas);
    
    // Render popup on top if visible
    deviceNamePopup_.render(canvas);
    macPopup_.render(canvas);
    bleNamePopup_.render(canvas);
    dashboardUserPopup_.render(canvas);
    dashboardPassPopup_.render(canvas);
    wpaSecKeyPopup_.render(canvas);
    wigleKeyPopup_.render(canvas);
    pwncrackKeyPopup_.render(canvas);
    actionMenu_.render(canvas);
#else
    (void)canvas;
#endif
}

void SettingsScreen::drawHeader(Canvas& canvas, const char* title) {
    ui::StatusBar::render(canvas, title, nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY());
}

void SettingsScreen::drawFooter(Canvas& canvas) {
    footerHints_.setHints({});
    
    static char countBuf[16];
    int sel = selection_;
    int selectablePos = 0;
    for (int i = 0; i < sel && i < itemCount_; i++) {
        if (items_[i].type != SettingType::HEADER && items_[i].type != SettingType::SEPARATOR) {
            selectablePos++;
        }
    }
    if (items_[sel].type != SettingType::HEADER && items_[sel].type != SettingType::SEPARATOR) {
        selectablePos++;
    }
    snprintf(countBuf, sizeof(countBuf), "%d/%d", selectablePos, selectableCount_);
    footerHints_.setRightContent(countBuf);
    
    footerHints_.render(canvas);
}

void SettingsScreen::drawItems(Canvas& canvas) {
    int16_t y = HEADER_HEIGHT + 2;
    int16_t screenWidth = canvas.width();
    const int16_t lineHeight = ROW_HEIGHT;
    
    canvas.setTextSize(1);
    
    int scrollOff = scrollOffset_;
    int selectedIdx = selection_;
    int endIdx = scrollOff + VISIBLE_ITEMS;
    if (endIdx > itemCount_) endIdx = itemCount_;
    
    for (int i = scrollOff; i < endIdx; i++) {
        const auto& item = items_[i];
        bool selected = (i == selectedIdx);
        bool editing = selected && (screenState_ == SettingsScreenState::EDITING);
        
        if (selected && item.type != SettingType::HEADER) {
            canvas.fillRect(0, y - 1, screenWidth, lineHeight, 
                           editing ? theme::WARNING() : theme::ACCENT());
        }
        
        switch (item.type) {
            case SettingType::HEADER:
                canvas.setTextColor(theme::ACCENT());
                canvas.setCursor(4, y + 3);
                canvas.print(item.label);
                break;
                
            case SettingType::NUMBER: {
                canvas.setTextColor(theme::TEXT_PRIMARY());
                canvas.setCursor(8, y + 3);
                canvas.print(item.label);
                
                char valStr[20];
                if (item.valuePtr == &tempToastPosition_) {
                    const char* posLabels[] = {"Top", "Center", "Bottom"};
                    int idx = *item.valuePtr;
                    if (idx >= 0 && idx <= 2) {
                        strncpy(valStr, posLabels[idx], sizeof(valStr) - 1);
                    } else {
                        snprintf(valStr, sizeof(valStr), "%d", idx);
                    }
                } else if (item.valuePtr == &tempKarmaRotationSpeed_) {
                    const char* speedLabels[] = {"Fast", "Normal", "Slow"};
                    int idx = *item.valuePtr;
                    if (idx >= 0 && idx <= 2) {
                        strncpy(valStr, speedLabels[idx], sizeof(valStr) - 1);
                    } else {
                        snprintf(valStr, sizeof(valStr), "%d", idx);
                    }
                } else {
                    snprintf(valStr, sizeof(valStr), "%d%s", (int)*item.valuePtr, item.suffix);
                }
                int valWidth = strlen(valStr) * 6;
                canvas.setCursor(screenWidth - valWidth - 8, y + 3);
                canvas.setTextColor(editing ? theme::BG_PRIMARY() : theme::TEXT_SECONDARY());
                canvas.print(valStr);
                break;
            }
                
            case SettingType::TOGGLE: {
                canvas.setTextColor(theme::TEXT_PRIMARY());
                canvas.setCursor(8, y + 3);
                canvas.print(item.label);
                
                bool isOn = item.valuePtr && (*item.valuePtr != 0);
                canvas.setCursor(screenWidth - 24, y + 3);
                canvas.setTextColor(isOn ? theme::SUCCESS() : theme::TEXT_DISABLED());
                canvas.print(isOn ? "ON" : "OFF");
                break;
            }
                
            case SettingType::INFO:
            case SettingType::ACTION:
                canvas.setTextColor(editing ? theme::BG_PRIMARY() : theme::TEXT_PRIMARY());
                canvas.setCursor(8, y + 3);
                canvas.print(item.label);
                break;
                
            case SettingType::SEPARATOR:
                break;
        }
        
        y += lineHeight;
    }
    
    // Draw vertical scrollbar when items exceed visible count
    if (itemCount_ > VISIBLE_ITEMS) {
        int16_t barX = screenWidth - 5;
        int16_t barY = HEADER_HEIGHT + 2;
        int16_t barHeight = VISIBLE_ITEMS * lineHeight - 4;
        
        canvas.fillRect(barX, barY, 4, barHeight, theme::BG_TERTIARY());
        
        int16_t thumbHeight = (VISIBLE_ITEMS * barHeight) / itemCount_;
        if (thumbHeight < 6) thumbHeight = 6;
        
        int maxScroll = itemCount_ - VISIBLE_ITEMS;
        int16_t thumbY = barY + (scrollOff * (barHeight - thumbHeight)) / maxScroll;
        canvas.fillRect(barX, thumbY, 4, thumbHeight, theme::ACCENT());
    }
}

} // namespace adversary
