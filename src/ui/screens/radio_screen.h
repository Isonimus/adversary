/**
 * @file radio_screen.h
 * @brief Radio screen for the multi-radio expansion cap.
 *
 * slice-0002 shipped this as a detection-only placeholder. slice-0003 made it the
 * CC1101 OOK console (Capture / Saved signals / Frequency). slice-0006 adds the
 * cap's second radio, so the screen now opens on a radio-select root (CC1101
 * Sub-GHz / NRF24 2.4 GHz); each radio owns its own sub-tree below that node.
 * There is no per-screen view stack in this framework, so the sub-levels are a
 * state enum (the CapturesScreen idiom); `` ` `` pops one level and only exits to
 * the carousel from the root.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/text_input_popup.h"
#include "../../modules/rf/ook_signal.h"
#include "../../modules/rf/spectrum_scan.h"
#include "../../modules/rf/band_sweep.h"
#include "screen_interface.h"

namespace adversary {

class RadioScreen : public IScreen {
public:
    RadioScreen();
    ~RadioScreen() override;

    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }

    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }

    bool handleInput(char key) override;

    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }

    const char* getName() const override { return "Radio"; }
    ScreenId getId() const override { return ScreenId::RADIO; }

    void init() override;
    void deinit();

private:
    // Sub-levels of the cap console (no per-screen view stack in the framework).
    enum class MenuState : uint8_t {
        RADIO_SELECT,    // root: CC1101 Sub-GHz / NRF24 2.4 GHz (slice-0006)
        MAIN,            // CC1101: Capture / Saved signals / Frequency
        CAPTURING,       // RMT RX window is open (blocking)
        CAPTURE_REVIEW,  // just-captured: pulse-strip preview, keep or discard
        NAMING,          // TextInputPopup collecting a name for the capture
        SIGNAL_LIST,     // saved .sub files
        SIGNAL_ACTION,   // Replay / Delete for the selected file
        REPLAY_CONFIRM,  // "transmit?" gate (replay emits RF)
        DELETE_CONFIRM,  // "delete?" gate
        FREQ_SELECT,     // pick a frequency preset
        FSK_SELECT,      // CC1101: pick an FSK modem preset to capture on (slice-0019)
        JAM_SELECT,      // CC1101 jammer: pick emission mode (slice-0017)
        JAM_CONFIRM,     // jammer: interference warning gate before arming
        JAM_ACTIVE,      // jammer: armed; emits while the hold key is held
        NRF_SCAN,        // NRF24: live 2.4 GHz occupancy sweep (slice-0006)
        BAND_SWEEP,      // CC1101: live RSSI sweep across the band presets (slice-0019)
    };

    struct SignalEntry {
        char name[40];   // basename incl. ".sub"
        uint32_t size;
    };

    // Render helpers (one per state).
    void drawUnavailable(Canvas& canvas);
    void drawRadioSelect(Canvas& canvas);
    void drawNrfScan(Canvas& canvas);
    void drawBandSweep(Canvas& canvas);
    void drawMain(Canvas& canvas);
    void drawCapturing(Canvas& canvas);
    void drawCaptureReview(Canvas& canvas);
    void drawSignalList(Canvas& canvas);
    void drawSignalAction(Canvas& canvas);
    void drawConfirm(Canvas& canvas, const char* question, uint16_t accent);
    void drawFreqSelect(Canvas& canvas);
    void drawFskSelect(Canvas& canvas);  // FSK modem preset picker (slice-0019)
    void drawJam(Canvas& canvas);  // mode-select / confirm / active (slice-0017)

    // Draws an OOK signal's summary (RAW · N edges · freq) plus its static
    // pulse-strip preview. Shared by the capture-review and signal-detail screens.
    void drawEncodingAndStrip(Canvas& canvas, const rf::OokSignal& sig,
                              int16_t x, int16_t y, int16_t w);
    void describeSignal(const rf::OokSignal& sig, char* out, size_t outSize) const;

    // Logic helpers.
    double currentFreqMHz() const;
    void updateFooterHints();  // footer hints reflecting the current state's actions
    void loadSignalList();
    void loadActionSignal();  // deserialize the selected signal for the detail view
    void startCapture();
    void performCapture();
    void promptForName();  // open the name popup for the reviewed capture
    bool captureSignal(rf::OokSignal& out);      // HAL+RMT orchestration for RX
    bool replaySignal(const rf::OokSignal& sig); // HAL+RMT orchestration for TX
    void saveCapturedSignal(const char* name);
    void replaySelected();
    void deleteSelected();
    void fullPath(const char* name, char* out, size_t outSize) const;

    // NRF24 analyzer (slice-0006): power up on entry, sweep a batch of channels
    // per update accumulating RPD occupancy, power down on exit.
    void startNrfScan();
    void stopNrfScan();
    void stepNrfScan();

    // CC1101 RSSI band-sweep (slice-0019): per update() retune to the next band
    // preset, enter RX, read RSSI into the peak-hold model, until the operator
    // leaves. Borrows the bus like capture; powers down + remounts on exit.
    void startBandSweep();
    void stopBandSweep();
    void stepBandSweep();

    // CC1101 jammer (slice-0017): arm the radio into TX once, emit one bit-banged
    // GDO0 burst per frame while the hold key is down, power down on exit.
    bool jamArm();     // cc1101ConfigureOok + EnterTx on the current preset
    void jamDisarm();  // cc1101Idle + remount; safe to call when not armed
    void stepJam();    // per-frame: emit a burst if the hold key is held

    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    bool available_;   // multi-radio cap resolved AND SD bus owned

    MenuState state_;
    MenuState lastHintState_;  // footer hints are refreshed only when state_ changes
    int radioSelection_;   // root: 0=CC1101 Sub-GHz 1=NRF24 2.4 GHz
    int mainSelection_;    // index into the CC1101 console main menu (MainItem)
    int mainScroll_;       // scroll window offset for the main menu
    int presetIndex_;      // index into the frequency-preset table
    int fskPresetIndex_;   // index into the FSK modem-preset table (slice-0019)
    int fskScroll_;        // scroll window offset for the FSK preset list
    bool captureIsFsk_;    // the in-flight capture is FSK (else OOK) — set on entry
    int actionSelection_;  // 0=Replay 1=Delete
    int fileSelection_;
    int fileScroll_;
    uint8_t captureStage_; // ramps so "Listening" paints before the blocking RX
    int jamModeSel_;       // 0=Carrier (CW) 1=Noise (modulated)
    bool jamArmed_;        // radio configured and held in TX
    bool jamEmitting_;     // hold key was down on the last frame (live indicator)

    std::vector<SignalEntry> signals_;
    rf::OokSignal pendingSignal_;  // just-captured, awaiting review then a name
    rf::OokSignal actionSignal_;   // selected saved signal, loaded for the detail view
    bool actionSignalValid_;       // false when the selected file failed to load
    TextInputPopup namePopup_;

    // NRF24 2.4 GHz analyzer state.
    rf::SpectrumScan spectrumScan_;  // per-channel occupancy tally
    uint8_t scanChannel_;            // next channel to sample this sweep
    bool scanActive_;                // radio is powered up and sweeping

    // CC1101 band-sweep state.
    rf::BandSweep bandSweep_;  // per-band peak-held RSSI
    uint8_t sweepBand_;        // next band preset to sample
    bool sweepActive_;         // sweep is running (owns the bus)

    ui::FooterHints footerHints_;

    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t LINE_HEIGHT = 14;
    static constexpr int VISIBLE_ROWS = 5;
    static constexpr uint32_t CAPTURE_WINDOW_MS = 6000;
    static constexpr uint8_t NAME_MAX_LEN = 24;

    // Jammer (slice-0017): one 80 ms bit-banged burst per frame, then the shared
    // GDO0 matrix row is released so the keyboard poll between bursts is clean and
    // key release is seen within a burst + a frame. Hold SPACE to emit.
    static constexpr uint32_t JAM_CHUNK_MS = 80;
    static constexpr char JAM_HOLD_KEY = ' ';

    // NRF24 sweep tuning. Per-channel RX dwell; channels sampled per update() so a
    // full 126-channel sweep spans a few frames, keeping the Back key responsive.
    static constexpr uint32_t NRF_DWELL_US = 120;
    static constexpr uint8_t NRF_CHANNELS_PER_UPDATE = 42;
    // Roll the occupancy window before the uint16 tally can overflow (~26
    // sweeps/s, so this is a couple of minutes) — keeps the bars showing recent
    // activity rather than a saturated all-time count.
    static constexpr uint16_t NRF_MAX_SWEEPS = 4000;
};

} // namespace adversary
