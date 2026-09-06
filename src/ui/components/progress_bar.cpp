/**
 * @file progress_bar.cpp
 * @brief Progress bar component implementation
 */

#include "progress_bar.h"
#include "ui/theme.h"

namespace adversary {
namespace ui {

ProgressBar::ProgressBar()
    : m_progress(0.0f)
    , m_x(0)
    , m_y(0)
    , m_width(100)
    , m_height(10)
{
    // Default style
    m_style.backgroundColor = theme::BG_SECONDARY();
    m_style.fillColor = theme::ACCENT();
    m_style.borderColor = theme::TEXT_SECONDARY();
    m_style.borderWidth = 1;
    m_style.cornerRadius = 2;
}

void ProgressBar::setProgress(float percent) {
    if (percent < 0.0f) {
        m_progress = 0.0f;
    } else if (percent > 1.0f) {
        m_progress = 1.0f;
    } else {
        m_progress = percent;
    }
}

void ProgressBar::setBounds(int16_t x, int16_t y, uint16_t width, uint16_t height) {
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
}

void ProgressBar::setStyle(const ProgressBarStyle& style) {
    m_style = style;
}

uint16_t ProgressBar::getFillWidth() const {
    // Account for border width on both sides
    uint16_t innerWidth = m_width - (2 * m_style.borderWidth);
    return static_cast<uint16_t>(innerWidth * m_progress);
}

} // namespace ui
} // namespace adversary
