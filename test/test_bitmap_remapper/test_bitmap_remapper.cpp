/**
 * @file test_bitmap_remapper.cpp
 * @brief Unit tests for BitmapRemapper utility
 */

#include <unity.h>
#include "utils/bitmap_remapper.h"
#include <vector>
#include <cstring>

#ifndef ESP32
#include "../common/m5_mocks.h"
#endif

using namespace adversary::utils;

// Mock canvas that records pixels
class TestCanvas : public M5Canvas {
public:
    struct Pixel {
        int16_t x, y;
        uint16_t color;
    };
    std::vector<Pixel> pixels;

    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        pixels.push_back({x, y, color});
    }
    
    void clear() { pixels.clear(); }
};

// Global M5 mock instance
#ifndef ESP32
M5Unified M5;
#endif

void test_remap_to_buffer() {
    // Minimal 2x2 1-bit BMP
    // Header (14) + DIB (40) + Pixels (8 - 4 bytes per row padded)
    // Actually, we need to provide enough bytes for the offset
    uint8_t mockBmp[100];
    memset(mockBmp, 0, 100);
    
    // BMP Header
    mockBmp[0] = 'B'; mockBmp[1] = 'M';
    mockBmp[10] = 62; // Offset to pixels
    
    // DIB Header
    mockBmp[14] = 40;
    mockBmp[18] = 2; mockBmp[19] = 0; // Width 2
    mockBmp[22] = 2; mockBmp[23] = 0; // Height 2
    mockBmp[26] = 1; mockBmp[27] = 0; // Planes 1
    mockBmp[28] = 1; mockBmp[29] = 0; // BitCount 1
    
    // Pixel bits (Bottom-up)
    // Row 0 (Bottom): 01xxxxxx (0x40) at offset 62
    // Row 1 (Top): 10xxxxxx (0x80) at offset 66 (aligned to 4 bytes)
    mockBmp[62] = 0x40; // Row 0
    mockBmp[66] = 0x80; // Row 1
    
    uint16_t buffer[4];
    uint16_t accent = 0xF800; // Red
    uint16_t bg = 0x0000;     // Black
    
    BitmapRemapper::remapToBuffer(mockBmp, buffer, 2, 2, accent, bg, false);
    
    // BitmapRemapper.h:
    // tr=0 -> bmpRow = 2-1-0 = 1 -> rowPtr = bmpData + offset + (1 * 4) = mockBmp + 62 + 4 = 66
    // mockBmp[66] = 0x80 (10000000)
    // c=0 -> bit = 1 -> buffer[0] = accent
    // c=1 -> bit = 0 -> buffer[1] = bg
    
    // tr=1 -> bmpRow = 2-1-1 = 0 -> rowPtr = mockBmp + 62 + 0 = 62
    // mockBmp[62] = 0x40 (01000000)
    // c=0 -> bit = 0 -> buffer[2] = bg
    // c=1 -> bit = 1 -> buffer[3] = accent
    
    TEST_ASSERT_EQUAL(accent, buffer[0]);
    TEST_ASSERT_EQUAL(bg, buffer[1]);
    TEST_ASSERT_EQUAL(bg, buffer[2]);
    TEST_ASSERT_EQUAL(accent, buffer[3]);
}

void test_remap_to_buffer_inverted() {
    uint8_t mockBmp[100];
    memset(mockBmp, 0, 100);
    mockBmp[10] = 62;
    mockBmp[18] = 2;
    mockBmp[22] = 2;
    mockBmp[28] = 1;
    mockBmp[66] = 0x80; // Top row: 1 0
    
    uint16_t buffer[4];
    uint16_t accent = 0xF800;
    uint16_t bg = 0x0000;
    
    BitmapRemapper::remapToBuffer(mockBmp, buffer, 2, 2, accent, bg, true);
    
    TEST_ASSERT_EQUAL((uint16_t)~accent, buffer[0]);
    TEST_ASSERT_EQUAL((uint16_t)~bg, buffer[1]);
}

void test_draw_theme_icon() {
    uint8_t mockBmp[100];
    memset(mockBmp, 0, 100);
    mockBmp[10] = 62;
    mockBmp[18] = 2;
    mockBmp[22] = 2;
    mockBmp[28] = 1;
    mockBmp[66] = 0x80; // Top row: 1 0
    
    TestCanvas canvas;
    BitmapRemapper::drawThemeIcon(canvas, mockBmp, 10, 20, 2, 2);
    
    // Should have drawn 4 pixels
    TEST_ASSERT_EQUAL(4, canvas.pixels.size());
    
    // First pixel (top-left) should be accent color from RED_TEAM theme (0xF800)
    TEST_ASSERT_EQUAL(10, canvas.pixels[0].x);
    TEST_ASSERT_EQUAL(20, canvas.pixels[0].y);
    TEST_ASSERT_EQUAL(0xF800, canvas.pixels[0].color); // Default RED_TEAM accent
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_remap_to_buffer);
    RUN_TEST(test_remap_to_buffer_inverted);
    RUN_TEST(test_draw_theme_icon);
    return UNITY_END();
}
