#pragma once

#ifndef ESP32

#include <stdint.h>

/**
 * @brief Minimal mock for FastLED to allow native builds
 */
struct CRGB {
    uint8_t r, g, b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    
    static const uint32_t Black = 0;
};

class CFastLED {
public:
    template<int TYPE, int PIN, int ORDER>
    void addLeds(CRGB* leds, int count) {}
    void setBrightness(uint8_t b) {}
    void show() {}
};

extern CFastLED FastLED;

#define WS2812 0
#define GRB 0

#endif
