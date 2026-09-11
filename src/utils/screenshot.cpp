#include "utils/screenshot.h"

// Device-only: readRectRGB() and the global canvas exist solely on real M5GFX
// hardware. On the native test target this compiles to an empty translation
// unit (the pure header-layout logic is tested via utils/bmp_header.h).
#if defined(TARGET_CARDPUTER) || defined(TARGET_M5STICK)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "config/config.h"
#include "hal/storage/sd_manager.h"
#include "utils/bmp_header.h"

namespace adversary {

namespace {
constexpr const char* SCREENSHOT_DIR = "/adversary/screenshots";
constexpr int MAX_SCREENSHOTS = 1000;  // shot_000 .. shot_999
}  // namespace

bool saveScreenshot(Canvas& canvas, SDManager& sd, char* outPath, size_t outPathLen) {
    if (!sd.isReady()) {
        Serial.println("[Screenshot] SD not ready");
        return false;
    }

    const uint16_t width = static_cast<uint16_t>(canvas.width());
    const uint16_t height = static_cast<uint16_t>(canvas.height());
    if (width == 0 || height == 0 || canvas.getBuffer() == nullptr) {
        Serial.println("[Screenshot] Canvas not ready");
        return false;
    }

    if (!sd.directoryExists(SCREENSHOT_DIR) && !sd.createDirectory(SCREENSHOT_DIR)) {
        Serial.printf("[Screenshot] Could not create %s\n", SCREENSHOT_DIR);
        return false;
    }

    // First free shot_NNN.bmp slot. Index-based (not timestamped) so captures
    // never collide when the RTC is unset, which is the common boot state.
    char path[64] = {0};
    int index = -1;
    for (int i = 0; i < MAX_SCREENSHOTS; ++i) {
        snprintf(path, sizeof(path), "%s/shot_%03d.bmp", SCREENSHOT_DIR, i);
        if (!sd.fileExists(path)) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        Serial.printf("[Screenshot] No free slot (>= %d files)\n", MAX_SCREENSHOTS);
        return false;
    }

    const size_t rowBytes = bmp::bmp24RowBytes(width);
    const size_t fileSize = bmp::bmp24FileSize(width, height);

    auto* buffer = static_cast<uint8_t*>(malloc(fileSize));
    if (buffer == nullptr) {
        Serial.printf("[Screenshot] malloc(%u) failed, free heap %u\n",
                      (unsigned)fileSize, (unsigned)ESP.getFreeHeap());
        return false;
    }

    bmp::writeBmp24Header(buffer, width, height);

    // Zero the pixel area only when rows carry 4-byte padding; readRectRGB()
    // fills exactly width*3 bytes per row and would otherwise leave pad bytes
    // uninitialised. Current targets are 240 px wide (720 B rows, unpadded).
    uint8_t* pixels = buffer + bmp::BMP24_HEADER_SIZE;
    if (rowBytes != static_cast<size_t>(width) * 3u) {
        memset(pixels, 0, bmp::bmp24PixelBytes(width, height));
    }

    // BMP rows are bottom-up: source row y lands at destination row (h-1-y).
    // readRectRGB() yields BGR888, which is BMP's native channel order.
    for (uint16_t y = 0; y < height; ++y) {
        uint8_t* dest = pixels + static_cast<size_t>(height - 1 - y) * rowBytes;
        canvas.readRectRGB(0, y, width, 1, dest);
    }

    FileResult result = sd.writeFile(path, buffer, fileSize);
    free(buffer);

    if (!result.success || result.bytesWritten != fileSize) {
        Serial.printf("[Screenshot] Write failed (%u/%u bytes): %s\n",
                      (unsigned)result.bytesWritten, (unsigned)fileSize,
                      result.error ? result.error : "unknown");
        sd.deleteFile(path);  // don't leave a truncated, blank-rendering file
        return false;
    }

    Serial.printf("[Screenshot] Saved %s (%u bytes)\n", path, (unsigned)fileSize);
    if (outPath != nullptr && outPathLen > 0) {
        strncpy(outPath, path, outPathLen - 1);
        outPath[outPathLen - 1] = '\0';
    }
    return true;
}

}  // namespace adversary

#endif  // TARGET_CARDPUTER || TARGET_M5STICK
