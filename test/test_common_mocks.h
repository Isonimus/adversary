/**
 * @file test_common_mocks.h
 * @brief Common mocks shared across all native tests
 * 
 * Provides global millis() mock needed by karma_ap and other modules
 */

#pragma once

#ifndef ESP32

#include <cstdint>

// Global millis() mock - defined here for all tests to link against
// karma_ap.cpp references this via extern declaration
#ifndef TEST_MILLIS_DEFINED
#define TEST_MILLIS_DEFINED

uint32_t g_mock_millis = 1000;

inline uint32_t millis() {
    return g_mock_millis;
}

inline void setMockMillis(uint32_t ms) {
    g_mock_millis = ms;
}

#endif // TEST_MILLIS_DEFINED

#endif // ESP32
