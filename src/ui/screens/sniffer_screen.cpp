/**
 * @file sniffer_screen.cpp
 * @brief Packet Sniffer UI screen implementation
 */

#include "sniffer_screen.h"
#include "sniffer_actions.h"
#include "config/config.h"
#include <cstring>
#include "core/event_bus.h"
#include "core/event_data.h"
#include "utils/log.h"

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
    , m_logPaused(false)
    , m_deauthCount(0)
    , m_deauthWindowStart(0)
    , m_deauthFloodAlert(false)
{
    memset(m_targetSsid, 0, sizeof(m_targetSsid));
    memset(m_targetBssid, 0, sizeof(m_targetBssid));
    m_stats.reset();
    m_packetLog.reserve(PACKET_LOG_SIZE);

    // Wire the shared quick-action overlay once. Selecting a row (ENTER or a shortcut key) maps
    // the shortcut back to a PacketAction and runs the unchanged, native-tested policy; dismissing
    // (ESC) just repaints the packet log underneath.
    actionMenu_.setTitle("Quick Actions");
    actionMenu_.setOnAction([this](char shortcut) {
        executeAction(actionFromShortcut(shortcut));
        m_needsRedraw = true;
    });
    actionMenu_.setOnDismiss([this]() {
        m_needsRedraw = true;
    });
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
    
    // Drain frames handed over from the WiFi-driver task and do all stats/log/pcap work here, on
    // the main loop (slice-0078). Runs whenever capturing: the log and stats always update; the
    // pcap write inside processFrame is itself gated on m_saveToFile.
    if (m_capturing) {
        drainFrameQueue();
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

void SnifferScreen::enqueueFrame(const CapturedPacket& packet)
{
#ifdef ESP32
    // Runs in the WiFi driver task. Copy the frame out of the driver-owned buffer (valid only for
    // this call) and hand it to the main loop. This is the ONLY work done here: no std::vector,
    // std::queue or m_stats access, so nothing the main loop also touches is mutated off-task.
    if (!m_frameQueue) {
        return;
    }

    SniffedFrame frame;
    frame.length = (packet.length > SNIFFER_FRAME_CAP) ? SNIFFER_FRAME_CAP : packet.length;
    memcpy(frame.data, packet.data, frame.length);
    frame.rssi = packet.rssi;
    frame.channel = packet.channel;
    frame.timestamp = packet.timestamp;
    frame.frameType = packet.frameType;
    frame.subtype = packet.subtype;
    memcpy(frame.addr1, packet.addr1, 6);
    memcpy(frame.addr2, packet.addr2, 6);
    memcpy(frame.addr3, packet.addr3, 6);

    // The promiscuous callback is a WiFi driver *task*, not a hardware ISR (it already blocks on
    // EventBus's mutex and allocates on the heap from here), so the task-API xQueueSend is correct.
    // Zero block time: never stall the driver; drop and count when the main loop is behind.
    if (xQueueSend(m_frameQueue, &frame, 0) != pdTRUE) {
        m_droppedFrames++;
    }
#else
    (void)packet;
#endif
}

void SnifferScreen::drainFrameQueue()
{
#ifdef ESP32
    if (!m_frameQueue) {
        return;
    }

    SniffedFrame frame;
    size_t processed = 0;
    while (processed < SNIFFER_FRAME_DRAIN_BUDGET &&
           xQueueReceive(m_frameQueue, &frame, 0) == pdTRUE) {
        // Rebuild a CapturedPacket as a view over the frame's own (main-loop-owned) buffer, so the
        // existing classification runs unchanged, now entirely on this task.
        CapturedPacket packet;
        packet.data = frame.data;
        packet.length = frame.length;
        packet.rssi = frame.rssi;
        packet.channel = frame.channel;
        packet.timestamp = frame.timestamp;
        packet.frameType = frame.frameType;
        packet.subtype = frame.subtype;
        memcpy(packet.addr1, frame.addr1, 6);
        memcpy(packet.addr2, frame.addr2, 6);
        memcpy(packet.addr3, frame.addr3, 6);

        processFrame(packet);
        processed++;
    }
#endif
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

    // Land the config selection on the Start/Stop action row so the primary action is one Enter
    // away and the long-standing "Enter starts capture" reflex is preserved (slice-0079).
    m_configList.setItemCount(CONFIG_ROW_COUNT);
    m_configList.setSelectedIndex(CFG_ACTION);

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

bool SnifferScreen::handleInput(char key)
{
    m_needsRedraw = true;

    // While the quick-action overlay is up it owns all input, like every other ActionMenu consumer
    // (slice-0077). Keys are consumed whether or not the menu mapped them, so none leaks to the
    // packet log underneath.
    if (actionMenu_.isVisible()) {
        actionMenu_.handleInput(key);
        return true;
    }

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
            if (m_screenState == SnifferScreenState::STATS) {
                // Activate the selected config row: cycle its value, or start/stop on the action
                // row. This replaces the old blanket Enter=toggleCapture (slice-0079).
                activateConfigRow();
            } else if (!m_packetLog.empty()) {
                // Snapshot the selected packet now: the ring mutates in place and auto-scroll can
                // move the selection while the menu is open, so the action must act on this copy.
                // The list holds an oldest-first display row; resolve it to its physical slot (slice-0070).
                m_selectedPacket = m_packetLog[snifferLogPhysicalIndex(m_packetLogHead, m_packetLog.size(),
                                                                       m_packetLogList.selectedIndex())];
                buildActionList(m_selectedPacket);  // opens the overlay iff the frame type affords an action
                m_needsRedraw = true;
            }
            return true;
            
        case '`':   // Back/escape
        case 27:    // ESC
            // The overlay, when open, consumed ESC above; here it always exits to the menu.
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
            
        case ';':   // Up - config rows in STATS, packet log in PACKET_LOG (overlay nav handled above)
            if (m_screenState == SnifferScreenState::STATS) {
                moveConfigSelection(false);  // skips disabled rows in fixed-target mode
            } else if (!m_packetLog.empty()) {
                m_packetLogList.setItemCount((int)m_packetLog.size());
                m_packetLogList.moveUp();  // wraps to the last row at the top
                m_needsRedraw = true;
            }
            return true;

        case '.':   // Down - config rows in STATS, packet log in PACKET_LOG (overlay nav handled above)
            if (m_screenState == SnifferScreenState::STATS) {
                moveConfigSelection(true);
            } else if (!m_packetLog.empty()) {
                m_packetLogList.setItemCount((int)m_packetLog.size());
                m_packetLogList.moveDown();  // wraps to the first row at the bottom
                m_needsRedraw = true;
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
            m_packetLogList.setItemCount(0);
            m_packetLogList.setSelectedIndex(0);
            m_needsRedraw = true;
            return true;
            
        case '+':   // Channel up (accelerator for the Channel config row)
            stepChannel(true);
            return true;

        case '-':   // Channel down
            stepChannel(false);
            return true;

        case 'h':   // Toggle channel hopping (accelerator for the Hopping config row)
        case 'H':
            toggleHopping();
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
            LOG_W("SNIFFER", "Failed to open PCAP file");
            // Continue without saving
        }
    }
    
    // Configure filter based on mode
    configureFilter();

#ifdef ESP32
    // Create the frame handoff queue before the sniffer starts, so the WiFi-task callback always
    // has a live queue to send to (slice-0078). Fail loud: without it every frame would be dropped.
    m_frameQueue = xQueueCreate(SNIFFER_FRAME_QUEUE_DEPTH, sizeof(SniffedFrame));
    if (!m_frameQueue) {
        LOG_E("SNIFFER", "Failed to allocate frame queue");
        m_pcapWriter.close();
        return;
    }
#endif
    m_droppedFrames = 0;

    // Start sniffer with callback. The callback runs in the WiFi driver task and only enqueues;
    // all processing happens in drainFrameQueue on the main loop.
    auto callback = [this](const CapturedPacket& packet) {
        this->enqueueFrame(packet);
    };

    if (!m_sniffer.start(callback)) {
        LOG_E("SNIFFER", "Failed to start sniffer");
        m_pcapWriter.close();
#ifdef ESP32
        vQueueDelete(m_frameQueue);
        m_frameQueue = nullptr;
#endif
        return;
    }

    m_capturing = true;
    m_stats.currentChannel = m_sniffer.getChannel();
    m_needsRedraw = true;
    
    LOG_I("SNIFFER", "Capture started");
}

void SnifferScreen::stopCapture()
{
    if (!m_capturing) return;

    // Stop the sniffer first: this disables promiscuous mode and clears the driver callback, so no
    // further frame can be enqueued after it returns. Only then is it safe to drain and delete the
    // queue (slice-0078). Ordering closes the use-after-free the crash backtrace walked through.
    m_sniffer.stop();

    // Flush any frames still queued into the pcap file before closing it (drain budget == queue
    // depth, so one pass empties it).
    drainFrameQueue();

    // Close PCAP file
    if (m_pcapWriter.isOpen()) {
        m_pcapWriter.flush();
        m_pcapWriter.close();
    }

#ifdef ESP32
    if (m_frameQueue) {
        vQueueDelete(m_frameQueue);
        m_frameQueue = nullptr;
    }
#endif

    m_capturing = false;
    m_needsRedraw = true;
    
    LOG_I("SNIFFER", "Capture stopped");
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

bool SnifferScreen::isConfigRowEnabled(int idx) const
{
    // Channel and Hopping are live-capture controls: they only affect the radio while it is
    // actually capturing (esp_wifi_set_channel needs WiFi up), and are fixed by the target when
    // one is set, so both are disabled (greyed + skipped) otherwise. Channel is additionally
    // disabled while hopping, since the sniffer then auto-cycles channels and a manual pick has no
    // effect. Mode/Save/Start stay actionable at all times (slice-0079).
    if (idx == CFG_HOPPING) return m_capturing && !m_hasTarget;
    if (idx == CFG_CHANNEL) return m_capturing && !m_hasTarget && !m_sniffer.isChannelHopping();
    return true;
}

void SnifferScreen::moveConfigSelection(bool down)
{
    m_configList.setItemCount(CONFIG_ROW_COUNT);
    // Move one row, then keep going in the same direction over any disabled row. Bounded by the
    // row count; CFG_MODE/CFG_SAVE/CFG_ACTION are always enabled, so this always lands on one.
    for (int i = 0; i < CONFIG_ROW_COUNT; ++i) {
        if (down) m_configList.moveDown();
        else      m_configList.moveUp();
        if (isConfigRowEnabled(m_configList.selectedIndex())) break;
    }
    m_needsRedraw = true;
}

void SnifferScreen::activateConfigRow()
{
    switch (m_configList.selectedIndex()) {
        case CFG_CHANNEL:  stepChannel(true); break;   // row is only reachable while editable
        case CFG_HOPPING:  toggleHopping();   break;
        case CFG_MODE:     cycleCaptureMode(); break;
        case CFG_SAVE:     m_saveToFile = !m_saveToFile; break;
        case CFG_ACTION:   toggleCapture();   break;
    }
    m_needsRedraw = true;
}

void SnifferScreen::stepChannel(bool up)
{
    // Shared by the Channel config row and the +/- keys. A no-op unless capturing and not
    // targeting, and inert while hopping, matching when the Channel row is enabled. Guarding here
    // too keeps the +/- accelerators honest even though the row nav already skips disabled states.
    if (!m_capturing || m_hasTarget || m_sniffer.isChannelHopping()) return;
    uint8_t ch = m_sniffer.getChannel();
    ch = up ? (ch < 13 ? ch + 1 : 1) : (ch > 1 ? ch - 1 : 13);
    m_sniffer.setChannel(ch);
    m_stats.currentChannel = m_sniffer.getChannel();
    m_needsRedraw = true;
}

void SnifferScreen::toggleHopping()
{
    // Shared by the Hopping config row and the 'h' key. Only while capturing and not targeting;
    // setChannelHopping is a plain flag the capture loop reads, so toggling it live is all it takes.
    if (!m_capturing || m_hasTarget) return;
    m_sniffer.setChannelHopping(!m_sniffer.isChannelHopping(), 100);
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

void SnifferScreen::processFrame(const CapturedPacket& packet)
{
    // Runs on the main loop (from drainFrameQueue). This is the sole writer of m_stats,
    // m_packetLog and the pcap file, and it shares that task with render(), so none of them needs a
    // lock: the cross-task heap race the old WiFi-task onPacketReceived caused is gone (slice-0078).

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
        }
    }
    
    // Append to the visible log only while it is following the live tail and not manually paused.
    // Scrolling up off the newest row (or pressing Pause) freezes the ring so the operator can
    // read it at any packet rate; stats, flood detection and SD capture below keep running
    // regardless, so nothing real is lost while the display is frozen (slice-0071).
    if (!m_logPaused && isLogFollowingTail() && shouldCapturePacket(packet)) {
        addPacketToLog(packet);
    }
    
    // Write to the pcap file. This SD/SPI work was deferred off the WiFi driver task to here on
    // purpose; packet.data/length already carry the SNIFFER_FRAME_CAP truncation from enqueueFrame.
    if (m_saveToFile && shouldCapturePacket(packet) && m_pcapWriter.isOpen()) {
        m_pcapWriter.writePacket(packet.data, packet.length, packet.timestamp);
        m_stats.bytesWritten = m_pcapWriter.getBytesWritten();
        // Periodic flush every 100 packets
        if (m_pcapWriter.getPacketCount() % 100 == 0 && m_pcapWriter.getPacketCount() > 0) {
            m_pcapWriter.flush();
        }
    }

    m_needsRedraw = true;
}

bool SnifferScreen::isLogFollowingTail() const
{
    // The log rides the live tail only while the selection sits on the newest row. An empty log
    // follows by definition so the first frames append (slice-0071).
    const int count = m_packetLogList.itemCount();
    return count == 0 || m_packetLogList.selectedIndex() >= count - 1;
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
            LOG_W("SNIFFER", "ALERT: Deauth flood detected!");
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
    
    // Add to ring buffer.
    if (m_packetLog.size() < PACKET_LOG_SIZE) {
        m_packetLog.push_back(summary);
    } else {
        m_packetLog[m_packetLogHead] = summary;
        m_packetLogHead = (m_packetLogHead + 1) % PACKET_LOG_SIZE;
    }

    // We only reach here while the log is following the live tail: the ingest gate in
    // processFrame stops appending the moment the operator scrolls up, so the ring freezes
    // and the view stays readable (slice-0071). So ride the tail: pin the selection to the newest
    // row and let the model scroll it into view. setViewport was set by the last render.
    const int newCount = (int)m_packetLog.size();
    m_packetLogList.setItemCount(newCount);
    m_packetLogList.setSelectedIndex(newCount - 1);
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

// The PacketAction <-> shortcut key mapping, in one place so buildActionList (forward) and the
// menu callback (reverse) can never disagree. Shortcuts follow the scanner convention (D/H/T)
// plus K for Karma. CANCEL carries no row: the shared menu's ESC/backtick already dismisses.
namespace {
struct SnifferActionKey { PacketAction action; char shortcut; };
constexpr SnifferActionKey SNIFFER_ACTION_KEYS[] = {
    { PacketAction::HANDSHAKE_CAPTURE, 'H' },
    { PacketAction::DEAUTH_ATTACK,     'D' },
    { PacketAction::EVIL_TWIN,         'T' },
    { PacketAction::KARMA_ATTACK,      'K' },
};
} // namespace

void SnifferScreen::buildActionList(const PacketSummary& packet)
{
    actionMenu_.clearItems();

    // Frame-type-conditional action set. The conditionality lives here in the caller, not in the
    // shared component, exactly as scanner/ble_scanner pick their own items (slice-0077).
    int added = 0;
    auto add = [this, &added](PacketAction action) {
        actionMenu_.addItem(getActionShortcut(action), getActionName(action), true);
        ++added;
    };

    switch (packet.frameType) {
        case PacketFrameType::BEACON:
        case PacketFrameType::PROBE_RESP:
            // Beacon/Probe Response - AP actions
            add(PacketAction::HANDSHAKE_CAPTURE);
            add(PacketAction::DEAUTH_ATTACK);
            add(PacketAction::EVIL_TWIN);
            break;

        case PacketFrameType::PROBE_REQ:
            // Probe Request - client actions
            add(PacketAction::KARMA_ATTACK);
            break;

        case PacketFrameType::EAPOL:
            // EAPOL - handshake actions
            add(PacketAction::HANDSHAKE_CAPTURE);
            break;

        case PacketFrameType::DEAUTH:
        default:
            // No attack for these frame types.
            break;
    }

    // Open the overlay only when the packet actually affords an action; a frame type with none no
    // longer pops a useless Cancel-only menu (the old hand-rolled version always did).
    if (added > 0) {
        actionMenu_.show();
    }
}

char SnifferScreen::getActionShortcut(PacketAction action)
{
    for (const auto& key : SNIFFER_ACTION_KEYS) {
        if (key.action == action) return key.shortcut;
    }
    return '?';  // unreachable for any action buildActionList adds
}

PacketAction SnifferScreen::actionFromShortcut(char shortcut)
{
    char upper = (shortcut >= 'a' && shortcut <= 'z') ? static_cast<char>(shortcut - 32) : shortcut;
    for (const auto& key : SNIFFER_ACTION_KEYS) {
        if (key.shortcut == upper) return key.action;
    }
    // The component only reports a shortcut it rendered, so an unmatched key means the table and
    // the addItem() calls drifted: fail loud instead of silently launching the wrong attack.
    LOG_E("SNIFFER", "Unknown action shortcut '%c'", shortcut);
    return PacketAction::CANCEL;  // makeSnifferAttackTarget() rejects CANCEL -> no-op
}

const char* SnifferScreen::getActionName(PacketAction action) const
{
    switch (action) {
        case PacketAction::HANDSHAKE_CAPTURE:  return "Handshake Capture";
        case PacketAction::DEAUTH_ATTACK:      return "Deauth Attack";
        case PacketAction::EVIL_TWIN:          return "Evil Twin";
        case PacketAction::KARMA_ATTACK:       return "Karma Attack";
        case PacketAction::CANCEL:             return "Cancel";
        default:                               return "???";
    }
}

void SnifferScreen::executeAction(PacketAction action)
{
    // Act on the packet snapshotted when the menu opened, never on a live ring index. The attack
    // target (screen + source MAC/SSID/channel) is built by the pure, native-tested policy;
    // CANCEL and unmapped actions return false and just close the menu.
    EventData evt(EventType::ATTACK_TARGET_SELECTED);
    if (!makeSnifferAttackTarget(m_selectedPacket, action, evt.payload.attackTarget)) {
        return;
    }

    // The navigator (subscribed in main.cpp) tears down this screen via hide() and launches the
    // attack on the packet's source: the sniffer never reaches into the attack screens itself.
    LOG_I("SNIFFER", "Executing action %s on %s",
          getActionName(action), m_selectedPacket.ssid[0] ? m_selectedPacket.ssid : "<hidden>");
    EventBus::getInstance().publish(evt);
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
    }

    // The quick-action overlay draws its own full-screen backdrop on top of the packet log
    // (shared ui::ActionMenu, slice-0077). Opened only from PACKET_LOG, so the log is underneath.
    if (actionMenu_.isVisible()) {
        actionMenu_.render(canvas);
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
        // STATS view: channel/hopping/mode/save/start all live in the config block now, so the
        // footer carries only the two actions that are not config rows (slice-0079).
        footerHints_.setHints({
            {'L', "Log", true},
            {'C', "Clear", true}
        });
    }

    footerHints_.render(canvas);
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawHeader(Canvas& canvas) {
#ifdef ESP32
    const char* title = m_hasTarget ? m_targetSsid : "Packet Sniffer";

    // Capture state shows once, as the status-bar center tag: a steady red "REC" while capturing,
    // otherwise the capture-mode name in its usual colour so the mode stays glanceable in both
    // views when idle (slice-0079). The old footer [REC]/[STOP] indicator is gone. When frames are
    // being dropped under flood (queue full, slice-0078 telemetry), the tag carries the count so a
    // lossy capture is visible instead of silently incomplete.
    if (m_capturing) {
        char recTag[16];
        if (m_droppedFrames > 0) {
            snprintf(recTag, sizeof(recTag), "REC !%u", (unsigned)m_droppedFrames);
        } else {
            snprintf(recTag, sizeof(recTag), "REC");
        }
        ui::StatusBar::render(canvas, title, recTag,
                              theme::BG_SECONDARY(), theme::TEXT_PRIMARY(), theme::ERROR());
    } else {
        ui::StatusBar::render(canvas, title, getModeName());
    }

    canvas.drawLine(0, ui::STATUS_BAR_HEIGHT - 1, canvas.width(), ui::STATUS_BAR_HEIGHT - 1,
                     theme::BG_TERTIARY());
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawStats(Canvas& canvas) {
#ifdef ESP32
    const int16_t lineHeight = theme::LIST_ROW_HEIGHT;  // 14px; same pitch as the deauth config list

    // --- Config block: Channel / Hopping / Mode / Save / Start-Stop as selectable rows ---
    char channelStr[12];
    if (m_hasTarget) {
        snprintf(channelStr, sizeof(channelStr), "%d", (int)m_targetChannel);
    } else if (m_sniffer.isChannelHopping()) {
        snprintf(channelStr, sizeof(channelStr), "auto");  // sniffer cycles channels; no fixed value
    } else {
        snprintf(channelStr, sizeof(channelStr), "%d", (int)m_sniffer.getChannel());
    }

    // Save row carries the live write status, not just on/off: the running pcap size while writing,
    // or ERR on a write failure, so the operator can confirm the capture is actually landing on SD
    // (slice-0079). This is the file-size readout the pre-redesign stats column had.
    char saveStr[16];
    if (!m_saveToFile) {
        snprintf(saveStr, sizeof(saveStr), "off");
    } else if (m_pcapWriter.hasError()) {
        snprintf(saveStr, sizeof(saveStr), "on ERR");
    } else if (m_pcapWriter.isOpen() || m_pcapWriter.hasWrittenData()) {
        uint32_t bytes = m_pcapWriter.getBytesWritten();
        if (bytes < 1024) {
            snprintf(saveStr, sizeof(saveStr), "on %luB", (unsigned long)bytes);
        } else if (bytes < 1024 * 1024) {
            snprintf(saveStr, sizeof(saveStr), "on %.1fK", bytes / 1024.0f);
        } else {
            snprintf(saveStr, sizeof(saveStr), "on %.1fM", bytes / (1024.0f * 1024.0f));
        }
    } else {
        snprintf(saveStr, sizeof(saveStr), "on");
    }

    struct ConfigRow {
        const char* label;
        const char* value;
        bool enabled;
    };
    const ConfigRow rows[CONFIG_ROW_COUNT] = {
        { "Channel",    channelStr,                                     isConfigRowEnabled(CFG_CHANNEL) },
        { "Hopping",    m_hasTarget ? "fixed"
                                    : (m_sniffer.isChannelHopping() ? "on" : "off"),
                                                                        isConfigRowEnabled(CFG_HOPPING) },
        { "Mode",       getModeName(),                                  true },
        { "Save to SD", saveStr,                                        true },
        { m_capturing ? "Stop Capture" : "Start Capture", "",           true },
    };

    const int16_t configTop = ui::STATUS_BAR_HEIGHT;            // y=20, directly under the status bar
    const int16_t configH   = CONFIG_ROW_COUNT * lineHeight;    // 70px
    m_configList.setItemCount(CONFIG_ROW_COUNT);
    ScrollableList::render(canvas, m_configList, 0, configTop, canvas.width(), configH, lineHeight,
        [&rows](Canvas& c, int idx, int16_t rowY, bool selected) {
            const ConfigRow& row = rows[idx];
            // Disabled rows (Channel/Hopping in fixed-target mode) render greyed; the selected row
            // reddens to ACCENT like the deauth config idiom; others are primary text (slice-0079).
            uint16_t color = !row.enabled ? theme::TEXT_DISABLED()
                           : selected      ? theme::ACCENT()
                                           : theme::TEXT_PRIMARY();
            c.setTextColor(color);
            c.setCursor(4, rowY + theme::LIST_ROW_TEXT_DY);
            c.print(row.label);
            if (row.value[0] != '\0') {
                int16_t valueWidth = (int16_t)strlen(row.value) * theme::FONT_CELL_WIDTH;
                c.setCursor(c.width() - valueWidth - 4, rowY + theme::LIST_ROW_TEXT_DY);
                c.print(row.value);
            }
        },
        !footerHints_.hasFocus());

    // --- Separator, then the live counter grid below it ---
    const int16_t sepY = configTop + configH;                  // y=90
    canvas.drawLine(0, sepY, canvas.width(), sepY, theme::ACCENT_DARK());
    drawStatsCounters(canvas, sepY + 4);                       // y=94
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawStatsCounters(Canvas& canvas, int16_t top) {
#ifdef ESP32
    // Two rows x three columns holding all six traffic counts, with full type names on an ~80px
    // column pitch (the names fit comfortably at typical counts) (slice-0079).
    const int16_t col[3] = { 4, 84, 164 };
    const int16_t row2Y  = top + 12;

    canvas.setTextSize(1);
    auto cell = [&canvas](int16_t x, int16_t y, const char* label, uint32_t n, uint16_t numColor) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y);
        canvas.print(label);
        canvas.setTextColor(numColor);
        canvas.print(n);
    };

    cell(col[0], top,   "Packets ", m_stats.totalPackets,  theme::TEXT_PRIMARY());
    cell(col[1], top,   "Beacons ", m_stats.beacons,       theme::TEXT_PRIMARY());
    cell(col[2], top,   "Probes ",  m_stats.probeRequests, theme::TEXT_PRIMARY());
    cell(col[0], row2Y, "Data ",    m_stats.dataFrames,    theme::TEXT_PRIMARY());
    cell(col[1], row2Y, "EAPOL ",   m_stats.eapolFrames,
         m_stats.eapolFrames > 0 ? theme::SUCCESS() : theme::TEXT_PRIMARY());
    cell(col[2], row2Y, "Deauth ",  m_stats.deauthFrames,
         m_stats.deauthFrames > 0 ? theme::WARNING() : theme::TEXT_PRIMARY());
#else
    (void)canvas;
    (void)top;
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
    } else if (!isLogFollowingTail()) {
        // Frozen by scrolling up off the tail. Reassure the operator capture is still running (the
        // ring is only frozen for reading); returning to the newest row resumes the stream (slice-0071).
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(canvas.width() / 2 - 60, startY);
        canvas.print("[REVIEW - capturing]");
        startY += 12;
    }

    size_t logSize = m_packetLog.size();
    if (logSize == 0) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(canvas.width() / 2 - 40, canvas.height() / 2);
        canvas.print("No packets yet");
        return;
    }
    
    // The list rect starts below any banners (startY was pushed down by them), so the shared
    // component derives the visible-row count from the space that actually remains above the footer.
    m_packetLogList.setItemCount((int)logSize);
    const int16_t listTop = startY;
    const int16_t listH   = (int16_t)(canvas.height() - ui::FOOTER_HEIGHT - listTop);
    const bool focused = (m_screenState == SnifferScreenState::PACKET_LOG) && !footerHints_.hasFocus();
    ScrollableList::render(canvas, m_packetLogList, 0, listTop, canvas.width(), listH,
        theme::LIST_ROW_HEIGHT, [this](Canvas& c, int idx, int16_t rowY, bool /*sel*/) {
            // idx is an oldest-first display row; map it to its physical ring slot (slice-0070).
            drawPacketLogEntry(c, m_packetLog[snifferLogPhysicalIndex(m_packetLogHead, m_packetLog.size(), idx)], rowY);
        },
        focused);

    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(canvas.width() - 50, canvas.height() - STATUS_HEIGHT - 12);
    canvas.printf("[%u/%u]", (unsigned)(m_packetLogList.selectedIndex() + 1), (unsigned)logSize);
#else
    (void)canvas;
#endif
}

void SnifferScreen::drawPacketLogEntry(Canvas& canvas, const PacketSummary& pkt, int16_t y) {
#ifdef ESP32
    // Content only: the shared component paints the selection band under this row (slice-0068).
    int16_t screenWidth = canvas.width();

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
    
    canvas.setTextColor(theme::TEXT_PRIMARY());
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
#endif
}

} // namespace adversary
