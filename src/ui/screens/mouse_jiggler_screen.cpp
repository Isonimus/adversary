#include "mouse_jiggler_screen.h"
#include "ui/theme.h"
#include "config/config.h"

namespace adversary {

#if defined(TARGET_CARDPUTER)
// Small net-zero displacement pattern (right, down, left, up) so the cursor
// doesn't visibly drift across the screen over a long session.
static constexpr int8_t JIGGLE_PATTERN[4][2] = { {3,0}, {0,3}, {-3,0}, {0,-3} };
#endif

MouseJigglerScreen::MouseJigglerScreen()
    : m_state(State::CONFIRM)
    , m_active(false)
    , m_shouldExit(false)
    , m_needsRedraw(true)
    , m_activatingStart(0)
    , m_lastJiggle(0)
    , m_lastUpdate(0)
    , m_jiggleCount(0)
    , m_patternIdx(0)
{}

void MouseJigglerScreen::show() {
    m_active      = true;
    m_shouldExit  = false;
    m_needsRedraw = true;
    m_jiggleCount = 0;

#if defined(TARGET_CARDPUTER)
    if (UsbHidDevices::getInstance().isReady()) {
        m_state      = State::RUNNING;
        m_lastJiggle = millis();
        m_footer.setHints({{'`', "Stop", true}});
    } else {
        m_state = State::CONFIRM;
        m_footer.setHints({{'\n', "Activate", true}, {'`', "Back", true}});
    }
#else
    m_state = State::CONFIRM;
    m_footer.setHints({{'`', "Back", true}});
#endif
}

void MouseJigglerScreen::hide() {
    m_active = false;
}

void MouseJigglerScreen::update() {
    if (!m_active) return;

#if defined(TARGET_CARDPUTER)
    if (m_state == State::ACTIVATING) {
        if (millis() - m_activatingStart >= ACTIVATE_MS) {
            m_state      = State::RUNNING;
            m_lastJiggle = millis();
            m_footer.setHints({{'`', "Stop", true}});
            m_needsRedraw = true;
        } else if (millis() - m_lastUpdate > 100) {
            m_lastUpdate  = millis();
            m_needsRedraw = true;
        }
        return;
    }

    if (m_state != State::RUNNING) return;

    if (millis() - m_lastJiggle >= JIGGLE_INTERVAL_MS) {
        jiggle();
        m_lastJiggle  = millis();
        m_needsRedraw = true;
    } else if (millis() - m_lastUpdate > 200) {
        m_lastUpdate  = millis();
        m_needsRedraw = true;  // refresh the "next jiggle in Xs" countdown
    }
#endif
}

void MouseJigglerScreen::render(Canvas& canvas) {
    if (!m_needsRedraw) return;
    m_needsRedraw = false;
    canvas.fillScreen(theme::BG_PRIMARY());

    switch (m_state) {
        case State::CONFIRM:    drawConfirm(canvas);    break;
        case State::ACTIVATING: drawActivating(canvas); break;
        case State::RUNNING:    drawRunning(canvas);    break;
    }
    m_footer.render(canvas);
}

void MouseJigglerScreen::drawConfirm(Canvas& canvas) {
#if defined(TARGET_CARDPUTER)
    ui::StatusBar::render(canvas, "MOUSE JIGGLER", "WARNING");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("USB HID Mode", cx, 28);

    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.drawString("Serial console will be", cx, 50);
    canvas.drawString("disabled until reboot.", cx, 66);

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("Prevents idle lock/sleep.", cx, 84);

    canvas.setTextDatum(top_left);
#else
    ui::StatusBar::render(canvas, "MOUSE JIGGLER", "N/A");
    canvas.setTextDatum(top_center);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("USB HID not available", config::SCREEN_WIDTH/2, 50);
    canvas.drawString("on this device.", config::SCREEN_WIDTH/2, 66);
    canvas.setTextDatum(top_left);
#endif
}

void MouseJigglerScreen::drawActivating(Canvas& canvas) {
#if defined(TARGET_CARDPUTER)
    ui::StatusBar::render(canvas, "MOUSE JIGGLER", "WAIT");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("Activating USB HID...", cx, 38);

    uint32_t elapsed = millis() - m_activatingStart;
    if (elapsed > ACTIVATE_MS) elapsed = ACTIVATE_MS;
    const int barX = 20;
    const int barW = config::SCREEN_WIDTH - 40;
    const int barY = 68;
    canvas.fillRect(barX, barY, barW, 8, theme::BG_TERTIARY());
    canvas.fillRect(barX, barY, (int)(elapsed * barW / ACTIVATE_MS), 8, theme::ACCENT());

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString("Serial console offline", cx, 86);
    canvas.setTextDatum(top_left);
#endif
}

void MouseJigglerScreen::drawRunning(Canvas& canvas) {
    ui::StatusBar::render(canvas, "MOUSE JIGGLER", "ACTIVE");

    int cx = config::SCREEN_WIDTH / 2;
    canvas.setTextDatum(top_center);

    canvas.setTextColor(theme::ACCENT());
    canvas.drawString("Jiggling...", cx, 30);

#if defined(TARGET_CARDPUTER)
    uint32_t elapsed   = millis() - m_lastJiggle;
    uint32_t remaining = (elapsed >= JIGGLE_INTERVAL_MS) ? 0 : (JIGGLE_INTERVAL_MS - elapsed) / 1000;
    char line[32];
    snprintf(line, sizeof(line), "Next move in %lus", (unsigned long)remaining);
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.drawString(line, cx, 55);

    snprintf(line, sizeof(line), "Moves: %lu", (unsigned long)m_jiggleCount);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.drawString(line, cx, 75);
#endif

    canvas.setTextDatum(top_left);
}

bool MouseJigglerScreen::handleInput(char key) {
    m_needsRedraw = true;

    if (m_state == State::ACTIVATING) return true;

    if (m_state == State::CONFIRM) {
        if (key == '\n') {
#if defined(TARGET_CARDPUTER)
            activateHid();
#else
            m_shouldExit = true;
#endif
        } else if (key == '`') {
            m_shouldExit = true;
        }
        return true;
    }

    // RUNNING
    if (key == '`') {
        m_shouldExit = true;
        return true;
    }
    return false;
}

void MouseJigglerScreen::activateHid() {
#if defined(TARGET_CARDPUTER)
    UsbHidDevices::getInstance().begin();
    m_activatingStart = millis();
    m_state           = State::ACTIVATING;
    m_footer.setHints({});
    m_needsRedraw = true;
#endif
}

void MouseJigglerScreen::jiggle() {
#if defined(TARGET_CARDPUTER)
    auto& mouse = UsbHidDevices::getInstance().mouse();
    mouse.move(JIGGLE_PATTERN[m_patternIdx][0], JIGGLE_PATTERN[m_patternIdx][1]);
    m_patternIdx = (m_patternIdx + 1) % 4;
    m_jiggleCount++;
#endif
}

} // namespace adversary
