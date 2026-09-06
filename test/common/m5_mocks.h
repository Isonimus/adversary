#pragma once

#ifndef ESP32

#include <stdint.h>
#include <vector>

/**
 * @brief Simple mock for M5Canvas to test BitmapRemapper
 */
class M5Canvas {
public:
    M5Canvas() {}
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) {
        // Minimal mock
    }
};

/**
 * @brief Mock for M5Unified
 */
class M5Unified {
public:
    void begin() {}
};

extern M5Unified M5;

#endif
