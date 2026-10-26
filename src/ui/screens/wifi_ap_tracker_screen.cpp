/**
 * @file wifi_ap_tracker_screen.cpp
 * @brief WifiApTrackerScreen implementation (ADR-0130, slice-0131).
 */

#include "wifi_ap_tracker_screen.h"

#include <cstdio>
#include <cstring>

#include "config/config.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "utils/log.h"

namespace adversary {

namespace {

// Re-poll the monitor at this cadence. Beacons arrive faster (~100 ms), but a tighter poll
// just reads the same scalars more often; the EWMA and age counter run off the sample
// timestamps, not the poll rate. (Duplicated from the BLE tracker shell by rule-of-three:
// a third proximity consumer should extract a shared meter component, see slice-0131.)
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

WifiApTrackerScreen::WifiApTrackerScreen()
    : m_monitor(ApRssiMonitor::getInstance())
    , m_tracker(kDefaultProximityConfig)
    , m_targetBssid{}
    , m_targetChannel(0)
    , m_haveTarget(false)
    , m_lastFedSeq(0)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_isInitializing(false)
    , m_pendingInit(0)
    , m_lastPoll(0) {
}

void WifiApTrackerScreen::setParams(const ScreenParams& params) {
    memcpy(m_targetBssid, params.bssid, 6);
    m_targetChannel = params.channel;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             m_targetBssid[0], m_targetBssid[1], m_targetBssid[2],
             m_targetBssid[3], m_targetBssid[4], m_targetBssid[5]);
    m_targetMac = mac;
    m_targetName = (params.ssid[0] != '\0') ? params.ssid : std::string("(hidden)");
    m_haveTarget = true;
    m_lastFedSeq = 0;
    m_tracker.reset();
}

void WifiApTrackerScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_lastPoll = 0;

    footerHints_.setHints({
        {'`', "Back", true}
    });
    footerHints_.setFocus(false);

    // Defer radio setup to update() to flatten the call stack, as the scanners do. No canvas
    // purge: promiscuous capture is light (unlike BLE/TLS), so there is no prepare/restore.
    m_isInitializing = true;
    m_pendingInit = 2;
}

void WifiApTrackerScreen::hide() {
    m_active = false;
    m_monitor.stop();
}

void WifiApTrackerScreen::update() {
    if (!m_active) return;

    if (m_isInitializing && m_pendingInit > 0) {
        if (--m_pendingInit == 0) {
            m_isInitializing = false;
            m_monitor.start(m_targetBssid, m_targetChannel);
        }
        m_needsRedraw = true;
        return;
    }

    if (!m_haveTarget) return;

    if (millis() - m_lastPoll >= POLL_INTERVAL_MS) {
        m_lastPoll = millis();

        // Feed each new beacon once: the monotonic sampleSeq changes per matching frame.
        if (m_monitor.hasSample()) {
            uint32_t seq = m_monitor.sampleSeq();
            if (seq != m_lastFedSeq) {
                m_tracker.addSample(m_monitor.lastRssi(), m_monitor.lastSeenMs());
                m_lastFedSeq = seq;
            }
        }

        // Silent past the drop timeout (out of range, powered off, or moved channel): release
        // the lock and let the main loop return us to the scanner (slice-0031 breadcrumb).
        if (m_tracker.evaluate(millis()).state == ProximityState::Dropped) {
            m_shouldExit = true;
        }
        m_needsRedraw = true;
    }
}

uint16_t WifiApTrackerScreen::bandColor(ProximityBand band) {
    switch (band) {
        case ProximityBand::Close: return theme::SUCCESS();  // green
        case ProximityBand::Mid:   return theme::WARNING();  // orange
        case ProximityBand::Far:   return theme::ERROR();    // red
    }
    return theme::TEXT_PRIMARY();
}

void WifiApTrackerScreen::render(Canvas& canvas) {
    if (!m_active || !m_needsRedraw) return;
    m_needsRedraw = false;

    ProximityReading reading = m_tracker.evaluate(millis());

    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "WIFI AP TRACKER", stateLabel(reading.state));

    if (m_isInitializing) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setTextSize(1);
        canvas.setCursor(10, config::SCREEN_HEIGHT / 2);
        canvas.print("Locking channel...");
    } else {
        drawReading(canvas, reading);
    }

    footerHints_.render(canvas);
}

void WifiApTrackerScreen::drawReading(Canvas& canvas, const ProximityReading& reading) {
    const bool dim = reading.state == ProximityState::Lost;
    const bool live = reading.state == ProximityState::Tracking;

    int y = 30;

    // Target identity: SSID, then BSSID + channel.
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(10, y);
    canvas.print(m_targetName.c_str());
    y += 11;
    char idLine[40];
    snprintf(idLine, sizeof(idLine), "%s  CH %u", m_targetMac.c_str(), (unsigned)m_targetChannel);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(10, y);
    canvas.print(idLine);
    y += 16;

    if (reading.state == ProximityState::Acquiring) {
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(10, y + 6);
        canvas.print("Listening for beacons...");
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

bool WifiApTrackerScreen::handleInput(char key) {
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    switch (key) {
        case '`':  // ESC: back to the WiFi scanner (breadcrumb set at launch).
            m_shouldExit = true;
            return true;
    }
    return false;
}

} // namespace adversary
