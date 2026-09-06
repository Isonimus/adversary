/**
 * @file ir_record_screen.h
 * @brief IR learn-and-replay console for the multi-radio cap (slice-0004).
 *
 * The IR analog of RadioScreen: Learn (capture → name → save), Saved codes
 * (list → Replay / Delete), and a TX-source toggle (built-in emitter ↔ cap
 * array, persisted in WirelessSettings). Built to docs/UI-STYLE from the start —
 * per-state footer hints + dispatch, BG_SELECTED highlight, standard keys,
 * state-enum sub-navigation (no view stack).
 *
 * Unlike RadioScreen, this screen is only *partially* gated on the cap: IR
 * receive lives solely on the cap, so Learn needs it, but replay of saved codes
 * over the always-present built-in emitter works with no cap attached.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/text_input_popup.h"
#include "../../modules/ir/ir_signal.h"
#include "screen_interface.h"

namespace adversary {

class IrRecordScreen : public IScreen {
public:
    IrRecordScreen();
    ~IrRecordScreen() override;

    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }

    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }

    bool handleInput(char key) override;

    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }

    const char* getName() const override { return "IR Record"; }
    ScreenId getId() const override { return ScreenId::INFRARED_RECORD; }

    void init() override;
    void deinit();

private:
    // Sub-levels (no per-screen view stack in the framework — the CapturesScreen
    // idiom). `` ` `` pops one level; only MAIN sets the exit flag.
    enum class MenuState : uint8_t {
        MAIN,             // Learn / Saved codes / TX source
        CAPTURING,        // IR RX window is open (blocking)
        CAPTURE_REVIEW,   // just-captured: pulse-strip preview, keep or discard
        NAMING,           // TextInputPopup collecting a name for the capture
        CODE_LIST,        // saved .ir files
        CODE_ACTION,      // Replay / Delete for the selected file
        REPLAY_CONFIRM,   // "transmit?" gate (replay emits IR)
        DELETE_CONFIRM,   // "delete?" gate
        TX_SOURCE_SELECT, // Built-in emitter / cap array
    };

    struct CodeEntry {
        char name[40];   // basename incl. ".ir"
        uint32_t size;
    };

    // Render helpers (one per state).
    void drawMain(Canvas& canvas);
    void drawCapturing(Canvas& canvas);
    void drawCaptureReview(Canvas& canvas);
    void drawCodeList(Canvas& canvas);
    void drawCodeAction(Canvas& canvas);
    void drawConfirm(Canvas& canvas, const char* question);
    void drawTxSourceSelect(Canvas& canvas);

    // Draws a signal's encoding summary (RAW dimmed vs decoded) plus, for a raw
    // train, its static pulse-strip preview. Shared by the capture-review and
    // code-detail screens so both read a captured code the same way.
    void drawEncodingAndStrip(Canvas& canvas, const ir::IrSignal& sig,
                              int16_t x, int16_t y, int16_t w);

    // Logic helpers.
    void updateFooterHints();  // footer hints reflecting the current state
    void loadCodeList();
    void loadActionSignal();  // deserialize the selected code for the detail view
    void startCapture();
    void performCapture();
    void promptForName();  // open the name popup for the reviewed capture
    void saveCapturedCode(const char* name);
    void replaySelected();
    void deleteSelected();
    void fullPath(const char* name, char* out, size_t outSize) const;
    void describeSignal(const ir::IrSignal& sig, char* out, size_t outSize) const;
    ir::IrTxSource txSource() const;   // current persisted TX source

    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    bool captureAvailable_;  // multi-radio cap resolved (IR receiver present)

    MenuState state_;
    MenuState lastHintState_;  // footer hints refreshed only when state_ changes
    int mainSelection_;      // 0=Learn 1=Saved 2=TX source
    int actionSelection_;    // 0=Replay 1=Delete
    int fileSelection_;
    int fileScroll_;
    int txSelection_;        // index while in TX_SOURCE_SELECT
    uint8_t captureStage_;   // ramps so "Listening" paints before the blocking RX

    std::vector<CodeEntry> codes_;
    ir::IrSignal pendingSignal_;  // just-captured, awaiting review then a name
    ir::IrSignal actionSignal_;   // selected saved code, loaded for the detail view
    bool actionSignalValid_;      // false when the selected file failed to load
    TextInputPopup namePopup_;

    ui::FooterHints footerHints_;

    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t ROW_HEIGHT = theme::LIST_ITEM_HEIGHT;  // 20, the shared token
    static constexpr int VISIBLE_ROWS = 5;
    static constexpr uint32_t CAPTURE_WINDOW_MS = 8000;
    static constexpr uint8_t NAME_MAX_LEN = 24;
};

} // namespace adversary
