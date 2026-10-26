/**
 * @file ble_tracker_screen.cpp
 * @brief BleTrackerScreen implementation (ADR-0128, slice-0129).
 */

#include "ble_tracker_screen.h"

#include <cstdio>
#include <cstring>

#include "config/config.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "modules/ble/ble_utils.h"
#include "modules/system/system_manager.h"
#include "utils/log.h"

namespace adversary {

namespace {

// Re-poll the scanner's device list at this cadence. Advertisements arrive faster than this,
// but a tighter poll just copies the list more often for no visible gain; the EWMA and age
// counter are driven off the sample timestamps, not the poll rate.
constexpr uint32_t POLL_INTERVAL_MS = 200;

const char* stateLabel(ProximityState state) {
    switch (state) {
        case ProximityState::Acquiring: return "ACQUIRING";
        case ProximityState::Tracking:  return "TRACKING";
        case ProximityState::Lost:      return "SIGNAL LOST";
        case ProximityState::Dropped:   return "LOST";
    }
    return "";
}

const char* bandLabel(ProximityBand band) {
    switch (band) {
        case ProximityBand::Close: return "CLOSE";
        case ProximityBand::Mid:   return "MID";
        case ProximityBand::Far:   return "FAR";
    }
    return "";
}

const char* trendLabel(ProximityTrend trend) {
    switch (trend) {
        case ProximityTrend::Approaching: return "GETTING CLOSER";
        case ProximityTrend::Receding:    return "GETTING FARTHER";
        case ProximityTrend::Steady:      return "HOLDING STEADY";
    }
    return "";
}

} // namespace

BleTrackerScreen::BleTrackerScreen()
    : m_scanner(BLEScanner::getInstance())
    , m_tracker(kDefaultProximityConfig)
    , m_targetAddr{}
    , m_haveTarget(false)
    , m_lastFedSeen(0)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_isInitializing(false)
    , m_pendingInit(0)
    , m_lastPoll(0) {
}

void BleTrackerScreen::setParams(const ScreenParams& params) {
    memcpy(m_targetAddr, params.bssid, 6);
    // Canonical MAC string: getNative() order is little-endian, so print high byte first
    // (matches ble::addressToString).
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             m_targetAddr[5], m_targetAddr[4], m_targetAddr[3],
             m_targetAddr[2], m_targetAddr[1], m_targetAddr[0]);
    m_targetMac = mac;
    m_targetName = (params.ssid[0] != '\0') ? params.ssid : m_targetMac;
    m_haveTarget = true;
    m_lastFedSeen = 0;
    m_tracker.reset();
}

void BleTrackerScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_lastPoll = 0;

    footerHints_.setHints({
        {'`', "Back", true}
    });
    footerHints_.setFocus(false);

    // Only free memory for the BLE session if it is not already up: navigating here from the
    // BLE scanner leaves NimBLE initialized, and a destructive deinit/init cycle is wasteful
    // (same optimization as BleScannerScreen::show).
    if (!BLEScanner::isInitialized()) {
        SystemManager::getInstance().prepareForMemoryIntensiveTask(false);
    }

    // Defer radio init to update() to flatten the call stack (WindowOverflow avoidance),
    // exactly as the scanner does.
    m_isInitializing = true;
    m_pendingInit = 2;
}

void BleTrackerScreen::hide() {
    m_active = false;
    m_scanner.stopScan();
    m_scanner.clearDevices();
    SystemManager::getInstance().restoreFromMemoryIntensiveTask();
}

void BleTrackerScreen::update() {
    if (!m_active) return;

    if (m_isInitializing && m_pendingInit > 0) {
        if (--m_pendingInit == 0) {
            m_scanner.init();
            m_isInitializing = false;
            // Passive + continuous: required for a long single-target lock without the
            // unbounded heap growth active+continuous scans cause (ADR-0103).
            m_scanner.startScan(0, false);
        }
        m_needsRedraw = true;
        return;
    }

    if (!m_haveTarget) return;

    if (millis() - m_lastPoll >= POLL_INTERVAL_MS) {
        m_lastPoll = millis();

        // Feed the tracker every fresh advertisement from the locked address (onResult
        // refreshes rssi + lastSeen per advert; a repeated lastSeen means no new advert).
        std::vector<BLEDeviceInfo> devices = m_scanner.getDevices();
        for (auto& dev : devices) {
            if (memcmp(dev.address.getNative(), m_targetAddr, 6) == 0) {
                if (dev.lastSeen != m_lastFedSeen) {
                    m_tracker.addSample(dev.rssi, dev.lastSeen);
                    m_lastFedSeen = dev.lastSeen;
                }
                break;
            }
        }

        // The target went silent past the drop timeout: release the lock and let the main
        // loop return us to the scanner (slice-0031 breadcrumb set at launch).
        if (m_tracker.evaluate(millis()).state == ProximityState::Dropped) {
            m_shouldExit = true;
        }
        m_needsRedraw = true;
    }
}

