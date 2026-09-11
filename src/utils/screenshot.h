/**
 * @file screenshot.h
 * @brief Capture the live UI canvas to a BMP file on the SD card.
 *
 * The Cardputer LCD is write-only over SPI (no panel read-back), so the only
 * faithful frame capture is the firmware dumping the canvas it renders into.
 * saveScreenshot() reads the global RGB332 canvas back into BMP's B,G,R byte
 * order and writes a 24-bit BMP to /adversary/screenshots/shot_NNN.bmp for
 * pop-the-SD retrieval.
 */

#pragma once

#include <stddef.h>

#include "hal/display/canvas_types.h"

namespace adversary {

class SDManager;

/**
 * @brief Write the current canvas to /adversary/screenshots/shot_NNN.bmp.
 *
 * @param canvas      The canvas to capture (the global UI canvas).
 * @param sd          SD manager, must be ready.
 * @param outPath     Receives the written file path on success.
 * @param outPathLen  Size of @p outPath.
 * @return true on success; false (with a logged reason) if the SD is not
 *         ready, no free filename slot exists, memory is too low, or the write
 *         is short. Never reports success on a partial write.
 */
bool saveScreenshot(Canvas& canvas, SDManager& sd, char* outPath, size_t outPathLen);

}  // namespace adversary
