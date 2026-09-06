/**
 * @file packet_sniffer.cpp
 * @brief 802.11 Packet Sniffer implementation
 */

#include "packet_sniffer.h"
#include "../../core/event_bus.h"
#include "../../utils/wifi_utils.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#else
// Native build stubs
#endif

namespace adversary {

// ===========================================
// CapturedPacket implementation
// ===========================================

bool CapturedPacket::isEAPOL() const {
    // EAPOL frames are data frames with EtherType 0x888E
    if (frameType != FrameType::DATA) return false;
    if (length < 36) return false;  // Minimum for EAPOL with headers
    
    // Calculate 802.11 header size based on frame control
    // Frame Control is at data[0-1]
    uint16_t frameControl = data[0] | (data[1] << 8);
    
    // Base header size is 24 bytes (FC + Duration + Addr1 + Addr2 + Addr3 + SeqCtl)
    size_t headerSize = 24;
    
    // Check ToDS and FromDS bits (bits 8 and 9 of frame control)
    uint8_t toDs = (frameControl >> 8) & 0x01;
    uint8_t fromDs = (frameControl >> 9) & 0x01;
    
    // If both ToDS and FromDS are set (WDS mode), there's a 4th address
    if (toDs && fromDs) {
        headerSize += 6;  // Add Address 4
    }
    
    // Check if this is a QoS data frame (subtype bit 3 set, i.e., subtypes 8-15)
    uint8_t subtype_local = (frameControl >> 4) & 0x0F;
    if (subtype_local >= 8) {  // QoS Data subtypes
        headerSize += 2;  // QoS Control field
    }
    
    // After 802.11 header, look for LLC/SNAP header:
    // LLC: AA AA 03 (DSAP=AA, SSAP=AA, Control=03)
    // OUI: 00 00 00
    // EtherType: 88 8E (EAPOL)
    if (length < headerSize + 8) return false;  // Need at least LLC/SNAP + EtherType
    
    const uint8_t* llc = data + headerSize;
    
    // Check for LLC/SNAP header with EAPOL EtherType
    if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
        llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00 &&
        llc[6] == 0x88 && llc[7] == 0x8E) {
        return true;
    }
    
    return false;
}

bool CapturedPacket::isBeacon() const {
    return frameType == FrameType::MANAGEMENT && 
           subtype == static_cast<uint8_t>(ManagementSubtype::BEACON);
}

bool CapturedPacket::isDeauth() const {
    return frameType == FrameType::MANAGEMENT && 
           (subtype == static_cast<uint8_t>(ManagementSubtype::DEAUTHENTICATION) ||
            subtype == static_cast<uint8_t>(ManagementSubtype::DISASSOCIATION));
}

bool CapturedPacket::isProbeRequest() const {
    return frameType == FrameType::MANAGEMENT && 
           subtype == static_cast<uint8_t>(ManagementSubtype::PROBE_REQ);
}

bool CapturedPacket::isProbeResponse() const {
    return frameType == FrameType::MANAGEMENT && 
           subtype == static_cast<uint8_t>(ManagementSubtype::PROBE_RESP);
}

const char* CapturedPacket::getFrameTypeString() const {
    switch (frameType) {
        case FrameType::MANAGEMENT:
            switch (static_cast<ManagementSubtype>(subtype)) {
                case ManagementSubtype::BEACON: return "Beacon";
                case ManagementSubtype::PROBE_REQ: return "ProbeReq";
                case ManagementSubtype::PROBE_RESP: return "ProbeResp";
                case ManagementSubtype::AUTHENTICATION: return "Auth";
                case ManagementSubtype::DEAUTHENTICATION: return "Deauth";
                case ManagementSubtype::DISASSOCIATION: return "Disassoc";
                case ManagementSubtype::ASSOCIATION_REQ: return "AssocReq";
                case ManagementSubtype::ASSOCIATION_RESP: return "AssocResp";
                case ManagementSubtype::ACTION: return "Action";
                default: return "Mgmt";
            }
        case FrameType::CONTROL:
            return "Ctrl";
        case FrameType::DATA:
            if (isEAPOL()) return "EAPOL";
            return "Data";
        default:
            return "Unknown";
    }
}

// ===========================================
// SnifferFilter implementation
// ===========================================

bool SnifferFilter::hasBSSIDFilter() const {
    for (int i = 0; i < 6; i++) {
        if (targetBSSID[i] != 0) return true;
    }
    return false;
}

void SnifferFilter::clearBSSIDFilter() {
    memset(targetBSSID, 0, 6);
}

