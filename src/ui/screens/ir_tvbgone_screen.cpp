/**
 * @file ir_tvbgone_screen.cpp
 * @brief IrTvBGoneScreen implementation
 */

#include "ir_tvbgone_screen.h"
#include "ui/theme.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast_manager.h"
#include "config/config.h"

namespace adversary {

IrTvBGoneScreen::IrTvBGoneScreen()
    : m_blaster(ir::TvBGoneBlaster::getInstance())
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_lastUpdate(0)
    , m_selectedRegion(ir::Region::NORTH_AMERICA) {
}

IrTvBGoneScreen::~IrTvBGoneScreen() {
    if (completionHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(completionHandlerId_);
        completionHandlerId_ = 0;
    }
}

void IrTvBGoneScreen::show() {
    m_active = true;
    m_shouldExit = false;
    m_needsRedraw = true;
    
    m_blaster.init();
    
    footerHints_.setHints({
        {'\n', "Blast", true},
        {'r', "Region", true}
    });
    footerHints_.setFocus(false);
    
    // Subscribe to IR completion events
    if (completionHandlerId_ == 0) {
        completionHandlerId_ = EventBus::getInstance().subscribe(
            EventType::IR_TRANSMISSION_COMPLETED,
            [this](const EventData&) {
                m_needsRedraw = true;
                showSuccessToast("Blast complete!");
                footerHints_.setHints({
                    {'\n', "Blast", true},
                    {'r', "Region", true}
                });
            }
        );
    }
}

void IrTvBGoneScreen::hide() {
    if (completionHandlerId_ != 0) {
        EventBus::getInstance().unsubscribe(completionHandlerId_);
        completionHandlerId_ = 0;
    }
    m_active = false;
    m_blaster.stop();
}

void IrTvBGoneScreen::update() {
    if (!m_active) return;

    if (m_blaster.isBlasting()) {
        m_blaster.update();
        m_needsRedraw = true;
    }

    if (millis() - m_lastUpdate > 200) {
        m_lastUpdate = millis();
        m_needsRedraw = true;
    }
}

void IrTvBGoneScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;

    canvas.fillScreen(theme::BG_PRIMARY());
    
    ui::StatusBar::render(canvas, "INFRARED", m_blaster.isBlasting() ? "BLASTING" : "READY");
    
    if (m_blaster.isBlasting()) {
        drawBlasting(canvas);
    } else {
        drawDashboard(canvas);
    }
    
    footerHints_.render(canvas);
    m_needsRedraw = false;
}

void IrTvBGoneScreen::drawDashboard(Canvas& canvas) {
    int y = config::STATUS_BAR_HEIGHT + 20;
    
    canvas.setTextColor(theme::ACCENT());
    canvas.setTextSize(2);
    canvas.setCursor(20, y);
    canvas.print("TV-B-Gone Blaster");
    
    y += 30;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(20, y);
    canvas.print("Selected Region:");
    
    y += 15;
    canvas.setCursor(40, y);
    canvas.setTextColor(m_selectedRegion == ir::Region::NORTH_AMERICA ? theme::SUCCESS() : theme::TEXT_SECONDARY());
    canvas.printf("%s North America", m_selectedRegion == ir::Region::NORTH_AMERICA ? ">" : " ");
    
    y += 15;
    canvas.setCursor(40, y);
    canvas.setTextColor(m_selectedRegion == ir::Region::EUROPE ? theme::SUCCESS() : theme::TEXT_SECONDARY());
    canvas.printf("%s Europe", m_selectedRegion == ir::Region::EUROPE ? ">" : " ");
    
    y += 25;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(20, y);
    canvas.print("Universal 'Power OFF' sequence");
}

void IrTvBGoneScreen::drawBlasting(Canvas& canvas) {
    int y = 60;
    canvas.setTextColor(theme::ERROR());
    canvas.setTextSize(2);
    int16_t txtWidth = 144; // Approx "BLASTING..."
    canvas.setCursor((config::SCREEN_WIDTH - txtWidth) / 2, y);
    
    // Pulsing text
    if ((millis() / 300) % 2 == 0) {
        canvas.print("BLASTING...");
    }
    
    y += 30;
    // Progress Bar
    int barW = 180;
    int barH = 12;
    int x = (config::SCREEN_WIDTH - barW) / 2;
    canvas.drawRect(x, y, barW, barH, theme::TEXT_SECONDARY());
    
    float progress = m_blaster.getProgress();
    canvas.fillRect(x + 2, y + 2, (barW - 4) * progress, barH - 4, theme::ERROR());
    
    y += 20;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("Code: %d / %d", m_blaster.getCurrentIndex(), m_blaster.getTotalCodes());
}

bool IrTvBGoneScreen::handleInput(char key) {
    char action = 0;
    if (footerHints_.handleInputWithDispatch(key, action)) {
        if (action) return handleInput(action);
        m_needsRedraw = true;
        return true;
    }

    if (key == '`') {
        if (m_blaster.isBlasting()) {
            m_blaster.stop();
            m_needsRedraw = true;
            footerHints_.setHints({
                {'\n', "Blast", true},
                {'r', "Region", true}
            });
        } else {
            m_shouldExit = true;
        }
        return true;
    }

    if (!m_blaster.isBlasting()) {
        if (key == 'r' || key == ';' || key == '.') {
            m_selectedRegion = (m_selectedRegion == ir::Region::NORTH_AMERICA) ? ir::Region::EUROPE : ir::Region::NORTH_AMERICA;
            m_needsRedraw = true;
            return true;
        }
        
        if (key == '\n') {
            m_blaster.start(m_selectedRegion);
            m_needsRedraw = true;
            footerHints_.setHints({
                {'\n', "Stop", true}
            });
            return true;
        }
    } else {
        if (key == '\n') {
            m_blaster.stop();
            m_needsRedraw = true;
            footerHints_.setHints({
                {'\n', "Blast", true},
                {'r', "Region", true}
            });
            return true;
        }
    }

    return false;
}

} // namespace adversary
