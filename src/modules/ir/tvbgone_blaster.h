/**
 * @file tvbgone_blaster.h
 * @brief Async TV-B-Gone blaster engine
 */

#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <IRsend.h>
#else
#include "../../../test/common/arduino_mocks.h"
#include "../../../test/common/ir_mocks.h"
#endif
#include "tvbgone_data.h"
#include <functional>

namespace adversary {
namespace ir {

enum class Region {
    NORTH_AMERICA,
    EUROPE
};

// Sony SIRC (38-40 kHz carrier) is only decoded reliably when a frame is sent at
// least three times with a short gap; other protocols send once. These bound the
// repeat policy applied by emitIrBurst().
constexpr uint32_t SONY_CARRIER_MIN_HZ = 38000;
constexpr uint32_t SONY_CARRIER_MAX_HZ = 40000;
constexpr uint8_t SONY_REPEATS = 3;
constexpr uint32_t SONY_REPEAT_GAP_MS = 45;

/**
 * @brief Emit one decompressed raw burst on @p sender at carrier @p freqHz.
 *
 * WHY a dedicated helper: the emit policy (how many repeats, at which carrier) is
 * the regression surface for a fixed double-send defect — a single send site here
 * makes the "send twice, once at the wrong carrier" bug structurally impossible,
 * and lets the policy be unit-tested against a mock without the PROGMEM database.
 */
void emitIrBurst(IRsend& sender, const uint16_t* buffer, uint16_t len, uint32_t freqHz);

class TvBGoneBlaster {
public:
    static TvBGoneBlaster& getInstance();

    /**
     * @brief Initialize transmitter hardware
     */
    void init();

    /**
     * @brief Start blasting codes for a region
     */
    void start(Region region);

    /**
     * @brief Stop blasting
     */
    void stop();

    /**
     * @brief Periodic update (call from loop)
     */
    void update();

    /**
     * @brief Status queries
     */
    bool isBlasting() const { return m_blasting; }
    int getCurrentIndex() const { return m_currentIndex; }
    int getTotalCodes() const { return m_totalCodes; }
    float getProgress() const { return m_totalCodes > 0 ? (float)m_currentIndex / m_totalCodes : 0; }
    Region getRegion() const { return m_region; }
private:
    TvBGoneBlaster();
    ~TvBGoneBlaster() = default;

    IRsend* m_irSend;
    bool m_blasting;
    Region m_region;
    int m_currentIndex;
    int m_totalCodes;
    uint32_t m_lastBlastTime;
    void sendNext();
};

} // namespace ir
} // namespace adversary
