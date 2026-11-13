/**
 * @file wifi_scanner.cpp
 * @brief WiFi network scanner implementation
 */

#include "wifi_scanner.h"
#include "core/event_bus.h"
#include "config/config.h"
#include "utils/log.h"
#include "utils/wifi_utils.h"
#include "utils/rsn_ie_parser.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#endif

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace adversary {

// NetworkInfo methods
std::string NetworkInfo::getBssidString() const {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return std::string(buf);
}

const char* wifiSecurityToString(WiFiSecurity security) {
    switch (security) {
        case WiFiSecurity::OPEN:           return "OPEN";
        case WiFiSecurity::WEP:            return "WEP";
        case WiFiSecurity::WPA_PSK:        return "WPA";
        case WiFiSecurity::WPA2_PSK:       return "WPA2";
        case WiFiSecurity::WPA_WPA2_PSK:   return "WPA/2";
        case WiFiSecurity::WPA2_ENTERPRISE: return "WPA2-E";
        case WiFiSecurity::WPA3_PSK:       return "WPA3";
        case WiFiSecurity::WPA2_WPA3_PSK:  return "WPA2/3";
        default:                           return "???";
    }
}

const char* NetworkInfo::getSecurityString() const {
    return wifiSecurityToString(security);
}

uint8_t NetworkInfo::getSignalQuality() const {
    // Convert RSSI to percentage
    // Typical range: -100 dBm (worst) to -30 dBm (best)
    if (rssi >= -30) return 100;
    if (rssi <= -100) return 0;
    int quality = 2 * (rssi + 100);
    return static_cast<uint8_t>(quality > 100 ? 100 : quality);
}

// WiFiScanner implementation
WiFiScanner& WiFiScanner::getInstance() {
    static WiFiScanner instance;
    return instance;
}

WiFiScanner::WiFiScanner()
    : m_state(ScannerState::IDLE)
    , m_scanCount(0)
    , m_scanStartTime(0)
    , m_lastScanTime(0)
    , m_continuousInterval(3000)
    , m_initialized(false)
    , m_continuousMode(false)
{
}

WiFiScanner::~WiFiScanner() {
    deinit();
}

#ifndef UNIT_TEST

bool WiFiScanner::init() {
    if (m_initialized) {
        return true;
    }
    
    LOG_I("WIFISCAN", "Initializing...");
    
    // Preserve existing connection if present
    bool wasConnected = WiFi.isConnected();
    String savedSSID;
    String savedPass;
    
    if (wasConnected) {
        LOG_I("WIFISCAN", "Preserving existing connection");
        // WiFi is already in STA mode and connected - don't disconnect
    } else {
        // Set WiFi mode to station for scanning
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
    }
    
    // Configure the regulatory domain for all 14 channels (the default limits to
    // 1-11). Re-applied on every startScan() too, since a capture/attack between
    // scans resets it (see wifi_utils::applyFullChannelRegulatory).
    wifi_utils::applyFullChannelRegulatory();
    
    // Extend minimum active scan time per channel for better detection
    // Default is ~100ms, we use 300ms to catch slow-responding APs
    WiFi.setScanActiveMinTime(300);
    
    // Allow time for mode change
    delay(100);
    
    m_initialized = true;
    m_state = ScannerState::IDLE;
    
    LOG_I("WIFISCAN", "Ready (CH1-14, 300ms min/ch)");
    return true;
}

void WiFiScanner::deinit() {
    if (!m_initialized) {
        return;
    }
    
    stopScan();
    m_networks.clear();
    m_initialized = false;
    m_state = ScannerState::IDLE;
    
    LOG_I("WIFISCAN", "Deinitialized");
}

bool WiFiScanner::startScan() {
    if (!m_initialized) {
        LOG_W("WIFISCAN", "Error: Not initialized");
        return false;
    }
    
    if (m_state == ScannerState::SCANNING) {
        LOG_W("WIFISCAN", "Scan already in progress");
        return false;
    }
    
    m_scanStartTime = millis();
    
    // Start async scan with all options for maximum detection:
    // async=true, show_hidden=true, passive=true (passive scan catches more APs)
    // channel=0 (all channels), ms_per_chan=300 (extended dwell time)
    
    // ERROR 34 "MISSING_ACKS" Fix: Force Max TX Power ensures Probe Requests reach distant APs
    // Ensure STA mode is active so setTxPower works (avoids "Neither AP or STA has been started")
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Re-apply the full 1-14 domain before every scan: a prior capture/attack in
    // the autohunt loop cycles WiFi and resets the country to 1-11, which would
    // otherwise make the scan blind to ch12-13 APs (set once at init() is not
    // enough).
    wifi_utils::applyFullChannelRegulatory();

    int16_t result = WiFi.scanNetworks(
        true,   // async
        true,   // show_hidden
        true,   // passive (listens longer, better detection)
        300     // 300ms per channel (default is ~100ms)
    );
    
    if (result == WIFI_SCAN_RUNNING) {
        m_state = ScannerState::SCANNING;
        LOG_I("WIFISCAN", "Scan started (passive, 300ms/ch)");
        return true;
    } else if (result == WIFI_SCAN_FAILED) {
        m_state = ScannerState::ERROR;
        LOG_E("WIFISCAN", "Scan failed to start");
        return false;
    }
    
    // Immediate result (shouldn't happen with async)
    m_state = ScannerState::SCANNING;
    return true;
}

bool WiFiScanner::scanSync(uint32_t timeoutMs) {
    if (!m_initialized) {
        return false;
    }
    
    m_scanStartTime = millis();
    m_state = ScannerState::SCANNING;
    
    LOG_I("WIFISCAN", "Starting synchronous scan...");
    
    // Synchronous scan with hidden networks
    int16_t count = WiFi.scanNetworks(false, true);  // async=false, show_hidden=true
    
    if (count < 0) {
        m_state = ScannerState::ERROR;
        LOG_E("WIFISCAN", "Scan failed: %d", count);
        return false;
    }
    
    processResults();
    return true;
}

void WiFiScanner::stopScan() {
    if (m_state == ScannerState::SCANNING) {
        WiFi.scanDelete();
        m_state = ScannerState::IDLE;
        LOG_I("WIFISCAN", "Scan stopped");
    }
}

void WiFiScanner::update() {
    if (!m_initialized) {
        return;
    }
    
    // Check for async scan completion
    if (m_state == ScannerState::SCANNING) {
        int16_t result = WiFi.scanComplete();
        
        if (result >= 0) {
            // Scan complete
            processResults();
            
            // EventBus event published in processResults
        } else if (result == WIFI_SCAN_FAILED) {
            m_state = ScannerState::ERROR;
            LOG_E("WIFISCAN", "Scan failed");
        }
        // WIFI_SCAN_RUNNING means still scanning
    }
    
    // Handle continuous mode
    if (m_continuousMode && m_state == ScannerState::COMPLETED) {
        uint32_t now = millis();
        if (now - m_lastScanTime >= m_continuousInterval) {
            startScan();
        }
    }
}

void WiFiScanner::processResults() {
    int16_t count = WiFi.scanComplete();
    if (count < 0) {
        count = 0;
    }
    
    m_networks.clear();
    m_networks.reserve(count);
    
    for (int i = 0; i < count; i++) {
        NetworkInfo net;
        
        // Get SSID
        String ssid = WiFi.SSID(i);
        net.ssid = ssid.c_str();
        net.isHidden = (ssid.length() == 0);
        
        // Get BSSID
        uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) {
            memcpy(net.bssid, bssid, 6);
        } else {
            memset(net.bssid, 0, 6);
        }
        
        // Get other info
        net.rssi = WiFi.RSSI(i);
        net.channel = WiFi.channel(i);
        net.security = authModeToSecurity(WiFi.encryptionType(i));
        net.lastSeen = millis();
        
        m_networks.push_back(net);
    }
    
    // Clean up scan results
    WiFi.scanDelete();

    // Fill PMF bits by sniffing beacons on each discovered channel. Done
    // between scan completion and the WIFI_SCAN_COMPLETED event so every
    // consumer sees the bits already on the record (ADR-0153, slice-0154).
    runPmfSniffPass();

    m_scanCount++;
    m_lastScanTime = millis();
    m_state = ScannerState::COMPLETED;

    LOG_I("WIFISCAN", "Found %d networks in %lu ms",
          count, m_lastScanTime - m_scanStartTime);

    // Publish scan completion event
    adversary::EventData event(adversary::EventType::WIFI_SCAN_COMPLETED);
    event.payload.scan.count = count;
    event.payload.scan.duration = m_lastScanTime - m_scanStartTime;
    adversary::EventBus::getInstance().publish(event);
}


