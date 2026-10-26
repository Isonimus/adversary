/**
 * @file ap_rssi_monitor.h
 * @brief Locked-channel beacon RSSI monitor for a single AP (ADR-0130).
 *
 * The WiFi-AP proximity tracker needs a responsive per-sample RSSI feed for one AP, not the
 * multi-second full-channel sweep of WiFiScanner. This module locks the radio to the AP's
 * channel, enables promiscuous mode with the management-frame filter, and keeps the RSSI of
 * the frames the AP itself transmits (beacons, probe responses). Beacons arrive about every
 * 100 ms whether or not a client is talking, and a hidden AP still beacons, so the feed stays
 * live on idle and hidden APs alike.
 *
 * Singleton like StationScanner: esp_wifi routes the promiscuous callback through one global
 * C function, so there is a single owner of that callback.
 */

#pragma once

#include <cstdint>

#ifdef ESP32
#include <esp_wifi.h>
#endif

namespace adversary {

/**
 * @brief Reports the latest RSSI of a target AP's own transmissions on its locked channel.
 */
class ApRssiMonitor {
public:
    static ApRssiMonitor& getInstance();

    ApRssiMonitor(const ApRssiMonitor&) = delete;
    ApRssiMonitor& operator=(const ApRssiMonitor&) = delete;

    /**
     * @brief Lock to `channel` and begin reporting RSSI for management frames from `bssid`.
     *
     * Idempotent restart: a running monitor is stopped first. Puts the radio in station mode
     * and enables promiscuous capture. Returns false on a non-ESP32 (native) build.
     */
    bool start(const uint8_t bssid[6], uint8_t channel);

    /// Disable promiscuous capture and release the callback.
    void stop();

    bool isRunning() const { return running_; }

    /// A sample has been seen since the last start() (sequence is non-zero).
    bool hasSample() const { return sampleSeq_ != 0; }

    /// Most recent RSSI (dBm) heard from the target.
    int8_t lastRssi() const { return lastRssi_; }

    /// millis() timestamp of the most recent matching frame.
    uint32_t lastSeenMs() const { return lastSeenMs_; }

    /// Monotonic count of matching frames: a consumer feeds each new sample once by watching
    /// this change, rather than re-feeding the same RSSI every poll.
    uint32_t sampleSeq() const { return sampleSeq_; }

private:
    ApRssiMonitor() = default;
    ~ApRssiMonitor() = default;

#ifdef ESP32
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    void onFrame(const uint8_t* data, uint16_t len, int8_t rssi);
#endif

    static ApRssiMonitor* s_instance_;

    uint8_t targetBssid_[6] = {0};
    bool running_ = false;

    // Written in the promiscuous callback, read in the UI task. Scalar, so each read sees a
    // whole value; sampleSeq_ orders them (a consumer reads the seq, then the paired fields).
    volatile int8_t lastRssi_ = 0;
    volatile uint32_t lastSeenMs_ = 0;
    volatile uint32_t sampleSeq_ = 0;
};

} // namespace adversary