void SnifferFilter::setBSSIDFilter(const uint8_t* bssid) {
    memcpy(targetBSSID, bssid, 6);
}

// ===========================================
// SnifferStats implementation
// ===========================================

void SnifferStats::reset() {
    totalPackets = 0;
    managementFrames = 0;
    controlFrames = 0;
    dataFrames = 0;
    beacons = 0;
    probeRequests = 0;
    probeResponses = 0;
    deauthFrames = 0;
    eapolFrames = 0;
    droppedPackets = 0;
    startTime = millis();
    currentChannel = 1;
}

uint32_t SnifferStats::getDurationSeconds() const {
    return (millis() - startTime) / 1000;
}

float SnifferStats::getPacketsPerSecond() const {
    uint32_t duration = getDurationSeconds();
    if (duration == 0) return 0.0f;
    return static_cast<float>(totalPackets) / duration;
}

// ===========================================
// PacketSniffer implementation
// ===========================================

// Static instance for singleton
static PacketSniffer* s_instance = nullptr;

PacketSniffer& PacketSniffer::getInstance() {
    if (!s_instance) s_instance = new PacketSniffer();
    return *s_instance;
}

PacketSniffer::PacketSniffer()
    : m_state(SnifferState::STOPPED)
    , m_initialized(false)
    , m_hopEnabled(false)
    , m_hopInterval(200)
    , m_lastHopTime(0)
    , m_hopChannelIndex(0)
{
    s_instance = this;
}

PacketSniffer::~PacketSniffer() {
    stop();
    s_instance = nullptr;
}

bool PacketSniffer::init() {
    if (m_initialized) return true;
    
#ifdef ARDUINO
    // WiFi should already be initialized
    // We just need to ensure we're in station mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
#endif
    
    m_stats.reset();
    m_initialized = true;
    
    Serial.println("[Sniffer] Initialized");
    return true;
}

bool PacketSniffer::start(PacketCallback callback) {
    if (m_state == SnifferState::RUNNING) {
        return true;  // Already running
    }
    
    if (!m_initialized) {
        if (!init()) return false;
    }
    
    m_state = SnifferState::STARTING;
    m_callback = callback;
    m_stats.reset();
    
#ifdef ARDUINO
    // Disconnect from any network
    WiFi.disconnect();
    delay(10);
    
    // Set to station mode for promiscuous
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // Set initial channel
    uint8_t startChannel = m_filter.targetChannel > 0 ? m_filter.targetChannel : 1;
    wifi_utils::setChannel(startChannel);
    m_stats.currentChannel = startChannel;
    
    // Enable promiscuous mode with all frames filter
    auto filter = wifi_utils::allFrameFilter();
    wifi_utils::enablePromiscuous(promiscuousCallback, &filter);
#endif
    
    m_state = SnifferState::RUNNING;
    m_lastHopTime = millis();
    
    Serial.printf("[Sniffer] Started on channel %d\n", m_stats.currentChannel);
    return true;
}

void PacketSniffer::stop() {
    if (m_state != SnifferState::RUNNING) return;
    
    m_state = SnifferState::STOPPING;
    
#ifdef ARDUINO
    wifi_utils::disablePromiscuous();
#endif
    
    m_state = SnifferState::STOPPED;
    m_callback = nullptr;
    
    Serial.printf("[Sniffer] Stopped. Captured %lu packets\n", m_stats.totalPackets);
}

void PacketSniffer::update() {
    if (m_state != SnifferState::RUNNING) return;
    
    // Handle channel hopping
    if (m_hopEnabled && m_filter.targetChannel == 0) {
        uint32_t now = millis();
        if (now - m_lastHopTime >= m_hopInterval) {
            hopChannel();
            m_lastHopTime = now;
        }
    }
}

bool PacketSniffer::setChannel(uint8_t channel) {
    if (channel < 1 || channel > 14) return false;
    
#ifdef ARDUINO
    if (!wifi_utils::setChannel(channel)) {
        Serial.printf("[Sniffer] Failed to set channel %d\n", channel);
        return false;
    }
#endif
    
    m_stats.currentChannel = channel;
    return true;
}

void PacketSniffer::setChannelHopping(bool enable, uint32_t intervalMs) {
    m_hopEnabled = enable;
    m_hopInterval = intervalMs;
    m_hopChannelIndex = 0;
    
    if (enable) {
        Serial.printf("[Sniffer] Channel hopping enabled (%lu ms)\n", intervalMs);
    } else {
        Serial.println("[Sniffer] Channel hopping disabled");
    }
}