WiFiSecurity WiFiScanner::authModeToSecurity(uint8_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:
            return WiFiSecurity::OPEN;
        case WIFI_AUTH_WEP:
            return WiFiSecurity::WEP;
        case WIFI_AUTH_WPA_PSK:
            return WiFiSecurity::WPA_PSK;
        case WIFI_AUTH_WPA2_PSK:
            return WiFiSecurity::WPA2_PSK;
        case WIFI_AUTH_WPA_WPA2_PSK:
            return WiFiSecurity::WPA_WPA2_PSK;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return WiFiSecurity::WPA2_ENTERPRISE;
        case WIFI_AUTH_WPA3_PSK:
            return WiFiSecurity::WPA3_PSK;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return WiFiSecurity::WPA2_WPA3_PSK;
        default:
            return WiFiSecurity::UNKNOWN;
    }
}

#else // UNIT_TEST stubs

bool WiFiScanner::init() {
    m_initialized = true;
    m_state = ScannerState::IDLE;
    return true;
}

void WiFiScanner::deinit() {
    m_initialized = false;
}

bool WiFiScanner::startScan() {
    if (!m_initialized) return false;
    m_state = ScannerState::SCANNING;
    return true;
}

bool WiFiScanner::scanSync(uint32_t timeoutMs) {
    if (!m_initialized) return false;
    m_state = ScannerState::COMPLETED;
    m_scanCount++;
    return true;
}

