#include "utils/screenshot.h"

// Device-only: readRectRGB() and the global canvas exist solely on real M5GFX
// hardware. On the native test target this compiles to an empty translation
// unit (the pure header-layout logic is tested via utils/bmp_header.h).
#if defined(TARGET_CARDPUTER) || defined(TARGET_M5STICK)

#include <Arduino.h>
#include <SD.h>
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

    // Stream the file row-by-row rather than buffering the whole ~97 KB BMP: on
    // the no-PSRAM device a single contiguous file-sized malloc fails once a
    // heavy screen is resident (slice-0034). Peak allocation here is one row
    // (~720 B). Hold the handle open (as PcapWriter does) instead of re-opening
    // per row, which SDManager::appendFile() would do.
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[Screenshot] Could not open %s for write, free heap %u\n",
                      path, (unsigned)ESP.getFreeHeap());
        return false;
    }

    // Close the handle, drop the partial file, and fail loud. Never leave a
    // truncated file behind — a short BMP renders as blank/garbage.
    auto abortWrite = [&](const char* why) -> bool {
        file.close();
        SD.remove(path);
        Serial.printf("[Screenshot] %s (free heap %u)\n", why, (unsigned)ESP.getFreeHeap());
        return false;
    };

    uint8_t header[bmp::BMP24_HEADER_SIZE];
    bmp::writeBmp24Header(header, width, height);
    if (file.write(header, bmp::BMP24_HEADER_SIZE) != bmp::BMP24_HEADER_SIZE) {
        return abortWrite("Header write failed");
    }

    auto* row = static_cast<uint8_t*>(malloc(rowBytes));
    if (row == nullptr) {
        return abortWrite("Row buffer malloc failed");
    }
    // Zero once so any 4-byte row-padding tail stays 0: readRect fills only
    // width*3 bytes. Current targets are 240 px wide (720 B rows, unpadded).
    memset(row, 0, rowBytes);

    // BMP 24bpp pixels are stored B,G,R, bottom-up. LovyanGFX's type names are
    // inverted from memory layout: rgb888_t is laid out {b,g,r} in memory,
    // which is exactly BMP order, whereas readRectRGB()/bgr888_t is {r,g,b} and
    // would swap red and blue (bright pink renders as purple). So read each row
    // as rgb888_t. File row r reads source row (h-1-r) for bottom-up order.
    for (uint16_t r = 0; r < height; ++r) {
        canvas.readRect(0, bmp::bmp24SourceRow(r, height), width, 1,
                        reinterpret_cast<lgfx::rgb888_t*>(row));
        if (file.write(row, rowBytes) != rowBytes) {
            free(row);
            return abortWrite("Row write failed");
        }
    }
    free(row);
    file.close();

    Serial.printf("[Screenshot] Saved %s (%u bytes)\n", path, (unsigned)fileSize);
    if (outPath != nullptr && outPathLen > 0) {
        strncpy(outPath, path, outPathLen - 1);
        outPath[outPathLen - 1] = '\0';
    }
    return true;
}

}  // namespace adversary

#endif  // TARGET_CARDPUTER || TARGET_M5STICK
