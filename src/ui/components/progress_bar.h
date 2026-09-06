#pragma once

/**
 * @file progress_bar.h
 * @brief Reusable progress bar UI component
 */

#include <stdint.h>

namespace adversary {
namespace ui {

/**
 * @brief Progress bar rendering data
 */
struct ProgressBarStyle {
    uint16_t backgroundColor;
    uint16_t fillColor;
    uint16_t borderColor;
    uint8_t borderWidth;
    uint8_t cornerRadius;
};

/**
 * @brief Progress bar component
 * Pure data structure - rendering is done by display
 */
class ProgressBar {
public:
    ProgressBar();

    /**
     * @brief Set progress value
     * @param percent Progress value from 0.0 to 1.0
     */
    void setProgress(float percent);

    /**
     * @brief Get current progress
     */
    float getProgress() const { return m_progress; }

    /**
     * @brief Set position and dimensions
     */
    void setBounds(int16_t x, int16_t y, uint16_t width, uint16_t height);

    /**
     * @brief Set style
     */
    void setStyle(const ProgressBarStyle& style);

    /**
     * @brief Get style
     */
    const ProgressBarStyle& getStyle() const { return m_style; }

    // Getters for rendering
    int16_t getX() const { return m_x; }
    int16_t getY() const { return m_y; }
    uint16_t getWidth() const { return m_width; }
    uint16_t getHeight() const { return m_height; }

    /**
     * @brief Calculate fill width based on progress
     */
    uint16_t getFillWidth() const;

private:
    float m_progress;
    int16_t m_x;
    int16_t m_y;
    uint16_t m_width;
    uint16_t m_height;
    ProgressBarStyle m_style;
};

} // namespace ui
} // namespace adversary
