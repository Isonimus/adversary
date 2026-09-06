#pragma once

#ifndef ESP32

#include <cstdint>

class TwoWire {
public:
    void begin() {}
    void begin(int sda, int scl) {}
    void write(uint8_t val) {}
    void endTransmission() {}
    void requestFrom(uint8_t addr, uint8_t size) {}
    uint8_t read() { return 0; }
    uint8_t available() { return 0; }
};

extern TwoWire Wire;

#endif
