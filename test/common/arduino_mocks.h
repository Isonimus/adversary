/**
 * @file arduino_mocks.h
 * @brief Arduino core function mocks for native testing
 */

#pragma once

#ifndef ESP32

#include <cstdarg>
#include <string>
#include <cstring>
#include "time_mocks.h"

#ifndef ESP32
typedef uint8_t byte;
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
typedef void* TaskHandle_t;
#endif

namespace test_mocks {

/**
 * Basic Arduino String mock
 */
class String : public std::string {
public:
    String() : std::string("") {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(unsigned long v) : std::string(std::to_string(v)) {}
    String(float v, int dec = 2) : std::string(std::to_string(v)) {} // simplified
    
    void concat(const char* s) { append(s); }
    void concat(const String& s) { append(s); }
    
    int lastIndexOf(char c) const {
        size_t pos = rfind(c);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    
    int lastIndexOf(const char* s) const {
        size_t pos = rfind(s);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    int indexOf(char c, int start = 0) const {
        size_t pos = find(c, start);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    int indexOf(const char* s, int start = 0) const {
        size_t pos = find(s, start);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    
    String substring(int start, int end = -1) const {
        if (end == -1) return String(substr(start).c_str());
        return String(substr(start, end - start).c_str());
    }

    bool startsWith(const char* s) const {
        return find(s) == 0;
    }

    bool endsWith(const char* s) const {
        size_t slen = strlen(s);
        if (slen > length()) return false;
        return compare(length() - slen, slen, s) == 0;
    }

    void trim() {
        size_t first = find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            clear();
            return;
        }
        size_t last = find_last_not_of(" \t\r\n");
        *this = substr(first, (last - first + 1));
    }
};

} // namespace test_mocks

// Make String available globally
using test_mocks::String;

namespace test_mocks {

/**
 * Mock Serial class for println/printf functions
 */
class MockSerial {
public:
    void println(const char* msg) { /* suppress */ }
    void println(const std::string& msg) { /* suppress */ }
    void printf(const char* fmt, ...) { /* suppress */ }
    void print(const char* msg) { /* suppress */ }
    void print(const std::string& msg) { /* suppress */ }
};

extern MockSerial Serial;

/**
 * Mock ESP class for heap/psram queries
 */
class MockESP {
public:
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getFreePsram() { return 0; }
    uint32_t getHeapSize() { return 327680; }
};

extern MockESP ESP;

// Global Arduino functions (non-conflicting)
void randomSeed(uint32_t seed);
long random(long max);
long random(long min, long max);

} // namespace test_mocks

// Make Serial available globally
using test_mocks::Serial;
using test_mocks::ESP;
using test_mocks::randomSeed;
using test_mocks::random;

// PROGMEM mocks
#define PROGMEM
#define pgm_read_ptr(addr) (*(const void**)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))

#endif // ESP32
