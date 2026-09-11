/**
 * @file test_bmp_header.cpp
 * @brief Unit tests for the 24-bit BMP header builder (utils/bmp_header.h).
 *
 * The header's byte layout is where a wrong offset, size, or endianness
 * silently yields a corrupt or blank image. These tests pin every field for
 * the real 240x135 capture size and assert the size/row-stride helpers.
 */

#include <unity.h>

#include "utils/bmp_header.h"

using namespace adversary::bmp;

namespace {

uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

constexpr uint16_t W = 240;
constexpr uint16_t H = 135;

}  // namespace

void setUp() {}
void tearDown() {}

void test_row_bytes_pads_to_four() {
    TEST_ASSERT_EQUAL_UINT32(720u, bmp24RowBytes(240));  // 720 already aligned
    TEST_ASSERT_EQUAL_UINT32(4u, bmp24RowBytes(1));      // 3 -> pad to 4
    TEST_ASSERT_EQUAL_UINT32(8u, bmp24RowBytes(2));      // 6 -> pad to 8
    TEST_ASSERT_EQUAL_UINT32(12u, bmp24RowBytes(3));     // 9 -> pad to 12
    TEST_ASSERT_EQUAL_UINT32(12u, bmp24RowBytes(4));     // 12 aligned
}

void test_sizes_for_capture_dimensions() {
    TEST_ASSERT_EQUAL_UINT32(720u * 135u, bmp24PixelBytes(W, H));
    TEST_ASSERT_EQUAL_UINT32(54u + 720u * 135u, bmp24FileSize(W, H));
}

void test_header_fields() {
    uint8_t hdr[BMP24_HEADER_SIZE];
    // Poison so the builder must zero the reserved/trailing fields itself.
    for (size_t i = 0; i < BMP24_HEADER_SIZE; ++i) hdr[i] = 0xAB;

    writeBmp24Header(hdr, W, H);

    // BITMAPFILEHEADER
    TEST_ASSERT_EQUAL_UINT8('B', hdr[0]);
    TEST_ASSERT_EQUAL_UINT8('M', hdr[1]);
    TEST_ASSERT_EQUAL_UINT32(bmp24FileSize(W, H), readLE32(hdr + 2));
    TEST_ASSERT_EQUAL_UINT16(0, readLE16(hdr + 6));   // reserved1
    TEST_ASSERT_EQUAL_UINT16(0, readLE16(hdr + 8));   // reserved2
    TEST_ASSERT_EQUAL_UINT32(54u, readLE32(hdr + 10)); // pixel data offset

    // BITMAPINFOHEADER
    TEST_ASSERT_EQUAL_UINT32(40u, readLE32(hdr + 14)); // DIB header size
    TEST_ASSERT_EQUAL_UINT32(W, readLE32(hdr + 18));
    TEST_ASSERT_EQUAL_UINT32(H, readLE32(hdr + 22));   // positive => bottom-up
    TEST_ASSERT_EQUAL_UINT16(1, readLE16(hdr + 26));   // planes
    TEST_ASSERT_EQUAL_UINT16(24, readLE16(hdr + 28));  // bpp
    TEST_ASSERT_EQUAL_UINT32(0u, readLE32(hdr + 30));  // BI_RGB
    TEST_ASSERT_EQUAL_UINT32(bmp24PixelBytes(W, H), readLE32(hdr + 34));
    TEST_ASSERT_EQUAL_UINT32(0u, readLE32(hdr + 46));  // colours used
    TEST_ASSERT_EQUAL_UINT32(0u, readLE32(hdr + 50));  // important colours
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_row_bytes_pads_to_four);
    RUN_TEST(test_sizes_for_capture_dimensions);
    RUN_TEST(test_header_fields);
    return UNITY_END();
}