void WiFiScanner::stopScan() {
    m_state = ScannerState::IDLE;
}

void WiFiScanner::update() {
    // No-op for tests
}

void WiFiScanner::processResults() {
    // No-op for tests
}

WiFiSecurity WiFiScanner::authModeToSecurity(uint8_t authMode) {
    return WiFiSecurity::UNKNOWN;
}

#endif // UNIT_TEST

// Common methods (work for both real and test builds)

void WiFiScanner::clearResults() {
    m_networks.clear();
    m_networks.shrink_to_fit();
}

const NetworkInfo* WiFiScanner::getNetwork(size_t index) const {
    if (index >= m_networks.size()) {
        return nullptr;
    }
    return &m_networks[index];
}

void WiFiScanner::sortBySignal() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.rssi > b.rssi;  // Higher RSSI = stronger signal
              });
}

void WiFiScanner::sortByChannel() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.channel < b.channel;
              });
}

void WiFiScanner::sortBySSID() {
    std::sort(m_networks.begin(), m_networks.end(),
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.ssid < b.ssid;
              });
}

void WiFiScanner::setContinuousMode(bool enable, uint32_t intervalMs) {
    m_continuousMode = enable;
    m_continuousInterval = intervalMs;

    if (enable && m_state != ScannerState::SCANNING) {
        startScan();
    }
}

bool WiFiScanner::applyPmfBitsByBssid(std::vector<NetworkInfo>& list,
                                      const uint8_t bssid[6],
                                      bool pmfCapable,
                                      bool pmfRequired) {
    for (auto& net : list) {
        if (memcmp(net.bssid, bssid, 6) == 0) {
            net.pmfCapable = pmfCapable;
            net.pmfRequired = pmfRequired;
            return true;
        }
    }
    return false;
}

#ifndef UNIT_TEST

