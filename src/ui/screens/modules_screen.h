/**
 * @file modules_screen.h
 * @brief Live peripheral inventory + hot-swap Re-scan (slice-0018).
 *
 * Boot probes the expansion cap, RFID, and GPS once; a module attached later is
 * invisible until reboot. This screen shows what is attached right now and lets
 * the operator re-run detection on demand (redetectModules()) after seating a
 * module while the bus was idle. It also surfaces the cap-override control (the
 * same value the Settings screen edits) because forcing cap presence is exactly
 * what an operator does here when a half-seated cap probes falsely.
 */

#pragma once

#include <cstdint>

#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "screen_interface.h"

namespace adversary {

class ModulesScreen : public IScreen {
public:
    ModulesScreen();
    ~ModulesScreen() override;

    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }

    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }

    bool handleInput(char key) override;

    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }

    const char* getName() const override { return "Modules"; }
    ScreenId getId() const override { return ScreenId::MODULES; }

    void init() override;
    void deinit();

private:
    // Re-scan is synchronous (it blocks on the GPS UART probe), so it runs one
    // frame AFTER a "Scanning..." banner has painted — otherwise the freeze would
    // swallow the only feedback the operator gets.
    enum class Phase : uint8_t { IDLE, SCAN_PENDING };

    void drawHeader(Canvas& canvas);
    void drawContent(Canvas& canvas);
    void drawRow(Canvas& canvas, int16_t y, const char* label, bool present,
                 const char* detail);
    void requestRescan(const char* toastMsg);  // show a toast + defer the blocking probe
    void cycleOverride();  // Auto -> ForceNone -> ForceMultiRadio -> Auto, then re-scan

    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;
    Phase phase_;
    bool scanFramePainted_;  // a frame has painted since the request, so the toast
                             // has reached the LCD — the blocking probe may run now

    ui::FooterHints footerHints_;

    static constexpr int16_t HEADER_HEIGHT = 20;      // status-bar band (UI-STYLE)
    static constexpr int16_t BODY_TOP = HEADER_HEIGHT + 6;
};

} // namespace adversary
