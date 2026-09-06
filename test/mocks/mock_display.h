#pragma once

/**
 * @file mock_display.h
 * @brief Mock display implementation for unit testing
 */

#include "hal/display/display_hal.h"
#include <string>
#include <vector>

namespace adversary {
namespace test {

/**
 * @brief Mock display for testing UI components
 */
class MockDisplay : public hal::IDisplay {
public:
    MockDisplay(uint16_t width = 240, uint16_t height = 135)
        : m_width(width)
        , m_height(height)
        , m_brightness(128)
        , m_textSize(1)
        , m_cursorX(0)
        , m_cursorY(0)
        , m_textColor(0xFFFF)
        , m_bgColor(0x0000)
    {}

    void init() override { initCalled = true; }
    void clear() override { clearCalled = true; clearCount++; }
    void clear(uint16_t color) override { 
        clearCalled = true; 
        clearCount++; 
        lastClearColor = color;
    }
    
    void setBrightness(uint8_t level) override { m_brightness = level; }
    
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        pixels.push_back({x, y, color});
    }
    
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override {
        lines.push_back({x0, y0, x1, y1, color});
    }
    
    void drawRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) override {
        rects.push_back({x, y, w, h, color, false});
    }
    
    void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) override {
        rects.push_back({x, y, w, h, color, true});
    }
    
    void drawRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h, 
                       uint8_t radius, uint16_t color) override {
        roundRects.push_back({x, y, w, h, radius, color, false});
    }
    
    void fillRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                       uint8_t radius, uint16_t color) override {
        roundRects.push_back({x, y, w, h, radius, color, true});
    }
    
    void drawCircle(int16_t x, int16_t y, uint16_t r, uint16_t color) override {
        circles.push_back({x, y, r, color, false});
    }
    
    void fillCircle(int16_t x, int16_t y, uint16_t r, uint16_t color) override {
        circles.push_back({x, y, r, color, true});
    }
    
    void setCursor(int16_t x, int16_t y) override {
        m_cursorX = x;
        m_cursorY = y;
    }
    
    void setTextColor(uint16_t color) override { m_textColor = color; }
    void setTextColor(uint16_t color, uint16_t bgColor) override {
        m_textColor = color;
        m_bgColor = bgColor;
    }
    
    void setTextSize(uint8_t size) override { m_textSize = size; }
    
    void print(const char* text) override {
        texts.push_back({m_cursorX, m_cursorY, text, m_textColor, m_textSize});
    }
    
    void drawText(int16_t x, int16_t y, const char* text, uint16_t color) override {
        texts.push_back({x, y, text, color, m_textSize});
    }
    
    void drawCenteredText(int16_t y, const char* text, uint16_t color) override {
        int16_t x = (m_width - getTextWidth(text)) / 2;
        texts.push_back({x, y, text, color, m_textSize});
    }
    
    uint16_t getTextWidth(const char* text) override {
        return strlen(text) * 6 * m_textSize;  // Approximate
    }
    
    uint16_t getTextHeight() override {
        return 8 * m_textSize;  // Approximate
    }
    
    void update() override { updateCount++; }
    
    uint16_t getWidth() const override { return m_width; }
    uint16_t getHeight() const override { return m_height; }
    
    // Test inspection helpers
    void resetCounters() {
        initCalled = false;
        clearCalled = false;
        clearCount = 0;
        updateCount = 0;
        pixels.clear();
        lines.clear();
        rects.clear();
        roundRects.clear();
        circles.clear();
        texts.clear();
    }
    
    // Test state
    bool initCalled = false;
    bool clearCalled = false;
    int clearCount = 0;
    int updateCount = 0;
    uint16_t lastClearColor = 0;
    
    // Recorded operations
    struct Pixel { int16_t x, y; uint16_t color; };
    struct Line { int16_t x0, y0, x1, y1; uint16_t color; };
    struct Rect { int16_t x, y; uint16_t w, h; uint16_t color; bool filled; };
    struct RoundRect { int16_t x, y; uint16_t w, h; uint8_t radius; uint16_t color; bool filled; };
    struct Circle { int16_t x, y; uint16_t r; uint16_t color; bool filled; };
    struct Text { int16_t x, y; std::string text; uint16_t color; uint8_t size; };
    
    std::vector<Pixel> pixels;
    std::vector<Line> lines;
    std::vector<Rect> rects;
    std::vector<RoundRect> roundRects;
    std::vector<Circle> circles;
    std::vector<Text> texts;

private:
    uint16_t m_width;
    uint16_t m_height;
    uint8_t m_brightness;
    uint8_t m_textSize;
    int16_t m_cursorX;
    int16_t m_cursorY;
    uint16_t m_textColor;
    uint16_t m_bgColor;
};

} // namespace test
} // namespace adversary
