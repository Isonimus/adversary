/**
 * @file sniffer_screen.cpp
 * @brief Packet Sniffer UI screen implementation
 */

#include "sniffer_screen.h"
#include "config/config.h"
#include <cstring>

namespace adversary {

SnifferScreen::SnifferScreen()
    : m_sniffer(PacketSniffer::getInstance())
    , m_pcapWriter()
    , m_captureMode(CaptureMode::ALL_PACKETS)
    , m_hasTarget(false)
    , m_targetChannel(1)
    , m_active(false)
    , m_shouldExit(false)
    , m_capturing(false)
    , m_saveToFile(true)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
#ifdef ESP32
    , m_canvas(nullptr)
    , m_canvasInitialized(false)
#endif
    , m_screenState(SnifferScreenState::STATS)
    , m_packetLogHead(0)
    , m_packetLogSelection(0)
    , m_packetLogScroll(0)
    , m_logPaused(false)
    , m_actionMenuSelection(0)
    , m_actionCount(0)
    , m_deauthCount(0)
    , m_deauthWindowStart(0)
    , m_deauthFloodAlert(false)
{
    memset(m_targetSsid, 0, sizeof(m_targetSsid));
    memset(m_targetBssid, 0, sizeof(m_targetBssid));
    m_stats.reset();
    m_packetLog.reserve(PACKET_LOG_SIZE);
    memset(m_availableActions, 0, sizeof(m_availableActions));
}

SnifferScreen::~SnifferScreen()
{
    stopCapture();
    
    if (m_canvas) {
        m_canvas->deleteSprite();
        delete m_canvas;
        m_canvas = nullptr;
    }
}

void SnifferScreen::init()
{
    m_stats.reset();
    m_needsRedraw = true;
    m_lastUpdate = 0;  // Force redraw on next render
    
    // Delete old canvas to force recreation with fresh display reference
    if (m_canvas) {
        m_canvas->deleteSprite();
        delete m_canvas;
        m_canvas = nullptr;
    }
    m_canvasInitialized = false;
}

void SnifferScreen::update()
{
    if (!m_active) return;
    
    // Update sniffer (for channel hopping etc)
    m_sniffer.update();
    
    // Process queued packets - write to SD in main loop context
    if (m_capturing && m_saveToFile) {
        processPacketQueue();
    }
    
    // Check for channel changes
    if (m_capturing) {
        m_stats.currentChannel = m_sniffer.getChannel();
    }
    
    // Force redraw periodically when capturing
    if (m_capturing && (millis() - m_lastUpdate) > REDRAW_INTERVAL_MS) {
        m_needsRedraw = true;
    }
}

void SnifferScreen::processPacketQueue()
{
    // Process up to 10 packets per update to avoid blocking
    int processed = 0;
    while (!m_packetQueue.empty() && processed < 10) {
        const QueuedPacket& pkt = m_packetQueue.front();
        
        if (m_pcapWriter.isOpen()) {
            m_pcapWriter.writePacket(pkt.data, pkt.length, pkt.timestamp);
            m_stats.bytesWritten = m_pcapWriter.getBytesWritten();
        }
        
        m_packetQueue.pop();
        processed++;
    }
    
    // Periodic flush every 100 packets
    if (m_pcapWriter.isOpen() && m_pcapWriter.getPacketCount() % 100 == 0 && m_pcapWriter.getPacketCount() > 0) {
        m_pcapWriter.flush();
    }
}

void SnifferScreen::setActive(bool active)
{
    m_active = active;
    m_needsRedraw = true;
    
    if (!active && m_capturing) {
        stopCapture();
    }
}

void SnifferScreen::show()
{
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    m_screenState = SnifferScreenState::STATS;
    
    // Subscribe to EventBus for packet events
    if (packetHandlerId_ == 0) {
        packetHandlerId_ = EventBus::getInstance().subscribe(
            EventType::PACKET_CAPTURED,
            [this](const EventData& evt) {
                // Packet captured notification via EventBus
                m_needsRedraw = true;
            }
        );
    }
    
    if (eapolHandlerId_ == 0) {
        eapolHandlerId_ = EventBus::getInstance().subscribe(
            EventType::EAPOL_CAPTURED,
            [this](const EventData& evt) {
                // EAPOL frame captured - trigger redraw for handshake tracking
                m_needsRedraw = true;
            }
        );
    }
    
    footerHints_.setHints({}); // Will be set dynamically in render
    footerHints_.setFocus(false);
}

void SnifferScreen::hide()
{
    m_active = false;
    if (m_capturing) {
        stopCapture();
    }
    
    // Unsubscribe from EventBus
    if (packetHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(packetHandlerId_);
        packetHandlerId_ = 0;
    }
    if (eapolHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(eapolHandlerId_);
        eapolHandlerId_ = 0;
    }
    
    // CRITICAL: Release WiFi driver memory (~30KB)
    // This effectively resets the heap for the next screen (e.g. Menu)
    WiFi.mode(WIFI_OFF);
    delay(50);
}

void CaptureStats::reset() {
    totalPackets = 0;
    beacons = 0;
    probeRequests = 0;
    probeResponses = 0;
    dataFrames = 0;
    eapolFrames = 0;
    deauthFrames = 0;
    bytesWritten = 0;
    currentChannel = 1;
}

PacketSummary::PacketSummary()
    : timestamp(0)
    , frameType(PacketFrameType::BEACON)
    , eapolMsg(EapolMsgType::UNKNOWN)
    , rssi(-100)
    , channel(0)
    , flags(0)
{
    memset(srcMac, 0, 6);
    memset(dstMac, 0, 6);
    memset(ssid, 0, sizeof(ssid));
}

bool SnifferScreen::handleInput(char key)
{
    m_needsRedraw = true;

    // Footer hints: focus navigation + action dispatch
    {
        char footerAction = 0;
        if (footerHints_.handleInputWithDispatch(key, footerAction)) {
            if (footerAction) return handleInput(footerAction);  // Dispatch action
            return true;  // Consumed (navigation/toggle)
        }
    }

    switch (key) {
        case '\n':  // Enter - context-dependent action
        case '\r':
            if (m_screenState == SnifferScreenState::ACTION_MENU) {
                // Execute selected action
                executeAction(m_availableActions[m_actionMenuSelection]);
                m_screenState = SnifferScreenState::PACKET_LOG;
                m_needsRedraw = true;
            } else if (m_screenState == SnifferScreenState::PACKET_LOG && !m_packetLog.empty()) {
                // Open action menu for selected packet
                buildActionList(m_packetLog[m_packetLogSelection]);
                m_actionMenuSelection = 0;
                m_screenState = SnifferScreenState::ACTION_MENU;
                m_needsRedraw = true;
            } else {
                // Default: toggle capture
                toggleCapture();
            }
            return true;
            
        case '`':   // Back/escape
        case 27:    // ESC
            if (m_screenState == SnifferScreenState::ACTION_MENU) {
                m_screenState = SnifferScreenState::PACKET_LOG;
                m_needsRedraw = true;
                return true;
            }
            // Exit to menu from top-level states
            m_shouldExit = true;
            return true;
            
        case 'm':   // Mode - cycle capture mode
        case 'M':
            if (m_screenState == SnifferScreenState::STATS) {
                cycleCaptureMode();
            }
            return true;
            
        case 'l':   // Log - toggle stats/packet log view
        case 'L':
            m_screenState = (m_screenState == SnifferScreenState::STATS) 
                          ? SnifferScreenState::PACKET_LOG 
                          : SnifferScreenState::STATS;
            m_needsRedraw = true;
            return true;
            
        case 'p':   // Pause - toggle log pause
        case 'P':
            if (m_screenState == SnifferScreenState::PACKET_LOG) {
                m_logPaused = !m_logPaused;
                m_needsRedraw = true;
            }
            return true;
            
        case ';':   // Up - navigate packet log or action menu
            if (m_screenState == SnifferScreenState::ACTION_MENU) {
                if (m_actionMenuSelection > 0) {
                    m_actionMenuSelection--;
                    m_needsRedraw = true;
                }
            } else if (m_screenState == SnifferScreenState::PACKET_LOG && m_packetLogSelection > 0) {
                m_packetLogSelection--;
                if (m_packetLogSelection < m_packetLogScroll) {
                    m_packetLogScroll = m_packetLogSelection;
                }
                m_needsRedraw = true;
            }
            return true;
            
        case '.':   // Down - navigate packet log or action menu
            if (m_screenState == SnifferScreenState::ACTION_MENU) {
                if (m_actionMenuSelection < m_actionCount - 1) {
                    m_actionMenuSelection++;
                    m_needsRedraw = true;
                }
            } else if (m_screenState == SnifferScreenState::PACKET_LOG) {
                if (m_packetLogSelection < m_packetLog.size() - 1) {
                    m_packetLogSelection++;
                    if (m_packetLogSelection >= m_packetLogScroll + LOG_VISIBLE_ROWS) {
                        m_packetLogScroll = m_packetLogSelection - LOG_VISIBLE_ROWS + 1;
                    }
                    m_needsRedraw = true;
                }
            }
            return true;
            
        case 's':   // Save toggle
        case 'S':
            m_saveToFile = !m_saveToFile;
            m_needsRedraw = true;
            return true;
            
        case 'c':   // Clear stats
        case 'C':
            m_stats.reset();
            m_packetLog.clear();
            m_packetLogHead = 0;
            m_packetLogSelection = 0;
            m_packetLogScroll = 0;
            m_needsRedraw = true;
            return true;
            
        case '+':   // Channel up
            if (!m_hasTarget) {
                uint8_t ch = m_sniffer.getChannel();
                m_sniffer.setChannel(ch < 13 ? ch + 1 : 1);
                m_stats.currentChannel = m_sniffer.getChannel();
                m_needsRedraw = true;
            }
            return true;
            
        case '-':   // Channel down
            if (!m_hasTarget) {
                uint8_t ch = m_sniffer.getChannel();
                m_sniffer.setChannel(ch > 1 ? ch - 1 : 13);
                m_stats.currentChannel = m_sniffer.getChannel();
                m_needsRedraw = true;
            }
            return true;
            
        case 'h':   // Toggle channel hopping
        case 'H':
            if (!m_hasTarget) {
                if (m_sniffer.getState() == SnifferState::RUNNING) {
                    // Check if hopping is active, toggle it
                    m_sniffer.setChannelHopping(!m_sniffer.isChannelHopping(), 100);
                    m_needsRedraw = true;
                }
            }
            return true;
            
        default:
            return false;
    }
}

void SnifferScreen::setTargetNetwork(const char* ssid, const uint8_t* bssid, uint8_t channel)
{
    m_hasTarget = true;
    strncpy(m_targetSsid, ssid ? ssid : "", sizeof(m_targetSsid) - 1);
    memcpy(m_targetBssid, bssid, 6);
    m_targetChannel = channel;
    
    // Set BSSID filter on sniffer
    m_sniffer.targetBSSID(bssid);
    
    // Set fixed channel
    m_sniffer.setChannel(channel);
    m_stats.currentChannel = channel;
    
    m_needsRedraw = true;
}

void SnifferScreen::clearTargetNetwork()
{
    m_hasTarget = false;
    memset(m_targetSsid, 0, sizeof(m_targetSsid));
    memset(m_targetBssid, 0, sizeof(m_targetBssid));
    
    // Clear filter
    m_sniffer.clearTarget();
    
    // Enable channel hopping
    m_sniffer.setChannelHopping(true, 100);
    
    m_needsRedraw = true;
}

void SnifferScreen::startCapture()
{
    if (m_capturing) return;
    
    // Open PCAP file if saving
    if (m_saveToFile) {
        std::string filename;
        
        if (m_hasTarget) {
            filename = PcapWriter::generateHandshakeFilename(
                m_targetSsid, m_targetBssid, config::SD_HANDSHAKES_PATH);
        } else {
            filename = PcapWriter::generateFilename(
                "capture", config::SD_PACKETS_PATH);
        }
        
        if (!m_pcapWriter.open(filename.c_str())) {
            Serial.println("[SnifferScreen] Failed to open PCAP file");
            // Continue without saving
        }
    }
    
    // Configure filter based on mode
    configureFilter();
    
    // Start sniffer with callback
    auto callback = [this](const CapturedPacket& packet) {
        this->onPacketReceived(packet);
    };
    
    if (!m_sniffer.start(callback)) {
        Serial.println("[SnifferScreen] Failed to start sniffer");
        m_pcapWriter.close();
        return;
    }
    
    m_capturing = true;
    m_stats.currentChannel = m_sniffer.getChannel();
    m_needsRedraw = true;
    
    Serial.println("[SnifferScreen] Capture started");
}

void SnifferScreen::stopCapture()
{
    if (!m_capturing) return;
    
    // Stop sniffer
    m_sniffer.stop();
    
    // Process any remaining queued packets
    while (!m_packetQueue.empty()) {
        const QueuedPacket& pkt = m_packetQueue.front();
        if (m_pcapWriter.isOpen()) {
            m_pcapWriter.writePacket(pkt.data, pkt.length, pkt.timestamp);
        }
        m_packetQueue.pop();
    }
    
    // Close PCAP file
    if (m_pcapWriter.isOpen()) {
        m_pcapWriter.flush();
        m_pcapWriter.close();
    }
    
    m_capturing = false;
    m_needsRedraw = true;
    
    Serial.println("[SnifferScreen] Capture stopped");
}

void SnifferScreen::toggleCapture()
{
    if (m_capturing) {
        stopCapture();
    } else {
        startCapture();
    }
}

void SnifferScreen::setCaptureMode(CaptureMode mode)
{
    m_captureMode = mode;
    configureFilter();
    m_needsRedraw = true;
}

void SnifferScreen::cycleCaptureMode()
{
    switch (m_captureMode) {
        case CaptureMode::ALL_PACKETS:
            m_captureMode = CaptureMode::BEACONS_ONLY;
            break;
        case CaptureMode::BEACONS_ONLY:
            m_captureMode = CaptureMode::PROBE_REQUESTS;
            break;
        case CaptureMode::PROBE_REQUESTS:
            m_captureMode = CaptureMode::DATA_ONLY;
            break;
        case CaptureMode::DATA_ONLY:
            m_captureMode = CaptureMode::EAPOL_ONLY;
            break;
        case CaptureMode::EAPOL_ONLY:
            m_captureMode = CaptureMode::DEAUTH_ONLY;
            break;
        case CaptureMode::DEAUTH_ONLY:
            m_captureMode = CaptureMode::ALL_PACKETS;
            break;
    }
    
    configureFilter();
    m_needsRedraw = true;
}

const char* SnifferScreen::getModeName() const
{
    switch (m_captureMode) {
        case CaptureMode::ALL_PACKETS:     return "ALL";
        case CaptureMode::BEACONS_ONLY:    return "BEACON";
        case CaptureMode::PROBE_REQUESTS:  return "PROBE";
        case CaptureMode::DATA_ONLY:       return "DATA";
        case CaptureMode::EAPOL_ONLY:      return "EAPOL";
        case CaptureMode::DEAUTH_ONLY:     return "DEAUTH";
        default:                           return "???";
    }
}

void SnifferScreen::configureFilter()
{
    // Get and modify filter based on mode
    SnifferFilter filter = m_sniffer.getFilter();
    
    switch (m_captureMode) {
        case CaptureMode::ALL_PACKETS:
            filter.captureManagement = true;
            filter.captureControl = true;
            filter.captureData = true;
            filter.captureBeacons = true;
            filter.captureProbes = true;
            filter.captureEAPOL = true;
            filter.captureDeauth = true;
            break;
            
        case CaptureMode::BEACONS_ONLY:
            filter.captureManagement = true;
            filter.captureControl = false;
            filter.captureData = false;
            filter.captureBeacons = true;
            filter.captureProbes = false;
            filter.captureEAPOL = false;
            filter.captureDeauth = false;
            break;
            
        case CaptureMode::PROBE_REQUESTS:
            filter.captureManagement = true;
            filter.captureControl = false;
            filter.captureData = false;
            filter.captureBeacons = false;
            filter.captureProbes = true;
            filter.captureEAPOL = false;
            filter.captureDeauth = false;
            break;
            
        case CaptureMode::DATA_ONLY:
            filter.captureManagement = false;
            filter.captureControl = false;
            filter.captureData = true;
            filter.captureBeacons = false;
            filter.captureProbes = false;
            filter.captureEAPOL = true;
            filter.captureDeauth = false;
            break;
            
        case CaptureMode::EAPOL_ONLY:
            filter.captureManagement = false;
            filter.captureControl = false;
            filter.captureData = true;  // EAPOL is in data frames
            filter.captureBeacons = false;
            filter.captureProbes = false;
            filter.captureEAPOL = true;
            filter.captureDeauth = false;
            break;
            
        case CaptureMode::DEAUTH_ONLY:
            filter.captureManagement = true;
            filter.captureControl = false;
            filter.captureData = false;
            filter.captureBeacons = false;
            filter.captureProbes = false;
            filter.captureEAPOL = false;
            filter.captureDeauth = true;
            break;
    }
    
    m_sniffer.setFilter(filter);
}

bool SnifferScreen::shouldCapturePacket(const CapturedPacket& packet) const
{
    switch (m_captureMode) {
        case CaptureMode::ALL_PACKETS:
            return true;
            
        case CaptureMode::BEACONS_ONLY:
            return packet.isBeacon();
            
        case CaptureMode::PROBE_REQUESTS:
            return packet.isProbeRequest();
            
        case CaptureMode::DATA_ONLY:
            return (packet.frameType == FrameType::DATA);
            
        case CaptureMode::EAPOL_ONLY:
            return packet.isEAPOL();
            
        case CaptureMode::DEAUTH_ONLY:
            return packet.isDeauth();
            
        default:
            return true;
    }
}

void SnifferScreen::onPacketReceived(const CapturedPacket& packet)
{
    // Update statistics
    m_stats.totalPackets++;
    
    // Count by frame type using helper methods
    if (packet.isBeacon()) {
        m_stats.beacons++;
    } else if (packet.isProbeRequest()) {
        m_stats.probeRequests++;
    } else if (packet.isProbeResponse()) {
        m_stats.probeResponses++;
    } else if (packet.isDeauth()) {
        m_stats.deauthFrames++;
    }
    
    if (packet.frameType == FrameType::DATA) {
        m_stats.dataFrames++;
        
        // Check for EAPOL
        if (packet.isEAPOL()) {
            m_stats.eapolFrames++;
            
            // Notify if handshake callback set
            if (m_onHandshakeCaptured && m_pcapWriter.isOpen()) {
                m_onHandshakeCaptured(m_pcapWriter.getFilename().c_str());
            }
        }
    }
    
    // Add to packet log (if not paused)
    if (!m_logPaused && shouldCapturePacket(packet)) {
        addPacketToLog(packet);
    }
    
    // Queue packet for deferred SD write (avoid SPI bus contention in callback)
    if (m_saveToFile && shouldCapturePacket(packet)) {
        // Only queue if we have space (drop packets if queue is full)
        if (m_packetQueue.size() < MAX_QUEUE_SIZE) {
            QueuedPacket qpkt;
            qpkt.length = (packet.length > sizeof(qpkt.data)) ? sizeof(qpkt.data) : packet.length;
            memcpy(qpkt.data, packet.data, qpkt.length);
            qpkt.timestamp = packet.timestamp;
            m_packetQueue.push(qpkt);
        }
        // If queue is full, packet is dropped (better than blocking/crashing)
    }
    
    m_needsRedraw = true;
}

void SnifferScreen::addPacketToLog(const CapturedPacket& packet)
{
    PacketSummary summary;
    summary.timestamp = packet.timestamp;
    summary.rssi = packet.rssi;
    summary.channel = m_stats.currentChannel;
    
    // Copy MACs
    if (packet.length >= 22) {
        // Addresses in 802.11 header: Addr1 at +4, Addr2 at +10, Addr3 at +16
        memcpy(summary.srcMac, packet.data + 10, 6);  // Addr2 = source
        memcpy(summary.dstMac, packet.data + 4, 6);   // Addr1 = destination
    }
    
    // Determine frame type
    if (packet.isBeacon()) {
        summary.frameType = PacketFrameType::BEACON;
        // Extract SSID from beacon
        if (packet.length > 38) {
            const uint8_t* ie = packet.data + 36;  // After fixed fields
            size_t remaining = packet.length - 36;
            while (remaining >= 2) {
                uint8_t id = ie[0];
                uint8_t len = ie[1];
                if (id == 0 && len > 0 && len < sizeof(summary.ssid)) {  // SSID element
                    memcpy(summary.ssid, ie + 2, len);
                    summary.ssid[len] = '\0';
                    break;
                }
                // Check for WPS IE (ID 221, OUI 00:50:f2:04)
                if (id == 221 && len >= 4 && remaining >= len + 2) {
                    if (ie[2] == 0x00 && ie[3] == 0x50 && ie[4] == 0xF2 && ie[5] == 0x04) {
                        summary.flags |= 0x01;  // WPS flag
                    }
                }
                
                // Safety check to prevent integer underflow/infinite loop
                if (remaining < 2 + len) break;
                
                ie += 2 + len;
                remaining -= 2 + len;
            }
        }
    } else if (packet.isProbeRequest()) {
        summary.frameType = PacketFrameType::PROBE_REQ;
        // Extract SSID from probe
        if (packet.length > 26) {
            const uint8_t* ie = packet.data + 24;
            if (ie[0] == 0 && ie[1] > 0 && ie[1] < sizeof(summary.ssid)) {
                memcpy(summary.ssid, ie + 2, ie[1]);
                summary.ssid[ie[1]] = '\0';
            }
        }
    } else if (packet.isProbeResponse()) {
        summary.frameType = PacketFrameType::PROBE_RESP;
    } else if (packet.isDeauth()) {
        summary.frameType = PacketFrameType::DEAUTH;
        
        // Deauth flood detection
        uint32_t now = millis();
        if (now - m_deauthWindowStart > DEAUTH_WINDOW_MS) {
            // Reset window
            m_deauthWindowStart = now;
            m_deauthCount = 0;
            m_deauthFloodAlert = false;
        }
        m_deauthCount++;
        if (m_deauthCount >= DEAUTH_FLOOD_THRESHOLD) {
            m_deauthFloodAlert = true;
            Serial.println("[Sniffer] ALERT: Deauth flood detected!");
        }
    } else if (packet.isEAPOL()) {
        summary.frameType = PacketFrameType::EAPOL;
        // Try to detect M1-M4 from key info (simplified)
        // Key info at offset ~77 in data frame after 802.11 + LLC
        // This is a simplified check
        summary.eapolMsg = EapolMsgType::UNKNOWN;
        if (packet.length >= 100) {
            // Very rough EAPOL M1-M4 detection based on key info flags
            // M1: has ANonce, no SNonce; M2: has SNonce, no ANonce...
            // For now just check if MIC is present
            const uint8_t* eapolStart = packet.data + 34; // Approximate
            if (packet.length > 80) {
                uint16_t keyInfo = (eapolStart[5] << 8) | eapolStart[6];
                bool hasMic = (keyInfo & 0x0100) != 0;
                bool isAck = (keyInfo & 0x0080) != 0;
                bool hasSecure = (keyInfo & 0x0200) != 0;
                
                if (isAck && !hasMic) {
                    summary.eapolMsg = hasSecure ? EapolMsgType::M3 : EapolMsgType::M1;
                } else if (hasMic && !isAck) {
                    summary.eapolMsg = hasSecure ? EapolMsgType::M4 : EapolMsgType::M2;
                }
            }
        }
    } else if (packet.frameType == FrameType::DATA) {
        summary.frameType = PacketFrameType::DATA;
    }
    
    // Add to ring buffer
    if (m_packetLog.size() < PACKET_LOG_SIZE) {
        m_packetLog.push_back(summary);
    } else {
        m_packetLog[m_packetLogHead] = summary;
        m_packetLogHead = (m_packetLogHead + 1) % PACKET_LOG_SIZE;
    }
    
    // Auto-scroll if at bottom and not paused
    if (!m_logPaused && m_packetLogSelection >= m_packetLog.size() - 2) {
        m_packetLogSelection = m_packetLog.size() - 1;
        if (m_packetLogSelection >= LOG_VISIBLE_ROWS) {
            m_packetLogScroll = m_packetLogSelection - LOG_VISIBLE_ROWS + 1;
        }
    }
}

uint16_t SnifferScreen::getFrameTypeColor(PacketFrameType type) const
{
    switch (type) {
        case PacketFrameType::BEACON:
            return theme::INFO();
        case PacketFrameType::PROBE_REQ:
        case PacketFrameType::PROBE_RESP:
            return theme::WARNING();
        case PacketFrameType::DATA:
            return theme::TEXT_PRIMARY();
        case PacketFrameType::EAPOL:
            return theme::SUCCESS();
        case PacketFrameType::DEAUTH:
            return theme::ERROR();
        default:
            return theme::TEXT_SECONDARY();
    }
}

const char* SnifferScreen::getFrameTypeName(PacketFrameType type) const
{
    switch (type) {
        case PacketFrameType::BEACON:    return "BCN";
        case PacketFrameType::PROBE_REQ: return "PRB";
        case PacketFrameType::PROBE_RESP:return "PRS";
        case PacketFrameType::DATA:      return "DAT";
        case PacketFrameType::EAPOL:     return "EAP";
        case PacketFrameType::DEAUTH:    return "DEA";
        default:                         return "???";
    }
}

void SnifferScreen::buildActionList(const PacketSummary& packet)
{
    m_actionCount = 0;
    
    switch (packet.frameType) {
        case PacketFrameType::BEACON:
        case PacketFrameType::PROBE_RESP:
            // Beacon/Probe Response - AP actions
            m_availableActions[m_actionCount++] = PacketAction::HANDSHAKE_CAPTURE;
            m_availableActions[m_actionCount++] = PacketAction::DEAUTH_ATTACK;
            m_availableActions[m_actionCount++] = PacketAction::EVIL_TWIN;
            break;
            
        case PacketFrameType::PROBE_REQ:
            // Probe Request - client actions
            m_availableActions[m_actionCount++] = PacketAction::KARMA_ATTACK;
            break;
            
        case PacketFrameType::EAPOL:
            // EAPOL - handshake actions
            m_availableActions[m_actionCount++] = PacketAction::HANDSHAKE_CAPTURE;
            break;
            
        case PacketFrameType::DEAUTH:
            // Deauth - detection info only for now
            break;
            
        default:
            break;
    }
    
    // Always add cancel
    m_availableActions[m_actionCount++] = PacketAction::CANCEL;
}

const char* SnifferScreen::getActionName(PacketAction action) const
{
    switch (action) {
        case PacketAction::HANDSHAKE_CAPTURE:  return "Handshake Capture";
        case PacketAction::DEAUTH_ATTACK:      return "Deauth Attack";
        case PacketAction::EVIL_TWIN:          return "Evil Twin";
        case PacketAction::KARMA_ATTACK:       return "Karma Attack";
        case PacketAction::COPY_BSSID:         return "Copy BSSID";
        case PacketAction::CANCEL:             return "Cancel";
        default:                               return "???";
    }
}

void SnifferScreen::executeAction(PacketAction action)
{
    if (m_packetLog.empty()) return;
    if (action == PacketAction::CANCEL) return;  // Just close menu
    
    const PacketSummary& pkt = m_packetLog[m_packetLogSelection];
    
    // Log action
    Serial.printf("[Sniffer] Executing action %s on %s\n", 
                 getActionName(action), pkt.ssid[0] ? pkt.ssid : "<hidden>");
    
    // Call callback to let main.cpp handle screen transition
    if (m_onPacketAction) {
        m_onPacketAction(pkt, action);
    } else {
        Serial.println("[Sniffer] No action callback registered!");
    }
}

// =============================================================================
// Rendering
// =============================================================================

void SnifferScreen::render(Canvas& canvas) {
    if (!m_active) return;
    
#ifdef ESP32
    uint32_t now = millis();
    if (!m_needsRedraw && (now - m_lastUpdate) < REDRAW_INTERVAL_MS) {
        return;
    }
    
    m_lastUpdate = now;
    m_needsRedraw = false;
    
    canvas.fillScreen(theme::BG_PRIMARY());
    
    drawHeader(canvas);
    
    switch (m_screenState) {
        case SnifferScreenState::STATS:
            drawStats(canvas);
            break;
        case SnifferScreenState::PACKET_LOG:
            drawPacketLog(canvas);
            break;
        case SnifferScreenState::ACTION_MENU:
            drawPacketLog(canvas);
            drawActionMenu(canvas);
            break;
    }
    
    // Set dynamic hints based on state
    if (m_screenState == SnifferScreenState::PACKET_LOG) {
        footerHints_.setHints({
            {'L', "Stats", true},
            {'P', m_logPaused ? "Resume" : "Pause", true},
            {'S', m_saveToFile ? "Save:ON" : "Save:OFF", true},
            {'C', "Clear", true}
        });
    } else {
        footerHints_.setHints({
            {'L', "Log", true},
            {'M', getModeName(), true},
            {'S', m_saveToFile ? "Save:ON" : "Save:OFF", true},
            {'C', "Clear", true}
        });
    }
    
    // Add [REC] indicator to right content
    static char recBuf[16];
    snprintf(recBuf, sizeof(recBuf), "%s", m_capturing ? "[REC]" : "[STOP]");
    footerHints_.setRightContent(recBuf);
    
    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawHeader(Canvas& canvas) {
#ifdef ESP32
    const char* title = m_hasTarget ? m_targetSsid : "Packet Sniffer";
    const char* modeName = getModeName();
    
    ui::StatusBar::render(canvas, title, modeName);
    
    canvas.drawLine(0, ui::STATUS_BAR_HEIGHT - 1, canvas.width(), ui::STATUS_BAR_HEIGHT - 1, 
                     theme::BG_TERTIARY());
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawStats(Canvas& canvas) {
#ifdef ESP32
    int16_t y = HEADER_HEIGHT + 4;
    int16_t midX = canvas.width() / 2;
    
    canvas.setTextSize(1);
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Packets: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(m_stats.totalPackets);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Beacons: ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(m_stats.beacons);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("Probes:  ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(m_stats.probeRequests);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(4, y);
    canvas.print("EAPOL:   ");
    canvas.setTextColor(m_stats.eapolFrames > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    canvas.print(m_stats.eapolFrames);
    
    // Right column
    y = HEADER_HEIGHT + 4;
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(midX + 4, y);
    canvas.print("Data:   ");
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.print(m_stats.dataFrames);
    
    y += 12;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(midX + 4, y);
    canvas.print("Deauth: ");
    canvas.setTextColor(m_stats.deauthFrames > 0 ? theme::WARNING() : theme::TEXT_PRIMARY());
    canvas.print(m_stats.deauthFrames);
    
    y += 12;
    if (m_saveToFile && (m_pcapWriter.isOpen() || m_pcapWriter.hasWrittenData())) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(midX + 4, y);
        canvas.print("File:   ");
        
        canvas.setTextColor(m_pcapWriter.hasError() ? theme::ERROR() : theme::TEXT_PRIMARY());
        uint32_t bytes = m_pcapWriter.getBytesWritten();
        if (bytes < 1024) {
            canvas.printf("%luB", (unsigned long)bytes);
        } else if (bytes < 1024 * 1024) {
            canvas.printf("%.1fKB", bytes / 1024.0f);
        } else {
            canvas.printf("%.1fMB", bytes / (1024.0f * 1024.0f));
        }
        if (m_pcapWriter.hasError()) canvas.print("!");
    }
    
    y += 12;
    drawChannelIndicator(canvas, midX + 4, y);
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawChannelIndicator(Canvas& canvas, int16_t x, int16_t y) {
#ifdef ESP32
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Ch: ");
    
    canvas.setTextColor(theme::ACCENT());
    canvas.printf("%2d", m_stats.currentChannel);
    
    static uint32_t lastPacketTime = 0;
    static bool blink = false;
    
    if (m_capturing) {
        uint32_t now = millis();
        if (now - lastPacketTime < 100) {
            blink = !blink;
            if (blink) {
                canvas.fillCircle(x + 40, y + 3, 3, theme::SUCCESS());
            }
        }
        lastPacketTime = now;
    }
#else
    (void)canvas;
    (void)x;
    (void)y;
#endif
}



void SnifferScreen::drawPacketLog(Canvas& canvas) {
#ifdef ESP32
    int16_t startY = HEADER_HEIGHT + 2;
    
    if (m_deauthFloodAlert) {
        canvas.fillRect(0, startY, canvas.width(), 14, theme::ERROR());
        canvas.setTextColor(theme::BG_PRIMARY());
        canvas.setCursor(canvas.width() / 2 - 55, startY + 3);
        canvas.print("! DEAUTH FLOOD DETECTED !");
        startY += 16;
    }
    
    if (m_logPaused) {
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(canvas.width() / 2 - 25, startY);
        canvas.print("[PAUSED]");
        startY += 12;
    }
    
    size_t logSize = m_packetLog.size();
    if (logSize == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(canvas.width() / 2 - 40, canvas.height() / 2);
        canvas.print("No packets yet");
        return;
    }
    
    size_t startIdx = m_packetLogScroll;
    size_t endIdx = startIdx + LOG_VISIBLE_ROWS;
    if (endIdx > logSize) endIdx = logSize;
    
    int16_t y = startY;
    for (size_t i = startIdx; i < endIdx; i++) {
        bool selected = (i == m_packetLogSelection);
        drawPacketLogEntry(canvas, m_packetLog[i], y, selected);
        y += LOG_ROW_HEIGHT;
    }
    
    if (logSize > LOG_VISIBLE_ROWS) {
        drawLogScrollbar(canvas);
    }
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(canvas.width() - 50, canvas.height() - STATUS_HEIGHT - 12);
    canvas.printf("[%u/%u]", (unsigned)(m_packetLogSelection + 1), (unsigned)logSize);
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawPacketLogEntry(Canvas& canvas, const PacketSummary& pkt, 
                                        int16_t y, bool selected) {
#ifdef ESP32
    int16_t screenWidth = canvas.width();
    
    if (selected) {
        canvas.fillRect(0, y, screenWidth, LOG_ROW_HEIGHT - 1, theme::BG_SELECTED());
    }
    
    canvas.setTextSize(1);
    
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(2, y + 3);
    uint32_t secs = (pkt.timestamp / 1000) % 100;
    canvas.printf("%02u", (unsigned)secs);
    
    canvas.setTextColor(getFrameTypeColor(pkt.frameType));
    canvas.setCursor(18, y + 3);
    canvas.print(getFrameTypeName(pkt.frameType));
    
    if (pkt.frameType == PacketFrameType::EAPOL && pkt.eapolMsg != EapolMsgType::UNKNOWN) {
        canvas.setTextColor(theme::SUCCESS());
        canvas.printf("M%d", static_cast<int>(pkt.eapolMsg));
    }
    
    canvas.setTextColor(selected ? theme::TEXT_PRIMARY() : theme::TEXT_SECONDARY());
    canvas.setCursor(50, y + 3);
    char ssidTrunc[11];
    strncpy(ssidTrunc, pkt.ssid[0] ? pkt.ssid : "<hidden>", 10);
    ssidTrunc[10] = '\0';
    canvas.print(ssidTrunc);
    
    if (pkt.flags & 0x01) {
        canvas.setTextColor(theme::ACCENT());
        canvas.print("[W]");
    }
    
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(screenWidth - 50, y + 3);
    canvas.printf("%02X%02X", pkt.srcMac[4], pkt.srcMac[5]);
    
    int8_t rssi = pkt.rssi;
    if (rssi > -50) {
        canvas.setTextColor(theme::SUCCESS());
    } else if (rssi > -70) {
        canvas.setTextColor(theme::WARNING());
    } else {
        canvas.setTextColor(theme::ERROR());
    }
    canvas.setCursor(screenWidth - 22, y + 3);
    canvas.printf("%d", rssi);
#else
    (void)canvas;
    (void)pkt;
    (void)y;
    (void)selected;
#endif
}

void SnifferScreen::drawLogScrollbar(Canvas& canvas) {
#ifdef ESP32
    int16_t barX = canvas.width() - 4;
    int16_t barY = HEADER_HEIGHT + 2;
    int16_t barHeight = canvas.height() - HEADER_HEIGHT - STATUS_HEIGHT - 4;
    
    canvas.drawRect(barX, barY, 3, barHeight, theme::BG_TERTIARY());
    
    size_t logSize = m_packetLog.size();
    if (logSize > 0) {
        int16_t thumbHeight = (LOG_VISIBLE_ROWS * barHeight) / logSize;
        if (thumbHeight < 4) thumbHeight = 4;
        
        int16_t thumbY = barY + (m_packetLogScroll * barHeight) / logSize;
        canvas.fillRect(barX, thumbY, 3, thumbHeight, theme::ACCENT());
    }
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawActionMenu(Canvas& canvas) {
#ifdef ESP32
    int16_t menuWidth = 140;
    int16_t rowHeight = 16;
    int16_t menuHeight = (m_actionCount + 1) * rowHeight + 8;
    int16_t menuX = (canvas.width() - menuWidth) / 2;
    int16_t menuY = (canvas.height() - menuHeight) / 2;
    
    canvas.fillRect(menuX - 2, menuY - 2, menuWidth + 4, menuHeight + 4, theme::BG_PRIMARY());
    canvas.fillRect(menuX, menuY, menuWidth, menuHeight, theme::BG_SECONDARY());
    canvas.drawRect(menuX, menuY, menuWidth, menuHeight, theme::ACCENT());
    
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(menuX + 6, menuY + 4);
    canvas.print("Quick Actions");
    
    int16_t y = menuY + rowHeight + 4;
    for (size_t i = 0; i < m_actionCount; i++) {
        bool selected = (i == m_actionMenuSelection);
        
        if (selected) {
            canvas.fillRect(menuX + 2, y - 1, menuWidth - 4, rowHeight - 2, theme::BG_SELECTED());
        }
        
        canvas.setTextColor(selected ? theme::TEXT_PRIMARY() : theme::TEXT_SECONDARY());
        canvas.setCursor(menuX + 8, y + 2);
        canvas.print(getActionName(m_availableActions[i]));
        
        y += rowHeight;
    }
#else
    (void)canvas;
#endif
}

} // namespace adversary
