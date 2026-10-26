/**
 * @file ap_rssi_monitor.cpp
 * @brief ApRssiMonitor implementation (ADR-0130).
 */

#include "ap_rssi_monitor.h"

#include "core/wifi_frame.h"
#include "utils/wifi_utils.h"
#include "utils/log.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

ApRssiMonitor* ApRssiMonitor::s_instance_ = nullptr;

static ApRssiMonitor* s_mgr = nullptr;

ApRssiMonitor& ApRssiMonitor::getInstance() {
    if (!s_mgr) s_mgr = new ApRssiMonitor();
    return *s_mgr;
}

bool ApRssiMonitor::start(const uint8_t bssid[6], uint8_t channel) {
#ifdef ESP32
    if (!bssid) return false;
    if (running_) stop();

    memcpy(targetBssid_, bssid, 6);
    lastRssi_ = 0;
    lastSeenMs_ = 0;
    sampleSeq_ = 0;

    // Station mode + locked channel: no scan, no hop. Dropping any association first so the
    // channel we set is the one we stay on.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
    wifi_utils::setChannel(channel);

    s_instance_ = this;
    wifi_promiscuous_filter_t filter = wifi_utils::managementFrameFilter();
    wifi_utils::enablePromiscuous(promiscuousCallback, &filter);

    running_ = true;
    LOG_I("APRSSI", "Tracking %02X:%02X:%02X:%02X:%02X:%02X on channel %d",
          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
    return true;
#else
    (void)bssid;
    (void)channel;
    return false;
#endif
}

void ApRssiMonitor::stop() {
#ifdef ESP32
    if (!running_) return;
    wifi_utils::disablePromiscuous();
    running_ = false;
    s_instance_ = nullptr;
    LOG_I("APRSSI", "Stopped");
#endif
}

#ifdef ESP32
void ApRssiMonitor::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_instance_ || type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* pkt = reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf);
    s_instance_->onFrame(pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.rssi);
}

void ApRssiMonitor::onFrame(const uint8_t* data, uint16_t len, int8_t rssi) {
    // Only the target AP's own transmissions carry its signal (pure, host-tested filter).
    if (!managementFrameFromBssid(data, len, targetBssid_)) return;
    lastRssi_ = rssi;
    lastSeenMs_ = millis();
    ++sampleSeq_;
}
#endif

} // namespace adversary
