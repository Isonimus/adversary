/**
 * @file bmp_header.h
 * @brief Pure builder for a 24-bit uncompressed (BI_RGB) BMP file header.
 *
 * Split out from the device-only screenshot writer so the byte-level layout —
 * where every off-by-one or wrong-endian field silently produces a corrupt or
 * blank image — can be unit-tested on the native target without M5GFX.
 *
 * The pixel array that follows this header must be bottom-up rows of BGR888
 * triples, each row padded to a 4-byte boundary (see bmp24RowBytes()). M5GFX's
 * readRectRGB() already yields BGR888, which is BMP's native channel order.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace adversary {
namespace bmp {

// BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40).
constexpr size_t BMP24_HEADER_SIZE = 54;

// 24 bpp = 3 bytes/pixel; BMP rows are padded to a 4-byte boundary.
constexpr size_t bmp24RowBytes(uint16_t width) {
    return ((static_cast<size_t>(width) * 3u) + 3u) & ~static_cast<size_t>(3u);
}

// Total pixel-array size (padded rows) for a width x height image.
constexpr size_t bmp24PixelBytes(uint16_t width, uint16_t height) {
    return bmp24RowBytes(width) * static_cast<size_t>(height);
}

// Total on-disk size: header + pixel array.
constexpr size_t bmp24FileSize(uint16_t width, uint16_t height) {
    return BMP24_HEADER_SIZE + bmp24PixelBytes(width, height);
}

namespace detail {
inline void putLE16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void putLE32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
}  // namespace detail

/**
 * @brief Write the 54-byte header for a bottom-up 24-bit BMP into @p out.
 *
 * @param out Buffer of at least BMP24_HEADER_SIZE bytes.
 * @param width  Image width in pixels.
 * @param height Image height in pixels. Positive = bottom-up row order, the
 *               most widely compatible form.
 */
inline void writeBmp24Header(uint8_t* out, uint16_t width, uint16_t height) {
    // 72 DPI expressed in pixels-per-metre, the conventional BMP default.
    constexpr int32_t PIXELS_PER_METRE_72DPI = 2835;

    for (size_t i = 0; i < BMP24_HEADER_SIZE; ++i) out[i] = 0;

    // BITMAPFILEHEADER
    out[0] = 'B';
    out[1] = 'M';
    detail::putLE32(out + 2, static_cast<uint32_t>(bmp24FileSize(width, height)));
    // bytes 6..9 reserved = 0
    detail::putLE32(out + 10, static_cast<uint32_t>(BMP24_HEADER_SIZE));  // pixel data offset

    // BITMAPINFOHEADER
    detail::putLE32(out + 14, 40u);  // DIB header size
    detail::putLE32(out + 18, static_cast<uint32_t>(width));
    detail::putLE32(out + 22, static_cast<uint32_t>(height));
    detail::putLE16(out + 26, 1u);   // colour planes
    detail::putLE16(out + 28, 24u);  // bits per pixel
    detail::putLE32(out + 30, 0u);   // BI_RGB, no compression
    detail::putLE32(out + 34, static_cast<uint32_t>(bmp24PixelBytes(width, height)));
    detail::putLE32(out + 38, static_cast<uint32_t>(PIXELS_PER_METRE_72DPI));
    detail::putLE32(out + 42, static_cast<uint32_t>(PIXELS_PER_METRE_72DPI));
    // bytes 46..49 colours used = 0 (all), 50..53 important colours = 0 (all)
}

}  // namespace bmp
}  // namespace adversary
