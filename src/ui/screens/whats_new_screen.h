/**
 * @file whats_new_screen.h
 * @brief "What's new" notes for the running release (ADR-0121, slice-0122).
 *
 * A scrollable block of static lines, the same shape as AboutScreen. ADR-0121 deferred
 * extracting a shared component "until the third such screen"; ADR-0133 is that trigger, so
 * this and AboutScreen now scroll through the shared ScrollableList in scroll-only (read-only
 * log) mode. Content comes from config/whats_new.h; the title line composes the running
 * config::VERSION. Reached two ways: auto-shown once on the first boot after a version change
 * (main.cpp), and on demand from Settings > What's New.
 */

#pragma once

#include <cstdint>
#include "config/config.h"
#include "../theme.h"
#include "../components/status_bar.h"
#include "../components/footer_hints.h"
#include "../components/list_scroll_model.h"
#include "../components/scrollable_list.h"
#include "screen_interface.h"

namespace adversary {

class WhatsNewScreen : public IScreen {
public:
    WhatsNewScreen();
    ~WhatsNewScreen() override;

    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }

    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { needsRedraw_ = true; }

    bool handleInput(char key) override;

    bool shouldExitToMenu() const override { return shouldExit_; }
    void resetExitFlag() override { shouldExit_ = false; }

    const char* getName() const override { return "What's New"; }
    ScreenId getId() const override { return ScreenId::WHATS_NEW; }

    void init() override;

private:
    void drawHeader(Canvas& canvas);
    void drawContent(Canvas& canvas);

    bool visible_;
    bool shouldExit_;
    bool needsRedraw_;

    // Scroll-only (read-only log) list model: static release notes with no selection (ADR-0133).
    ListScrollModel scrollModel_;

    ui::FooterHints footerHints_;

    static constexpr int16_t HEADER_HEIGHT = 20;
    static constexpr int16_t LINE_HEIGHT = 10;
};

} // namespace adversary
