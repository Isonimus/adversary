#pragma once

#ifndef ESP32

#include <vector>
#include <utility>
#include <stdint.h>

/**
 * @brief Mock for IRremoteESP8266's IRsend class
 */
class IRsend {
public:
    // One recorded sendRaw call: the burst length and the carrier frequency it
    // was sent at. The log (not just the last call) is what lets a test assert
    // how many times and at which carrier a code was emitted.
    struct SendCall {
        uint16_t len;
        uint32_t freq;
    };

    IRsend(uint16_t pin) : m_pin(pin), m_begun(false), m_lastFreq(0) {}

    void begin() { m_begun = true; }

    void sendRaw(const uint16_t *buf, uint16_t len, uint32_t freq) {
        m_lastBuffer.assign(buf, buf + len);
        m_lastFreq = freq;
        m_calls.push_back({len, freq});
    }

    // Test helpers
    bool isBegun() const { return m_begun; }
    const std::vector<uint16_t>& getLastBuffer() const { return m_lastBuffer; }
    uint32_t getLastFreq() const { return m_lastFreq; }
    const std::vector<SendCall>& getCalls() const { return m_calls; }
    void clear() { m_lastBuffer.clear(); m_lastFreq = 0; m_calls.clear(); }

private:
    uint16_t m_pin;
    bool m_begun;
    std::vector<uint16_t> m_lastBuffer;
    uint32_t m_lastFreq;
    std::vector<SendCall> m_calls;
};

#endif
