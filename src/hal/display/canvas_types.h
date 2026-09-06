/**
 * @file canvas_types.h
 * @brief Platform-agnostic canvas type definitions
 * 
 * Provides a unified Canvas typedef that works across different
 * M5Stack devices and future Lilygo T-Embed support.
 * 
 * All screens use this Canvas type instead of M5Canvas directly,
 * enabling easy porting to other LovyanGFX-based platforms.
 */

#pragma once

#include "config/config.h"

#if defined(TARGET_CARDPUTER) || defined(TARGET_M5STICK)
    // M5Stack devices use M5GFX (LovyanGFX derivative)
    #include <M5GFX.h>
    namespace adversary {
        using Canvas = M5Canvas;
    }
#elif defined(TARGET_TEMBED)
    // Lilygo T-Embed uses LovyanGFX directly
    // NOTE: T-Embed support not yet implemented
    #include <LovyanGFX.hpp>
    namespace adversary {
        using Canvas = LGFX_Sprite;
    }
#else
    // Fallback for native tests / unknown platforms
    #ifdef ESP32
        #include <M5GFX.h>
        namespace adversary {
            using Canvas = M5Canvas;
        }
    #else
        // Native build stub
        namespace adversary {
            class Canvas {
            public:
                void fillScreen(uint32_t) {}
                void pushSprite(int, int) {}
                int width() const { return 320; }
                int height() const { return 240; }
                void setTextSize(int) {}
                void setTextColor(uint32_t) {}
                void setCursor(int, int) {}
                void print(const char*) {}
                void printf(const char*, ...) {}
                void fillRect(int, int, int, int, uint32_t) {}
                void drawRect(int, int, int, int, uint32_t) {}
                void drawLine(int, int, int, int, uint32_t) {}
                void fillRoundRect(int, int, int, int, int, uint32_t) {}
                void drawRoundRect(int, int, int, int, int, uint32_t) {}
            };
        }
    #endif
#endif
