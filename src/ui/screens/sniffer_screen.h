/**
 * @file sniffer_screen.h
 * @brief Packet Sniffer UI screen
 * 
 * Displays packet capture statistics and controls
 * for the 802.11 packet sniffer.
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/sniffer/packet_sniffer.h"
#include "modules/pcap/pcap_writer.h"
#include "core/event_bus.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "ui/components/list_scroll_model.h"
#include "ui/components/scrollable_list.h"
#include "ui/components/action_menu.h"
#include "screen_interface.h"
#include "sniffer_types.h"
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef ESP32
#include <Arduino.h>
#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

namespace adversary {

// Forward declaration
struct CapturedPacket;

/**
 * @brief Upper bound on the frame bytes copied out of the WiFi-driver buffer per packet.
 *
 * Matches the truncation the retired QueuedPacket already applied, so pcap output is unchanged.
 * Management frames (beacons/probes/deauth/EAPOL) sit well under this; longer data frames are
 * truncated here as before.
 */
constexpr uint16_t SNIFFER_FRAME_CAP = 512;

/**
 * @brief One captured frame copied out of the WiFi-driver task for the main loop.
 *
 * The promiscuous callback runs in the WiFi driver task and its frame buffer is valid only for the
 * duration of the callback, so the bytes are copied here and the already-parsed header is carried
 * alongside them. Instances cross from the WiFi task to the main loop by value over a FreeRTOS
 * queue (slice-0078); nothing in it points at driver-owned memory.
 */
struct SniffedFrame {
    uint8_t data[SNIFFER_FRAME_CAP];
    uint16_t length;      ///< Bytes valid in data[], clamped to SNIFFER_FRAME_CAP
    int8_t rssi;
    uint8_t channel;
    uint32_t timestamp;
    FrameType frameType;
    uint8_t subtype;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
};

/**
 * @brief Capture mode for sniffer
 */
enum class CaptureMode : uint8_t {
    ALL_PACKETS,
    BEACONS_ONLY,
    PROBE_REQUESTS,
    DATA_ONLY,
    EAPOL_ONLY,
    DEAUTH_ONLY
};

/**
 * @brief Statistics for capture display
 */
struct CaptureStats {
    uint32_t totalPackets;
    uint32_t beacons;
    uint32_t probeRequests;
    uint32_t probeResponses;
    uint32_t dataFrames;
    uint32_t eapolFrames;
    uint32_t deauthFrames;
    uint32_t bytesWritten;
    uint8_t currentChannel;
    
    void reset();
};

// PacketFrameType, EapolMsgType and PacketSummary now live in sniffer_types.h (display-free,
// shared with the native-tested sniffer_actions policy).

/**
 * @brief Sniffer screen display state
 */
enum class SnifferScreenState : uint8_t {
    STATS,
    PACKET_LOG
    // The quick-action overlay is no longer a screen state: it renders through the shared
    // ui::ActionMenu and is gated on actionMenu_.isVisible(), the same way scanner_screen does
    // (slice-0077). The base state stays PACKET_LOG while the menu overlays it.
};

// PacketAction now lives in sniffer_types.h (shared with the native-tested sniffer_actions policy).

