/**
 * @file bitmap_remapper.h
 * @brief Utility to remap 1-bit monochrome BMPs to theme colors
 */

#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include "../ui/theme.h"

namespace adversary {
namespace utils {

/**
 * @brief Remaps 1-bit BMP pixels to theme colors and draws to canvas
 * 
 * Logic:
 * - Bit 0 (Background/Dark in BMP) -> theme::BG_PRIMARY()
 * - Bit 1 (Foreground/Light in BMP) -> theme::ACCENT()
 * 
 * NOTE: Some displays or buffers may require inversion.
 * Standard BMP 1-bit format:
 * - 48x48 1-bit
 * - Bytes per row: (Width + 31) / 32 * 4 
 * - For 48x48: (48+31)/32 * 4 = 2 * 4 = 8 bytes per row
 */
class BitmapRemapper {
public:
    static void drawThemeIcon(M5Canvas& canvas, const uint8_t* bmpData, int16_t x, int16_t y, uint16_t width, uint16_t height, float scale = 1.0f, uint16_t accentColor = 0xFFFF) {
        // Use provided accentColor or default to theme::ACCENT()
        uint16_t accent = (accentColor == 0xFFFF) ? theme::ACCENT() : accentColor;
        
        // Read offset from BMP header (bytes 10-13)
        uint32_t offset = (uint32_t)bmpData[10] | ((uint32_t)bmpData[11] << 8) | 
                         ((uint32_t)bmpData[12] << 16) | ((uint32_t)bmpData[13] << 24);

        int16_t tW = (int16_t)(width * scale);
        int16_t tH = (int16_t)(height * scale);
        
        // Prevent div by zero
        if (tW <= 0 || tH <= 0) return;
        
        uint16_t bytesPerRow = (width + 31) / 32 * 4;
        uint16_t bg = theme::BG_PRIMARY();
        
        // Iterate TARGET pixels to avoid gaps
        for (int16_t tr = 0; tr < tH; tr++) {
             // Nearest neighbor mapping
             int16_t sr = tr * height / tH;
             if (sr >= height) sr = height - 1;
             
             // BMP is bottom-up
             int16_t bmpRow = height - 1 - sr;
             const uint8_t* rowPtr = bmpData + offset + (bmpRow * bytesPerRow);
             
             for (int16_t tc = 0; tc < tW; tc++) {
                 int16_t sc = tc * width / tW;
                 
                 uint8_t byte = rowPtr[sc >> 3];
                 // Standard BMP 1-bit: bit 7 is leftmost pixel
                 bool bit = (byte >> (7 - (sc & 7))) & 1;
                 
                 uint16_t color = bit ? accent : bg;
                 canvas.drawPixel(x + tc, y + tr, color);
             }
        }
    }

    /**
     * @brief Remaps a standard 1-bit BMP buffer into a 16-bit color buffer
     * @param invert If true, bitwise-NOT is applied to all colors. Helpful for some IPS panels.
     */
    static void remapToBuffer(const uint8_t* bmpData, uint16_t* outBuffer, uint16_t width, uint16_t height, uint16_t accentColor, uint16_t bgColor, bool invert = false) {
        // Safe read of BMP offset (Endian-safe)
        uint32_t offset = (uint32_t)bmpData[10] | ((uint32_t)bmpData[11] << 8) | 
                         ((uint32_t)bmpData[12] << 16) | ((uint32_t)bmpData[13] << 24);
        
        uint16_t bytesPerRow = (width + 31) / 32 * 4;
        
        uint16_t accent = invert ? ~accentColor : accentColor;
        uint16_t bg = invert ? ~bgColor : bgColor;

        for (int16_t r = 0; r < height; r++) {
            // BMP rows are bottom-up
            int16_t bmpRow = height - 1 - r;
            const uint8_t* rowPtr = bmpData + offset + (bmpRow * bytesPerRow);
            
            for (int16_t c = 0; c < width; c++) {
                uint8_t byte = rowPtr[c >> 3];
                // Bit 7 is leftmost pixel
                bool bit = (byte >> (7 - (c & 7))) & 1;
                
                // standard: 1=accent, 0=bg
                outBuffer[r * width + c] = bit ? accent : bg;
            }
        }
    }
};

} // namespace utils
} // namespace adversary
