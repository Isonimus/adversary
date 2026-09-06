#pragma once

/**
 * @file display_hal.h
 * @brief Display Hardware Abstraction Layer interface
 */

#include <stdint.h>

namespace adversary {
namespace hal {

/**
 * @brief Display interface - abstract base for all display implementations
 */
class IDisplay {
public:
    virtual ~IDisplay() = default;

    /**
     * @brief Initialize the display
     */
    virtual void init() = 0;

    /**
     * @brief Clear display with background color
     */
    virtual void clear() = 0;

    /**
     * @brief Clear display with specific color
     */
    virtual void clear(uint16_t color) = 0;

    /**
     * @brief Set display brightness
     * @param level Brightness level (0-255)
     */
    virtual void setBrightness(uint8_t level) = 0;

    /**
     * @brief Draw a pixel
     */
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

    /**
     * @brief Draw a line
     */
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) = 0;

    /**
     * @brief Draw a rectangle outline
     */
    virtual void drawRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;

    /**
     * @brief Draw a filled rectangle
     */
    virtual void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;

    /**
     * @brief Draw a rounded rectangle outline
     */
    virtual void drawRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h, 
                               uint8_t radius, uint16_t color) = 0;

    /**
     * @brief Draw a filled rounded rectangle
     */
    virtual void fillRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                               uint8_t radius, uint16_t color) = 0;

    /**
     * @brief Draw a circle outline
     */
    virtual void drawCircle(int16_t x, int16_t y, uint16_t r, uint16_t color) = 0;

    /**
     * @brief Draw a filled circle
     */
    virtual void fillCircle(int16_t x, int16_t y, uint16_t r, uint16_t color) = 0;

    /**
     * @brief Set text cursor position
     */
    virtual void setCursor(int16_t x, int16_t y) = 0;

    /**
     * @brief Set text color
     */
    virtual void setTextColor(uint16_t color) = 0;

    /**
     * @brief Set text color with background
     */
    virtual void setTextColor(uint16_t color, uint16_t bgColor) = 0;

    /**
     * @brief Set text size multiplier
     */
    virtual void setTextSize(uint8_t size) = 0;

    /**
     * @brief Draw text at cursor position
     */
    virtual void print(const char* text) = 0;

    /**
     * @brief Draw text at specific position
     */
    virtual void drawText(int16_t x, int16_t y, const char* text, uint16_t color) = 0;

    /**
     * @brief Draw text centered horizontally
     */
    virtual void drawCenteredText(int16_t y, const char* text, uint16_t color) = 0;

    /**
     * @brief Get text width in pixels
     */
    virtual uint16_t getTextWidth(const char* text) = 0;

    /**
     * @brief Get text height in pixels
     */
    virtual uint16_t getTextHeight() = 0;

    /**
     * @brief Push changes to display (double buffering)
     */
    virtual void update() = 0;

    /**
     * @brief Get display width
     */
    virtual uint16_t getWidth() const = 0;

    /**
     * @brief Get display height
     */
    virtual uint16_t getHeight() const = 0;
};

/**
 * @brief Factory function to create platform-specific display
 */
IDisplay* createDisplay();

} // namespace hal
} // namespace adversary
