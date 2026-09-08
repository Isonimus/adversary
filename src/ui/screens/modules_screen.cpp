/**
 * @file modules_screen.cpp
 * @brief ModulesScreen implementation (slice-0018).
 */

#include "modules_screen.h"

#ifdef ESP32
#include <Arduino.h>
#endif

#include "../components/toast_manager.h"
#include "../../hal/expansion/expansion_cap.h"
#include "../../modules/gps/gps_manager.h"
#include "../../modules/storage/settings_manager.h"
#include "../../core/module_detection.h"

namespace adversary {

namespace {
const char* overrideName(hal::CapOverride ov) {
    switch (ov) {
        case hal::CapOverride::Auto:            return "Auto";
        case hal::CapOverride::ForceNone:       return "Force None";
        case hal::CapOverride::ForceMultiRadio: return "Force Cap";
    }
    return "?";
}
}  // namespace

ModulesScreen::ModulesScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , phase_(Phase::IDLE)
    , scanFramePainted_(false) {
}

ModulesScreen::~ModulesScreen() {
    deinit();
}

void ModulesScreen::init() {}

void ModulesScreen::deinit() {
    hide();
}

void ModulesScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    phase_ = Phase::IDLE;
    scanFramePainted_ = false;
    footerHints_.setHints({
        {'r', "Re-scan"},
        {'o', "Override"},
        {'`', "Exit"}
    });
}

void ModulesScreen::hide() {
    visible_ = false;
}

void ModulesScreen::update() {
    // The probe blocks (GPS UART), so run it only after a frame has painted since
    // the request — that guarantees the "Scanning..." toast reached the LCD first,
    // rather than freezing behind an unpainted toast.
    if (phase_ == Phase::SCAN_PENDING && scanFramePainted_) {
        redetectModules();
        phase_ = Phase::IDLE;
        scanFramePainted_ = false;
        needsRedraw_ = true;
    }
}

bool ModulesScreen::handleInput(char key) {
    // Map a footer-hint keypress to its action char, then handle uniformly.
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        key = action;
    }

    switch (key) {
        case 'r':
        case 'R':
            if (phase_ == Phase::IDLE) requestRescan("Scanning modules...");
            return true;
        case 'o':
        case 'O':
            if (phase_ == Phase::IDLE) cycleOverride();
            return true;
        case '`':
        case '\n':
        case '\r':
            shouldExit_ = true;
            return true;
    }
    return false;
}

void ModulesScreen::requestRescan(const char* toastMsg) {
    showToast(toastMsg);  // painted next frame by the ScreenManager toast overlay
    phase_ = Phase::SCAN_PENDING;
    scanFramePainted_ = false;
    needsRedraw_ = true;
}

void ModulesScreen::cycleOverride() {
    auto& settings = SettingsManager::getInstance().getMutable();
    using hal::CapOverride;
    switch (settings.wireless.capOverride) {
        case CapOverride::Auto:
            settings.wireless.capOverride = CapOverride::ForceNone; break;
        case CapOverride::ForceNone:
            settings.wireless.capOverride = CapOverride::ForceMultiRadio; break;
        case CapOverride::ForceMultiRadio:
            settings.wireless.capOverride = CapOverride::Auto; break;
    }
    SettingsManager::getInstance().save();
    // A changed override changes cap resolution — apply it by re-scanning, and let
    // the toast report the new setting (which is the operator's feedback here).
    char msg[32];
    snprintf(msg, sizeof msg, "Override: %s",
             overrideName(settings.wireless.capOverride));
    requestRescan(msg);
}

void ModulesScreen::render(Canvas& canvas) {
    if (!visible_) return;
#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    canvas.fillScreen(theme::BG_PRIMARY());
    drawHeader(canvas);
    drawContent(canvas);
    footerHints_.render(canvas);

    // This frame will reach the LCD (loop() pushSprite after render), and the
    // toast overlay paints on top of it — so the pending probe may run next frame.
    if (phase_ == Phase::SCAN_PENDING) scanFramePainted_ = true;
#else
    (void)canvas;
#endif
}

void ModulesScreen::drawHeader(Canvas& canvas) {
    ui::StatusBar::render(canvas, "MODULES", nullptr, theme::BG_SECONDARY(),
                          theme::TEXT_PRIMARY());
}

void ModulesScreen::drawRow(Canvas& canvas, int16_t y, const char* label,
                            bool present, const char* detail) {
#ifdef ESP32
    // Single-line list row at body text size (UI-STYLE): label left, dim detail in
    // a fixed middle column, present/absent status right-aligned.
    canvas.setTextSize(1);

    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(6, y);
    canvas.print(label);

    if (detail && detail[0]) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(92, y);
        canvas.print(detail);
    }

    canvas.setTextColor(present ? theme::SUCCESS() : theme::ERROR());
    canvas.setTextDatum(top_right);
    canvas.drawString(present ? "PRESENT" : "ABSENT", canvas.width() - 6, y);
    canvas.setTextDatum(top_left);
#else
    (void)canvas; (void)y; (void)label; (void)present; (void)detail;
#endif
}

void ModulesScreen::drawContent(Canvas& canvas) {
#ifdef ESP32
    int16_t y = BODY_TOP;

    const bool capPresent =
        hal::resolvedExpansionCap() == hal::ExpansionCap::MultiRadio;
    char capDetail[24];
    snprintf(capDetail, sizeof capDetail, "CC1101 v0x%02X",
             hal::lastCapProbe().cc1101Version);
    drawRow(canvas, y, "Sub-GHz Cap", capPresent,
            capPresent ? capDetail : "SPI header");
    y += theme::LIST_ITEM_HEIGHT;

    drawRow(canvas, y, "RFID", ui::g_rfidDetected, "Grove I2C 0x28");
    y += theme::LIST_ITEM_HEIGHT;

    const char* src = GPSManager::getInstance().getDetectedPinSet();
    drawRow(canvas, y, "GPS", ui::g_gpsDetected,
            (ui::g_gpsDetected && src) ? src : "UART");
    y += theme::LIST_ITEM_HEIGHT;

    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(6, y + 2);
    canvas.printf("Cap override: %s",
                  overrideName(SettingsManager::getInstance().get().wireless.capOverride));
#else
    (void)canvas;
#endif
}

} // namespace adversary
