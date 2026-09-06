/**
 * @file ir_tvbgone_screen.h
 * @brief Screen for universal TV-B-Gone power-off blaster
 */

#pragma once

#include "ui/screens/screen_interface.h"
#include "ui/components/footer_hints.h"
#include "core/event_bus.h"
#include "modules/ir/tvbgone_blaster.h"

namespace adversary {

class IrTvBGoneScreen : public IScreen {
public:
    IrTvBGoneScreen();
    virtual ~IrTvBGoneScreen();

    void show() override;
    void hide() override;
    void update() override;
    void render(Canvas& canvas) override;
    void requestRedraw() override { m_needsRedraw = true; }
    bool handleInput(char key) override;
    
    const char* getName() const override { return "TV-B-Gone"; }
    ScreenId getId() const override { return ScreenId::MENU; } // Will need a specific one if registered
    bool isVisible() const override { return m_active; }
    bool shouldExitToMenu() const override { return m_shouldExit; }
    void resetExitFlag() override { m_shouldExit = false; }

private:
    ir::TvBGoneBlaster& m_blaster;
    
    bool m_active;
    bool m_shouldExit;
    bool m_needsRedraw;
    uint32_t m_lastUpdate;
    
    ui::FooterHints footerHints_;
    ir::Region m_selectedRegion;
    uint32_t completionHandlerId_ = 0;  // IR_TRANSMISSION_COMPLETED subscription

    void drawHeader(Canvas& canvas);
    void drawDashboard(Canvas& canvas);
    void drawBlasting(Canvas& canvas);
};

} // namespace adversary
