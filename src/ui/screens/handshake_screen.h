/**
 * @file handshake_screen.h
 * @brief Handshake capture UI screen
 * 
 * Implements IScreen interface for ScreenManager integration.
 */

#pragma once

#include "modules/capture/handshake_capture.h"
#include "modules/pcap/pcap_writer.h"
#include "core/event_bus.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/footer_hints.h"
#include "utils/handshake_utils.h"
#include "utils/mac_utils.h"
#include "screen_interface.h"
#include <cstdint>
#include <vector>

namespace adversary {

/**
 * @brief Lightweight per-capture record for the Auto Hunt session list view.
 *
 * The full CapturedHandshake (~1.4 KB — four fixed EAPOL message buffers) is far
 * too heavy to keep N copies of just to render a list; the real handshake is
 * already written to SD at capture time. The list only needs identity + which
 * message frames were seen, so we store this ~45-byte summary instead. Field
 * names match CapturedHandshake so the render/delete code is unchanged.
 */
struct SessionHandshakeEntry {
    char    ssid[33] = {0};
    uint8_t apBssid[6] = {0};
    uint8_t channel = 0;
    bool    hasMsg1 = false;
    bool    hasMsg2 = false;
    bool    hasMsg3 = false;
    bool    hasMsg4 = false;
    int8_t  signalStrength = 0;
};

/**
 * @brief Screen states for handshake capture
 */
enum class HandshakeScreenState : uint8_t {
    CONFIG,
    CAPTURING,
    SUCCESS,
    TIMEOUT,
    ERROR,
    HANDSHAKE_LIST,
    CONFIRM_DELETE
};

/**
 * @brief Auto Hunt state machine states
 */
enum class AutoHuntState : uint8_t {
    SCANNING,
    LOCKING,
    ATTACKING,
    WAITING,
    NEXT_TARGET,
    IDLE_SCAN
};

/**
 * @brief Handshake capture screen
 * 
 * Implements IScreen for ScreenManager compatibility.
 */
class HandshakeScreen : public IScreen {
public:
    HandshakeScreen();
    ~HandshakeScreen() override;
    
    // =========================================================================
    // IScreen Interface
    // =========================================================================
    
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }
    
    bool handleInput(char key) override;
    
    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }
    
    const char* getName() const override { return "Handshake"; }
    ScreenId getId() const override { return ScreenId::HANDSHAKE; }
    
    // =========================================================================
    // Screen-specific methods
    // =========================================================================
    
    void init() override;
    void setTarget(const uint8_t* bssid, const char* ssid, uint8_t channel);
    bool hasTarget() const { return hasTarget_; }

    // Factory lazy-loading: receive target via ScreenManager::navigateWithParams()
    void setParams(const ScreenParams& params) override {
        setTarget(params.bssid, params.ssid, params.channel);
    }

private:
    // Drawing methods
    void drawHeader(Canvas& canvas, const char* title);
    void drawConfig(Canvas& canvas);
    void drawCapturing(Canvas& canvas);
    void drawAutoHuntCapturing(Canvas& canvas);
    void drawSuccess(Canvas& canvas);
    void drawTimeout(Canvas& canvas);
    void drawProgressRing(Canvas& canvas, int x, int y, int radius, float progress);
    void drawHandshakeList(Canvas& canvas);
    void drawDeleteConfirm(Canvas& canvas);
    
    // Capture control
    void startCapture();
    void stopCapture();
    void saveHandshake();
    void savePMKID22000(const CapturedPMKID& pmkid);
    void onHandshakeCaptured(const SessionHandshakeEntry& hs);
    void onStateChanged(HandshakeState state);
    
    // Auto Hunt state machine
    void updateAutoHunt();
    bool selectNextTarget();
    void setAutoHuntState(AutoHuntState newState);
    
    // Handshake list helpers
    void loadSessionHandshakes();
    void startDeletionConfirm();
    void executeDeletion();
    
    // Track attempted networks
    void markNetworkAttempted(const uint8_t* bssid);
    bool wasNetworkAttempted(const uint8_t* bssid) const;
    
    HandshakeCapture& capture_;
    PcapWriter pcapWriter_;
    
    HandshakeScreenState screenState_;
    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    uint32_t lastUpdate_;
    bool initialized_;
    uint32_t handshakeHandlerId_ = 0;  // EventBus subscription
    uint32_t stateHandlerId_ = 0;      // ATTACK_STATE_CHANGED subscription
    
    // Target info
    bool hasTarget_;
    uint8_t targetBssid_[6];
    char targetSsid_[33];
    uint8_t targetChannel_;
    
    // Config options
    int configSelection_;
    bool autoDeauth_;
    uint8_t deauthCount_;
    uint32_t timeoutSecs_;
    
    // Capture state
    bool capturing_;
    bool handshakeSaved_;
    char savedFilename_[128];
    
    // Auto Hunt mode
    bool autoHuntMode_ = false;
    AutoHuntState autoState_;
    uint32_t autoStateStart_ = 0;
    uint32_t handshakesCaptured_ = 0;
    uint32_t pmkidsCaptured_ = 0;
    uint32_t networksScanned_ = 0;
    uint32_t networksSkipped_ = 0;
    int currentTargetIndex_ = -1;
    
    // Track attempted networks — ring buffer so a long/dense hunt keeps cycling.
    // The old cap of 16 silently dropped further attempts, so in any area with
    // >16 candidate APs, attempted targets were never recorded, got re-selected
    // repeatedly, and never counted as skipped. 128 entries × 6 B = 768 B; when
    // full, the oldest entry is evicted (becomes eligible again) instead of the
    // list going deaf.
    static constexpr uint8_t MAX_ATTEMPTED_NETWORKS = 128;
    uint8_t attemptedBssids_[MAX_ATTEMPTED_NETWORKS][6];
    uint8_t attemptedCount_ = 0;   // number of valid entries (saturates at MAX)
    uint8_t attemptedHead_ = 0;    // ring write index

    // Handshake list view state. Lightweight per-entry struct (SessionHandshakeEntry,
    // ~45 B) instead of the full CapturedHandshake (~1.4 KB) — the list only needs
    // SSID/BSSID/type flags for display + delete; the actual handshake is already
    // on SD. This is what kept the History view from nearly OOMing mid-capture.
    int handshakeListScroll_ = 0;
    int handshakeListSelection_ = 0;
    std::vector<SessionHandshakeEntry> sessionHandshakes_;
    std::vector<SessionHandshakeEntry> sessionHandshakeBuffer_;
    int deletionCandidateIndex_ = -1;
    HandshakeScreenState returnState_ = HandshakeScreenState::CAPTURING;
    
    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t FOOTER_HEIGHT = 16;
    static constexpr uint32_t REDRAW_INTERVAL_MS = 100;
    static constexpr int CONFIG_ITEMS = 5;
    
    // Auto Hunt timing constants (ms)
    static constexpr uint32_t AUTO_SCAN_TIME = 5000;
    static constexpr uint32_t AUTO_LOCK_TIME = 2000;
    static constexpr uint32_t AUTO_ATTACK_TIME = 30000;
    static constexpr uint32_t AUTO_WAIT_TIME = 2000;
    static constexpr uint32_t AUTO_IDLE_TIME = 10000;
    
    ui::FooterHints footerHints_;
};

} // namespace adversary