uint16_t BleTrackerScreen::bandColor(ProximityBand band) {
    switch (band) {
        case ProximityBand::Close: return theme::SUCCESS();  // green
        case ProximityBand::Mid:   return theme::WARNING();  // orange
        case ProximityBand::Far:   return theme::ERROR();    // red
    }
    return theme::TEXT_PRIMARY();
}

void BleTrackerScreen::render(Canvas& canvas) {
    if (!m_active || !m_needsRedraw) return;
    m_needsRedraw = false;

    ProximityReading reading = m_tracker.evaluate(millis());

    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "BLE TRACKER", stateLabel(reading.state));

    if (m_isInitializing) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setTextSize(1);
        canvas.setCursor(10, config::SCREEN_HEIGHT / 2);
        canvas.print("Initializing BLE...");
    } else {
        drawReading(canvas, reading);
    }

    footerHints_.render(canvas);
}

void BleTrackerScreen::drawReading(Canvas& canvas, const ProximityReading& reading) {
    // A Lost target is held but dimmed so the operator reads "it was just here", not a live
    // value; Acquiring has no sample yet.
    const bool dim = reading.state == ProximityState::Lost;
    const bool live = reading.state == ProximityState::Tracking;

    int y = 30;

    // Target identity.
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(10, y);
    canvas.print(m_targetName.c_str());
    y += 11;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.print(m_targetMac.c_str());
    y += 16;

    if (reading.state == ProximityState::Acquiring) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(10, y + 6);
        canvas.print("Listening for signal...");
        return;
    }

    const uint16_t band = dim ? theme::TEXT_DISABLED() : bandColor(reading.band);

    // Band + dBm, prominent.
    canvas.setTextSize(2);
    canvas.setTextColor(band);
    canvas.setCursor(10, y);
    canvas.print(bandLabel(reading.band));
    char dbm[12];
    snprintf(dbm, sizeof(dbm), "%d dBm", reading.smoothedRssi);
    canvas.setTextColor(dim ? theme::TEXT_DISABLED() : theme::TEXT_PRIMARY());
    canvas.setCursor(130, y + 2);
    canvas.setTextSize(1);
    canvas.print(dbm);
    y += 20;

    // Signal meter bar (NOT a distance bar: labelled as signal below).
    const int barX = 10;
    const int barW = config::SCREEN_WIDTH - 20;
    const int barH = 12;
    canvas.drawRect(barX, y, barW, barH, theme::TEXT_SECONDARY());
    int fillW = (barW - 2) * reading.meterPercent / 100;
    if (fillW > 0) {
        canvas.fillRect(barX + 1, y + 1, fillW, barH - 2, band);
    }
    y += barH + 6;

    // Direction of travel.
    if (live) {
        canvas.setTextColor(band);
        canvas.setCursor(10, y);
        canvas.print(trendLabel(reading.trend));
    } else if (dim) {
        char lostMsg[32];
        snprintf(lostMsg, sizeof(lostMsg), "No signal for %lus",
                 (unsigned long)(reading.ageMs / 1000));
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(10, y);
        canvas.print(lostMsg);
    }
    y += 14;

    // Honest-labelling: this is relative signal, never distance.
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(10, y);
    canvas.print("Signal strength, not distance");
}

bool BleTrackerScreen::handleInput(char key) {
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    switch (key) {
        case '`':  // ESC: back to the BLE scanner (breadcrumb set at launch).
            m_shouldExit = true;
            return true;
    }
    return false;
}

} // namespace adversary
