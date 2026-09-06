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
#include "screen_interface.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <queue>

#ifdef ESP32
#include <Arduino.h>
#include <M5GFX.h>
#endif

namespace adversary {

// Forward declaration
struct CapturedPacket;

/**
 * @brief Queued packet for deferred SD writing
 */
struct QueuedPacket {
    uint8_t data[512];
    uint16_t length;
    uint32_t timestamp;
    
    QueuedPacket() : length(0), timestamp(0) {}
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

/**
 * @brief Frame type for packet log display
 */
enum class PacketFrameType : uint8_t {
    BEACON = 0,
    PROBE_REQ = 1,
    PROBE_RESP = 2,
    DATA = 3,
    EAPOL = 4,
    DEAUTH = 5
};

/**
 * @brief EAPOL message type for handshake tracking
 */
enum class EapolMsgType : uint8_t {
    UNKNOWN = 0,
    M1 = 1,
    M2 = 2,
    M3 = 3,
    M4 = 4
};

/**
 * @brief Lightweight packet summary for log display
 */
struct PacketSummary {
    uint32_t timestamp;
    PacketFrameType frameType;
    EapolMsgType eapolMsg;
    int8_t rssi;
    uint8_t srcMac[6];
    uint8_t dstMac[6];
    char ssid[17];
    uint8_t channel;
    uint8_t flags;
    
    PacketSummary();
};

/**
 * @brief Sniffer screen display state
 */
enum class SnifferScreenState : uint8_t {
    STATS,
    PACKET_LOG,
    ACTION_MENU
};

/**
 * @brief Quick action types available from packet log
 */
enum class PacketAction : uint8_t {
    HANDSHAKE_CAPTURE,
    DEAUTH_ATTACK,
    EVIL_TWIN,
    KARMA_ATTACK,
    COPY_BSSID,
    CANCEL
};

/**
 * @brief Sniffer screen UI component
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class SnifferScreen : public IScreen {
public:
    using HandshakeCapturedCallback = std::function<void(const char* filename)>;
    using PacketActionCallback = std::function<void(const PacketSummary& packet, PacketAction action)>;
    
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
    
    void setOnHandshakeCaptured(HandshakeCapturedCallback callback) { m_onHandshakeCaptured = callback; }
    void setOnPacketAction(PacketActionCallback callback) { m_onPacketAction = callback; }
    
    bool isActive() const { return m_active; }
    void setActive(bool active);
    const CaptureStats& getStats() const { return m_stats; }

private:
    void onPacketReceived(const CapturedPacket& packet);
    
    void drawHeader(Canvas& canvas);
    void drawStats(Canvas& canvas);
    void drawChannelIndicator(Canvas& canvas, int16_t x, int16_t y);
    void drawPacketLog(Canvas& canvas);
    void drawPacketLogEntry(Canvas& canvas, const PacketSummary& pkt, int16_t y, bool selected);
    void drawLogScrollbar(Canvas& canvas);
    void drawActionMenu(Canvas& canvas);
    
    void addPacketToLog(const CapturedPacket& packet);
    uint16_t getFrameTypeColor(PacketFrameType type) const;
    const char* getFrameTypeName(PacketFrameType type) const;
    
    void buildActionList(const PacketSummary& packet);
    const char* getActionName(PacketAction action) const;
    void executeAction(PacketAction action);
    
    const char* getModeName() const;
    void configureFilter();
    bool shouldCapturePacket(const CapturedPacket& packet) const;
    void processPacketQueue();
    
    PacketSniffer& m_sniffer;
    PcapWriter m_pcapWriter;
    
    HandshakeCapturedCallback m_onHandshakeCaptured;
    PacketActionCallback m_onPacketAction;
    CaptureStats m_stats;
    CaptureMode m_captureMode;
    
    std::queue<QueuedPacket> m_packetQueue;
    // static constexpr size_t MAX_QUEUE_SIZE = 50; // Removed duplicate
    
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
    std::vector<PacketSummary> m_packetLog;
    size_t m_packetLogHead;
    size_t m_packetLogSelection;
    size_t m_packetLogScroll;
    bool m_logPaused;
    
    size_t m_actionMenuSelection;
    PacketAction m_availableActions[6];
    size_t m_actionCount;
    
    uint32_t m_deauthCount;
    uint32_t m_deauthWindowStart;
    bool m_deauthFloodAlert;
    
    static constexpr size_t PACKET_LOG_SIZE = 25; // Reduce from 50 (Save ~1.5KB)
    static constexpr size_t LOG_VISIBLE_ROWS = 5;
    static constexpr int16_t LOG_ROW_HEIGHT = 18;
    static constexpr uint32_t DEAUTH_FLOOD_THRESHOLD = 10;
    static constexpr uint32_t DEAUTH_WINDOW_MS = 1000;
    
    // Queue size reduced from 50 to 10 to prevent OOM
    // 10 packets * ~518 bytes = ~5KB peak
    // 50 packets * ~518 bytes = ~26KB peak (CRITICAL FAIL)
    static constexpr size_t MAX_QUEUE_SIZE = 10;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t STATUS_HEIGHT = 16;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 200;
    
    // EventBus subscriptions
    uint32_t packetHandlerId_ = 0;
    uint32_t eapolHandlerId_ = 0;
    
    ui::FooterHints footerHints_;
};

} // namespace adversary
