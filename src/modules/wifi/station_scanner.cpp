/**
 * @file station_scanner.cpp
 * @brief Passive station scanner implementation
 */

#include "station_scanner.h"
#include "../../utils/wifi_utils.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

// Static instance pointer for callback (set/cleared on start/stop)
StationScanner* StationScanner::s_instance_ = nullptr;

// Singleton storage pointer
static StationScanner* s_mgr = nullptr;

StationScanner& StationScanner::getInstance() {
    if (!s_mgr) s_mgr = new StationScanner();
    return *s_mgr;
}

bool StationScanner::start(uint8_t channel, uint32_t durationMs) {
#ifdef ESP32
    if (scanning_) {
        stop();
    }
    
    results_.clear();
    filterByBssid_ = false;
    memset(targetBssid_, 0, 6);
    
    // Set WiFi to station mode for promiscuous
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
    
    // Set channel
    wifi_utils::setChannel(channel);
    
    // Set static instance for callback
    s_instance_ = this;
    
    // Enable promiscuous mode with data frame filter
    auto filter = wifi_utils::dataFrameFilter();
    wifi_utils::enablePromiscuous(promiscuousCallback, &filter);
    
    scanning_ = true;
    startTime_ = millis();
    duration_ = durationMs;
    
    Serial.printf("[StationScanner] Started on channel %d for %lums\n", channel, durationMs);
    return true;
#else
    (void)channel;
    (void)durationMs;
    return false;
#endif
}

bool StationScanner::startForAP(const uint8_t* bssid, uint8_t channel, uint32_t durationMs) {
#ifdef ESP32
    if (!bssid) return false;
    
    if (!start(channel, durationMs)) {
        return false;
    }
    
    // Enable BSSID filtering
    filterByBssid_ = true;
    memcpy(targetBssid_, bssid, 6);
    
    Serial.printf("[StationScanner] Filtering for AP %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    
    return true;
#else
    (void)bssid;
    (void)channel;
    (void)durationMs;
    return false;
#endif
}

void StationScanner::stop() {
#ifdef ESP32
    if (!scanning_) return;
    
    wifi_utils::disablePromiscuous();
    scanning_ = false;
    s_instance_ = nullptr;
    
    Serial.printf("[StationScanner] Stopped. Found %zu stations\n", results_.count);
#endif
}

void StationScanner::update() {
#ifdef ESP32
    if (!scanning_) return;
    
    // Check timeout
    if (duration_ > 0 && (millis() - startTime_) >= duration_) {
        stop();
    }
#endif
}

size_t StationScanner::getStationsForAP(const uint8_t* bssid, uint8_t* outMacs, size_t maxCount) const {
    if (!bssid || !outMacs || maxCount == 0) return 0;
    
    size_t copied = 0;
    for (size_t i = 0; i < results_.count && copied < maxCount; i++) {
        if (results_.stations[i].isForAP(bssid)) {
            memcpy(outMacs + (copied * 6), results_.stations[i].mac, 6);
            copied++;
        }
    }
    
    return copied;
}

#ifdef ESP32
void StationScanner::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_instance_ || type != WIFI_PKT_DATA) return;
    
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    s_instance_->processFrame(pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.rssi);
}
#endif

void StationScanner::processFrame(const uint8_t* data, uint16_t len, int8_t rssi) {
#ifdef ESP32
    if (len < 24) return;  // Minimum 802.11 header
    
    // Frame control field
    uint16_t frameControl = data[0] | (data[1] << 8);
    
    // Check if data frame (type = 2)
    uint8_t frameType = (frameControl >> 2) & 0x03;
    if (frameType != 2) return;  // Not a data frame
    
    // Extract ToDS and FromDS flags
    bool toDS = (frameControl & 0x0100) != 0;
    bool fromDS = (frameControl & 0x0200) != 0;
    
    const uint8_t* clientMac = nullptr;
    const uint8_t* bssid = nullptr;
    
    if (toDS && !fromDS) {
        // Client → AP: addr2 is client, addr1 is BSSID (or addr3)
        clientMac = &data[10];  // Address 2 (Source)
        bssid = &data[4];       // Address 1 (Receiver/BSSID)
    } else if (!toDS && fromDS) {
        // AP → Client: addr1 is client, addr2 is BSSID
        clientMac = &data[4];   // Address 1 (Destination)
        bssid = &data[10];      // Address 2 (Transmitter/BSSID)
    } else {
        // WDS or ad-hoc, skip
        return;
    }
    
    // Filter by BSSID if enabled
    if (filterByBssid_) {
        if (memcmp(bssid, targetBssid_, 6) != 0) {
            return;
        }
    }
    
    // Skip broadcast/multicast client MACs
    if (clientMac[0] & 0x01) return;
    
    // Add or update station
    bool isNew = results_.addOrUpdate(clientMac, bssid, rssi);
    
    if (isNew) {
        Serial.printf("[StationScanner] New client: %02X:%02X:%02X:%02X:%02X:%02X (RSSI:%d)\n",
                      clientMac[0], clientMac[1], clientMac[2],
                      clientMac[3], clientMac[4], clientMac[5], rssi);
    }
#else
    (void)data;
    (void)len;
    (void)rssi;
#endif
}

} // namespace adversary
