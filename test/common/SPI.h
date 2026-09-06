#pragma once

#ifndef ESP32

#include <cstdint>
#include <cstddef>

class SPIClass {
public:
    void begin() {}
    void beginTransaction(void*) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t data) { return 0; }
    void transfer(void* buf, size_t count) {}
};

extern SPIClass SPI;

#endif
