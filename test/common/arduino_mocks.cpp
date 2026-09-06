/**
 * @file arduino_mocks.cpp
 * @brief Arduino core function mock implementations
 */

#include "arduino_mocks.h"
#include "m5_mocks.h"
#include "FastLED.h"
#include <chrono>
#include <thread>
#include <cstdlib>

#ifndef ESP32

namespace test_mocks {

// Define the global Serial and ESP instances
MockSerial Serial;
MockESP ESP;
M5Unified M5;
CFastLED FastLED;

void randomSeed(uint32_t seed) {
    srand(seed);
}

long random(long max) {
    if (max <= 0) return 0;
    return rand() % max;
}

long random(long min, long max) {
    if (max <= min) return min;
    return min + (rand() % (max - min));
}

} // namespace test_mocks

#endif // ESP32
