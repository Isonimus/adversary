/**
 * @file wifi_ap_tracker_screen.h
 * @brief Hot/cold proximity tracker for a single WiFi AP (ADR-0128, ADR-0130, slice-0131).
 *
 * Locks onto an AP chosen in the WiFi scanner and guides the operator toward it by signal
 * strength, reading the AP's beacons on its own channel (ApRssiMonitor). The decision logic
 * is the shared pure core ProximityTracker; this screen is the device shell that feeds it
 * live beacon RSSI and renders the result. Output is honest relative signal, never distance.
 */

#pragma once

#include <string>

#include "ui/screens/screen_interface.h"
#include "ui/components/footer_hints.h"
#include "core/proximity_tracker.h"
#include "modules/wifi/ap_rssi_monitor.h"

namespace adversary {

class WifiApTrackerScreen : public IScreen {
public:
    WifiApTrackerScreen();
    ~WifiApTrackerScreen() override = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }
    const char* getName() const override { return "WiFi AP Tracker"; }
    ScreenId getId() const override { return ScreenId::WIFI_AP_TRACKER; }

    /// The locked target arrives via the navigation params: bssid = the AP BSSID, ssid = the
    /// network name (or empty for hidden), channel = the AP's channel.
    void setParams(const ScreenParams& params) override;

private:
    void drawReading(Canvas& canvas, const ProximityReading& reading);
    static uint16_t bandColor(ProximityBand band);

    ApRssiMonitor& m_monitor;
    ProximityTracker m_tracker;
    ui::FooterHints footerHints_;

    uint8_t m_targetBssid[6];  ///< Matched against beacon transmitters inside the monitor.
    std::string m_targetName;  ///< SSID, or a hidden-network stand-in.
    std::string m_targetMac;   ///< Canonical BSSID string for display.
    uint8_t m_targetChannel;
    bool m_haveTarget;
    uint32_t m_lastFedSeq;     ///< sampleSeq of the last frame fed, so each is fed once.

    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    bool m_isInitializing;
    int m_pendingInit;
    uint32_t m_lastPoll;
};

} // namespace adversary
