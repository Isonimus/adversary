/**
 * @file time_mocks.h
 * @brief Time function mocks for native testing
 * 
 * Provides millis(), micros(), and delay() mocks for all native tests.
 * Header-only design with inline functions to avoid linker conflicts.
 */

#pragma once

#ifndef ESP32

#include <cstdint>

// Global time state
namespace test_mocks {
    inline uint32_t g_mock_millis = 1000;
    inline uint32_t g_mock_micros = 1000000;
}

// Mock Arduino time functions
inline uint32_t millis() {
    return test_mocks::g_mock_millis;
}

inline uint32_t micros() {
    return test_mocks::g_mock_micros;
}

inline void delay(unsigned long ms) {
    // No-op for tests - instant execution
    (void)ms;
}

// Test helpers
inline void setMockMillis(uint32_t ms) {
    test_mocks::g_mock_millis = ms;
}

inline void setMockMicros(uint32_t us) {
    test_mocks::g_mock_micros = us;
}

#endif // ESP32