void PacketSniffer::targetBSSID(const uint8_t* bssid) {
    m_filter.setBSSIDFilter(bssid);
    Serial.printf("[Sniffer] Targeting BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

void PacketSniffer::clearTarget() {
    m_filter.clearBSSIDFilter();
    Serial.println("[Sniffer] BSSID filter cleared");
}

void PacketSniffer::hopChannel() {
    m_hopChannelIndex = (m_hopChannelIndex + 1) % HOP_CHANNEL_COUNT;
    uint8_t nextChannel = HOP_CHANNELS[m_hopChannelIndex];
    setChannel(nextChannel);
}

// Static callback for ESP32 promiscuous mode
#ifdef ARDUINO
void PacketSniffer::promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (s_instance && s_instance->m_state == SnifferState::RUNNING) {
        s_instance->processPacket(buf, static_cast<uint16_t>(type));
    }
}
#else
void PacketSniffer::promiscuousCallback(void* buf, uint16_t type) {
    if (s_instance && s_instance->m_state == SnifferState::RUNNING) {
        s_instance->processPacket(buf, type);
    }
}
#endif

void PacketSniffer::processPacket([[maybe_unused]] void* buf, [[maybe_unused]] uint16_t type) {
#ifdef ARDUINO
    // ESP32 provides wifi_promiscuous_pkt_t structure
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    wifi_pkt_rx_ctrl_t& rx_ctrl = pkt->rx_ctrl;
    
    // Validate packet length - ESP32 can report garbage
    if (rx_ctrl.sig_len < 24 || rx_ctrl.sig_len > 2500) {
        return;  // Invalid length
    }
    
    // Use ESP32's frame type classification
    // WIFI_PKT_MGMT = 0, WIFI_PKT_CTRL = 1, WIFI_PKT_DATA = 2, WIFI_PKT_MISC = 3
    wifi_promiscuous_pkt_type_t esp_type = static_cast<wifi_promiscuous_pkt_type_t>(type);
    
    // Create packet structure
    CapturedPacket packet;
    packet.data = pkt->payload;
    packet.length = rx_ctrl.sig_len;
    packet.rssi = rx_ctrl.rssi;
    packet.channel = rx_ctrl.channel;
    packet.timestamp = micros();
    
    // Parse 802.11 header
    if (!parseHeader(pkt->payload, packet.length, packet, esp_type)) {
        return;  // Invalid header
    }
    
    // Check if packet passes our filter
    if (!passesFilter(packet)) {
        return;
    }
    
    // Update statistics
    m_stats.totalPackets++;
    
    switch (packet.frameType) {
        case FrameType::MANAGEMENT:
            m_stats.managementFrames++;
            if (packet.isBeacon()) m_stats.beacons++;
            if (packet.isProbeRequest()) m_stats.probeRequests++;
            if (packet.isProbeResponse()) m_stats.probeResponses++;
            if (packet.isDeauth()) m_stats.deauthFrames++;
            break;
        case FrameType::CONTROL:
            m_stats.controlFrames++;
            break;
        case FrameType::DATA:
            m_stats.dataFrames++;
            if (packet.isEAPOL()) {
                m_stats.eapolFrames++;
                // Queue EAPOL_CAPTURED event (ISR-safe)
                EventData eapolEvt(EventType::EAPOL_CAPTURED);
                memcpy(eapolEvt.payload.handshake.bssid, packet.addr3, 6);
                eapolEvt.payload.handshake.channel = packet.channel;
                eapolEvt.payload.handshake.type = 2;  // EAPOL type
                EventBus::getInstance().queue(eapolEvt);
            }
            break;
        default:
            break;
    }
    
    // Internal callback for screens
    if (m_callback) {
        m_callback(packet);
    }
    
    // Queue PACKET_CAPTURED event (ISR-safe, lightweight)
    EventData pktEvt(EventType::PACKET_CAPTURED);
    pktEvt.payload.packet.count = m_stats.totalPackets;
    pktEvt.payload.packet.rate = static_cast<uint32_t>(m_stats.getPacketsPerSecond());
    pktEvt.payload.packet.type = static_cast<uint8_t>(packet.frameType);
    EventBus::getInstance().queue(pktEvt);
#endif
}

bool PacketSniffer::parseHeader(const uint8_t* data, uint16_t len, CapturedPacket& packet, 
                                 [[maybe_unused]] uint16_t espType) {
    if (len < 24) return false;  // Minimum 802.11 header size
    
    // Frame Control field (2 bytes) - little endian
    uint16_t frameControl = data[0] | (data[1] << 8);
    
    // Protocol version must be 0 (bits 0-1)
    if ((frameControl & 0x03) != 0) {
        return false;  // Invalid protocol version
    }
    
    // Extract frame type (bits 2-3) and subtype (bits 4-7)
    uint8_t type = (frameControl >> 2) & 0x03;
    uint8_t subtype = (frameControl >> 4) & 0x0F;
    
#ifdef ARDUINO
    // Validate against ESP32's classification
    // WIFI_PKT_MGMT = 0, WIFI_PKT_CTRL = 1, WIFI_PKT_DATA = 2, WIFI_PKT_MISC = 3
    wifi_promiscuous_pkt_type_t pktType = static_cast<wifi_promiscuous_pkt_type_t>(espType);
    
    bool typeMatch = false;
    switch (pktType) {
        case WIFI_PKT_MGMT:
            typeMatch = (type == 0);  // Management
            break;
        case WIFI_PKT_CTRL:
            typeMatch = (type == 1);  // Control
            break;
        case WIFI_PKT_DATA:
            typeMatch = (type == 2);  // Data
            break;
        case WIFI_PKT_MISC:
            // MISC packets are often corrupted or non-802.11 frames
            // Reject them entirely to avoid false positives
            return false;
    }
    
    if (!typeMatch) {
        return false;  // ESP32 says one type, frame control says another - corrupted
    }
#endif
    
    packet.frameType = static_cast<FrameType>(type);
    packet.subtype = subtype;
    
    // For management frames, do additional validation
    if (type == 0) {  // Management frame
        // Deauth frames (subtype 12) and Disassoc frames (subtype 10)
        if (subtype == 12 || subtype == 10) {
            // Size check: should be 26-30 bytes (24 header + 2 reason + optional elements)
            if (len < 26 || len > 100) {
                return false;  // Suspiciously sized deauth/disassoc
            }
            
            // Frame Control validation for deauth/disassoc:
            // - To DS and From DS should typically be 0 for these frames
            // - Protected bit (bit 14) should typically be 0 (unencrypted)
            uint8_t toDs = (frameControl >> 8) & 0x01;
            uint8_t fromDs = (frameControl >> 9) & 0x01;
            
            // Deauth/disassoc shouldn't have both To DS and From DS set
            if (toDs && fromDs) {
                return false;  // Invalid for deauth/disassoc
            }
        }
        
        // Additional validation: management frames should not have Protected bit set
        // (except for Action frames in some cases)
        bool protectedBit = (frameControl >> 14) & 0x01;
        if (protectedBit && subtype != 13 && subtype != 14) {  // Allow for Action frames
            return false;  // Management frames shouldn't be encrypted
        }
    }
    
    // Extract addresses
    // Address 1: bytes 4-9 (receiver)
    memcpy(packet.addr1, data + 4, 6);
    
    // Address 2: bytes 10-15 (transmitter)
    memcpy(packet.addr2, data + 10, 6);
    
    // Address 3: bytes 16-21 (BSSID or other)
    memcpy(packet.addr3, data + 16, 6);
    
    return true;
}

bool PacketSniffer::passesFilter(const CapturedPacket& packet) {
    // Frame type filters
    switch (packet.frameType) {
        case FrameType::MANAGEMENT:
            if (!m_filter.captureManagement) return false;
            if (packet.isBeacon() && !m_filter.captureBeacons) return false;
            if ((packet.isProbeRequest() || packet.isProbeResponse()) && 
                !m_filter.captureProbes) return false;
            if (packet.isDeauth() && !m_filter.captureDeauth) return false;
            break;
            
        case FrameType::CONTROL:
            if (!m_filter.captureControl) return false;
            break;
            
        case FrameType::DATA:
            if (!m_filter.captureData) return false;
            if (packet.isEAPOL() && !m_filter.captureEAPOL) return false;
            break;
            
        default:
            return false;
    }
    
    // BSSID filter
    if (m_filter.hasBSSIDFilter()) {
        bool matches = false;
        
        // Check if any address matches the target BSSID
        if (memcmp(packet.addr1, m_filter.targetBSSID, 6) == 0 ||
            memcmp(packet.addr2, m_filter.targetBSSID, 6) == 0 ||
            memcmp(packet.addr3, m_filter.targetBSSID, 6) == 0) {
            matches = true;
        }
        
        if (!matches) return false;
    }
    
    return true;
}

// Channel hop order (optimized to avoid adjacent channels)
constexpr uint8_t PacketSniffer::HOP_CHANNELS[];

} // namespace adversary