/**
 * @brief Sniffer screen UI component
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class SnifferScreen : public IScreen {
public:
    SnifferScreen();
    ~SnifferScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return m_active; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }
    
    const char* getName() const override { return "Packet Sniffer"; }
    ScreenId getId() const override { return ScreenId::SNIFFER; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    
    void setTargetNetwork(const char* ssid, const uint8_t* bssid, uint8_t channel);
    void clearTargetNetwork();
    
    void startCapture();
    void stopCapture();
    void toggleCapture();
    
    bool isCapturing() const { return m_capturing; }
    void setCaptureMode(CaptureMode mode);
    CaptureMode getCaptureMode() const { return m_captureMode; }
    void cycleCaptureMode();

    bool isActive() const { return m_active; }
    void setActive(bool active);
    const CaptureStats& getStats() const { return m_stats; }

private:
    // The WiFi-driver-task callback: copy the frame into a SniffedFrame and hand it to the main
    // loop over m_frameQueue. Touches no shared std container (slice-0078).
    void enqueueFrame(const CapturedPacket& packet);
    // Main-loop side: drain m_frameQueue and run processFrame on each. All stats/log/pcap work
    // lives on this one task (which also renders), so the std containers need no lock.
    void drainFrameQueue();
    // Classify one drained frame into stats, the packet log and the pcap file (formerly the body
    // of the WiFi-task onPacketReceived, now reached only from the main loop).
    void processFrame(const CapturedPacket& packet);

    void drawHeader(Canvas& canvas);
    // STATS view: the selectable config block (channel/hopping/mode/save + start-stop action
    // row) above a separator, then the live counter grid below it (slice-0079).
    void drawStats(Canvas& canvas);
    void drawStatsCounters(Canvas& canvas, int16_t top);
    void drawPacketLog(Canvas& canvas);
    void drawPacketLogEntry(Canvas& canvas, const PacketSummary& pkt, int16_t y);

    void addPacketToLog(const CapturedPacket& packet);
    // True while the log is pinned to its newest row; scrolling up freezes ingestion (slice-0071).
    bool isLogFollowingTail() const;
    uint16_t getFrameTypeColor(PacketFrameType type) const;
    const char* getFrameTypeName(PacketFrameType type) const;
    
    void buildActionList(const PacketSummary& packet);
    const char* getActionName(PacketAction action) const;
    // The shared ActionMenu identifies rows by a shortcut char; these map it to and from our
    // PacketAction enum through one table (slice-0077). actionFromShortcut fails loud on an
    // unknown key: the component only ever reports a shortcut it rendered.
    static char getActionShortcut(PacketAction action);
    static PacketAction actionFromShortcut(char shortcut);
    void executeAction(PacketAction action);
    
    const char* getModeName() const;
    void configureFilter();
    bool shouldCapturePacket(const CapturedPacket& packet) const;

    // STATS-view config block (slice-0079). Rows, in order: Channel, Hopping, Mode, Save to SD,
    // and a terminal Start/Stop Capture action row. Channel and Hopping are disabled (greyed, and
    // skipped by the nav) in fixed-target mode, where the sniffer owns the channel.
    enum ConfigRow : int { CFG_CHANNEL = 0, CFG_HOPPING, CFG_MODE, CFG_SAVE, CFG_ACTION };
    static constexpr int CONFIG_ROW_COUNT = 5;
    bool isConfigRowEnabled(int idx) const;
    void moveConfigSelection(bool down);
    void activateConfigRow();
    // Channel/hopping controls shared by the config rows and the +/-/h accelerators, so the two
    // entry points cannot drift. Both are live-capture controls: no-ops unless capturing and not
    // targeting (esp_wifi_set_channel only works with WiFi up), which is exactly when the rows are
    // enabled. stepChannel is additionally inert while hopping, since the sniffer auto-cycles then
    // (slice-0079).
    void stepChannel(bool up);
    void toggleHopping();

    PacketSniffer& m_sniffer;
    PcapWriter m_pcapWriter;

    CaptureStats m_stats;
    CaptureMode m_captureMode;
    
#ifdef ESP32
    // Lock-free handoff from the WiFi-driver task (enqueueFrame) to the main loop (drainFrameQueue).
    // Created in startCapture before the sniffer starts and deleted in stopCapture after it stops,
    // so no enqueue can ever target a freed queue (slice-0078).
    QueueHandle_t m_frameQueue = nullptr;
#endif
    // Frames dropped because the queue was full (main loop not draining fast enough under flood).
    // Backpressure telemetry; capture keeps running, matching the old drop-on-full behaviour.
    uint32_t m_droppedFrames = 0;

    bool m_hasTarget;
    char m_targetSsid[33];
    uint8_t m_targetBssid[6];
    uint8_t m_targetChannel;
    
    bool m_active;
    bool m_shouldExit;
    bool m_capturing;
    bool m_saveToFile;
    bool m_needsRedraw;
    uint32_t m_lastUpdate;
    
#ifdef ESP32
    M5Canvas* m_canvas;
    bool m_canvasInitialized;
#endif
    
    SnifferScreenState m_screenState;
    // Selection + scroll state for the STATS-view config block (slice-0079), fed its item count
    // and viewport by drawStats each frame, exactly like m_packetLogList below.
    ListScrollModel m_configList;
    std::vector<PacketSummary> m_packetLog;
    size_t m_packetLogHead;
    // Owns the packet-log selection + scroll window + wrap; drawPacketLog feeds it the live item
    // count and viewport each frame, so cursor and scroll offset can't drift (slice-0062/0068).
    ListScrollModel m_packetLogList;
    bool m_logPaused;
    // Snapshot of the packet the action menu was opened on. The menu acts on this copy, never on
    // m_packetLog[m_packetLogList.selectedIndex()]: the ring overwrites entries in place and auto-scroll can
    // move the selection while the menu is open, so a live index would target a different packet.
    PacketSummary m_selectedPacket;

    // The packet-log quick-action overlay, shared with scanner/ble_scanner/wardriving/etc.
    // buildActionList() fills it per frame type and shows it; gated on isVisible() (slice-0077).
    ui::ActionMenu actionMenu_;

    uint32_t m_deauthCount;
    uint32_t m_deauthWindowStart;
    bool m_deauthFloodAlert;
    
    // 50 entries at sizeof(PacketSummary)==40B = 2KB, reserved once at construction. Sized for
    // scrollback: scrolling up freezes the ring (slice-0071), and 50 rows give the operator more
    // captured history to page through while frozen than the previous 25.
    static constexpr size_t PACKET_LOG_SIZE = 50;
    static constexpr uint32_t DEAUTH_FLOOD_THRESHOLD = 10;
    static constexpr uint32_t DEAUTH_WINDOW_MS = 1000;
    
    // Frame-queue depth. A busy 2.4 GHz channel delivers a few hundred frames/s (<1/ms); against a
    // worst-case ~10 ms gap between update() drains that is ~3 frames in flight, so 16 is a >5x
    // margin. sizeof(SniffedFrame) ~= 544 B, so the queue is one ~8.5 KB block allocated once at
    // startCapture and freed at stopCapture, not per packet (slice-0078).
    static constexpr size_t SNIFFER_FRAME_QUEUE_DEPTH = 16;
    // Frames drained per update() call; = depth so a full queue clears in one pass under normal
    // load, with residual frames carried to the next call rather than blocking the loop.
    static constexpr size_t SNIFFER_FRAME_DRAIN_BUDGET = SNIFFER_FRAME_QUEUE_DEPTH;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t STATUS_HEIGHT = 16;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 200;
    
    // EventBus subscriptions
    uint32_t packetHandlerId_ = 0;
    uint32_t eapolHandlerId_ = 0;
    
    ui::FooterHints footerHints_;
};

} // namespace adversary
