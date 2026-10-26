/**
 * @file ble_tracker_screen.h
 * @brief Hot/cold proximity tracker for a single BLE device (ADR-0128, slice-0129).
 *
 * Locks onto a device chosen in the BLE scanner and guides the operator toward it by
 * signal strength. The decision logic (smoothing, banding, direction, lost-signal state)
 * is the shared pure core ProximityTracker; this screen is the device shell that feeds it
 * live advertisement RSSI and renders the result. Output is honest relative signal, never
 * distance.
 */

#pragma once

#include <string>

#include "ui/screens/screen_interface.h"
#include "ui/components/footer_hints.h"
#include "core/proximity_tracker.h"
#include "modules/ble/ble_scanner.h"

namespace adversary {

class BleTrackerScreen : public IScreen {
public:
    BleTrackerScreen();
    ~BleTrackerScreen() override = default;

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }
    const char* getName() const override { return "BLE Tracker"; }
    ScreenId getId() const override { return ScreenId::BLE_TRACKER; }

    /// The locked target arrives via the navigation params: bssid = the BLE address bytes
    /// (NimBLEAddress::getNative order), ssid = the advertised name (or stand-in).
    void setParams(const ScreenParams& params) override;

private:
    void drawReading(Canvas& canvas, const ProximityReading& reading);
    static uint16_t bandColor(ProximityBand band);

    BLEScanner& m_scanner;
    ProximityTracker m_tracker;
    ui::FooterHints footerHints_;

    uint8_t m_targetAddr[6];  ///< getNative()-order bytes; matched against live adverts by memcmp.
    std::string m_targetName; ///< Display name of the locked device.
    std::string m_targetMac;  ///< Canonical MAC string for display.
    bool m_haveTarget;
    uint32_t m_lastFedSeen;   ///< lastSeen of the last advert fed, so each advert is fed once.

    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    bool m_isInitializing;
    int m_pendingInit;
    uint32_t m_lastPoll;
};

} // namespace adversary