namespace {

// Dwell per channel. Typical 2.4 GHz beacon interval is ~100 ms, so one dwell
// catches at least one beacon from every BSSID on that channel under normal
// conditions. Tunable from here if on-device latency measurement motivates it.
constexpr uint32_t PMF_SNIFF_DWELL_MS = 120;

// Module-local pointer the RX callback consults: the promiscuous callback
// signature carries no context, and the scan path is single-threaded, so a
// pointer owned by runPmfSniffPass and cleared on exit is sufficient.
std::vector<NetworkInfo>* g_pmfSniffList = nullptr;

// 802.11 management frame subtypes we care about: beacon (8) and probe-response
// (5). Both carry the same fixed-parameter + tagged-parameter layout.
constexpr uint8_t MGMT_SUBTYPE_BEACON = 8;
constexpr uint8_t MGMT_SUBTYPE_PROBE_RESPONSE = 5;

// MAC-header + fixed-parameters offset constants (bytes from frame start).
constexpr size_t MGMT_HEADER_LEN = 24;      // FC(2)+Dur(2)+DA(6)+SA(6)+BSSID(6)+Seq(2)
constexpr size_t MGMT_FIXED_PARAMS_LEN = 12;  // Timestamp(8)+Interval(2)+Caps(2)
constexpr size_t MGMT_TAGGED_OFFSET = MGMT_HEADER_LEN + MGMT_FIXED_PARAMS_LEN;
constexpr size_t MGMT_BSSID_OFFSET = 16;

void IRAM_ATTR pmfSniffRxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || g_pmfSniffList == nullptr) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    const uint8_t* payload = pkt->payload;
    const uint32_t len = pkt->rx_ctrl.sig_len;
    if (len < MGMT_TAGGED_OFFSET) return;

    // Frame Control byte 0 carries Type (bits 2-3) and Subtype (bits 4-7).
    // The type filter already selected mgmt; check subtype for beacon/probe-resp.
    const uint8_t subtype = (payload[0] >> 4) & 0x0F;
    if (subtype != MGMT_SUBTYPE_BEACON && subtype != MGMT_SUBTYPE_PROBE_RESPONSE) {
        return;
    }

    const uint8_t* bssid = payload + MGMT_BSSID_OFFSET;

    // Walk the tagged parameters. Stop at the first RSN IE: one per beacon.
    size_t off = MGMT_TAGGED_OFFSET;
    while (off + 2 <= len) {
        const uint8_t tag = payload[off];
        const uint8_t tagLen = payload[off + 1];
        if (off + 2 + tagLen > len) break;
        if (tag == adversary::utils::RSN_IE_ELEMENT_ID) {
            bool mfpc = false, mfpr = false;
            if (adversary::utils::parseRsnPmfBits(payload + off,
                                                   static_cast<size_t>(2) + tagLen,
                                                   &mfpc, &mfpr)) {
                WiFiScanner::applyPmfBitsByBssid(*g_pmfSniffList, bssid, mfpc, mfpr);
            }
            return;
        }
        off += static_cast<size_t>(2) + tagLen;
    }
}

} // namespace

void WiFiScanner::runPmfSniffPass() {
    if (m_networks.empty()) return;

    // Distinct 2.4 GHz channels appearing in the scan result.
    std::vector<uint8_t> channels;
    channels.reserve(14);
    for (const auto& net : m_networks) {
        if (net.channel < 1 || net.channel > 14) continue;
        if (std::find(channels.begin(), channels.end(), net.channel) == channels.end()) {
            channels.push_back(net.channel);
        }
    }
    if (channels.empty()) return;

    g_pmfSniffList = &m_networks;

    wifi_promiscuous_filter_t filter = wifi_utils::managementFrameFilter();
    if (!wifi_utils::enablePromiscuous(pmfSniffRxCallback, &filter)) {
        g_pmfSniffList = nullptr;
        LOG_W("WIFISCAN", "PMF sniff: enablePromiscuous failed, skipping");
        return;
    }

    const uint32_t start = millis();
    for (uint8_t ch : channels) {
        wifi_utils::setChannel(ch);
        delay(PMF_SNIFF_DWELL_MS);
    }

    wifi_utils::disablePromiscuous();
    g_pmfSniffList = nullptr;

    LOG_I("WIFISCAN", "PMF sniff: %u channels in %lu ms",
          static_cast<unsigned>(channels.size()),
          static_cast<unsigned long>(millis() - start));
}

#else

void WiFiScanner::runPmfSniffPass() {
    // No-op on the native host build: no radio.
}

#endif

} // namespace adversary
