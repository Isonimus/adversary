/**
 * @file tvbgone_blaster.cpp
 * @brief TvBGoneBlaster implementation
 */

#include "tvbgone_blaster.h"
#include "core/event_bus.h"
#include "config/pins.h"

namespace adversary {
namespace ir {

void emitIrBurst(IRsend& sender, const uint16_t* buffer, uint16_t len, uint32_t freqHz) {
    if (freqHz >= SONY_CARRIER_MIN_HZ && freqHz <= SONY_CARRIER_MAX_HZ) {
        for (uint8_t r = 0; r < SONY_REPEATS; ++r) {
            sender.sendRaw(buffer, len, freqHz);
            if (r + 1 < SONY_REPEATS) delay(SONY_REPEAT_GAP_MS);
        }
    } else {
        sender.sendRaw(buffer, len, freqHz);
    }
}

static TvBGoneBlaster* s_instance = nullptr;

TvBGoneBlaster& TvBGoneBlaster::getInstance() {
    if (!s_instance) s_instance = new TvBGoneBlaster();
    return *s_instance;
}

TvBGoneBlaster::TvBGoneBlaster()
    : m_irSend(nullptr)
    , m_blasting(false)
    , m_region(Region::NORTH_AMERICA)
    , m_currentIndex(0)
    , m_totalCodes(0)
    , m_lastBlastTime(0) {
}

void TvBGoneBlaster::init() {
    if (m_irSend) return;

#if defined(TARGET_CARDPUTER)
    m_irSend = new IRsend(44); // IR LED pin on Cardputer
#elif defined(TARGET_M5STICK)
    m_irSend = new IRsend(19); // IR LED pin on M5Stick (default)
#else
    m_irSend = new IRsend(pins::IR_TX);
#endif

    m_irSend->begin();
    Serial.println("[IR] Blaster initialized");
}

void TvBGoneBlaster::start(Region region) {
    if (region != Region::NORTH_AMERICA && region != Region::EUROPE) {
        Serial.println("[IR] Error: Invalid region");
        m_blasting = false;
        return;
    }

    m_region = region;
    m_currentIndex = 0;
    m_blasting = true;
    m_totalCodes = (region == Region::NORTH_AMERICA) ? NA_DATABASE_SIZE : EU_DATABASE_SIZE;
    
    Serial.printf("[IR] Starting TV-B-Gone blast for %s (%d codes)\n", 
                  region == Region::NORTH_AMERICA ? "NA" : "EU", m_totalCodes);
    
    sendNext();
}

void TvBGoneBlaster::stop() {
    m_blasting = false;
    m_currentIndex = 0;
    Serial.println("[IR] Blaster stopped");
}

void TvBGoneBlaster::update() {
    if (!m_blasting) return;

    // Small delay between bursts to avoid overheating the tiny IR LED 
    // and to allow the receiving TV's MCU to process the command
    if (millis() - m_lastBlastTime > 300) {
        sendNext();
    }
}

void TvBGoneBlaster::sendNext() {
    if (!m_blasting || !m_irSend) return;

    if (m_currentIndex >= m_totalCodes) {
        m_blasting = false;
        Serial.println("[IR] Region scan complete");
        // Publish completion via EventBus
        adversary::EventData evt(adversary::EventType::IR_TRANSMISSION_COMPLETED);
        adversary::EventBus::getInstance().publish(evt);
        return;
    }

    // Get code metadata from PROGMEM
    IrCode const* codePtr;
    if (m_region == Region::NORTH_AMERICA) {
        codePtr = (IrCode const*)pgm_read_ptr(&NA_DATABASE[m_currentIndex]);
    } else {
        codePtr = (IrCode const*)pgm_read_ptr(&EU_DATABASE[m_currentIndex]);
    }

    if (!codePtr) {
        m_currentIndex++;
        return;
    }

    // Read metadata from flash
    // freq_khz is actually already in kHz, but sendRaw expects Hz.
    // However, looking at the database, it's already divided.
    uint32_t freq = pgm_read_word(&codePtr->freq_khz) * 1000;
    uint16_t numPairs = pgm_read_word(&codePtr->num_pairs);
    uint8_t bitTime = pgm_read_byte(&codePtr->bit_time);
    uint16_t const* timesPtr = (uint16_t const*)pgm_read_ptr(&codePtr->times);
    uint8_t const* codesPtr = (uint8_t const*)pgm_read_ptr(&codePtr->codes);

    // Decompress code on the fly
    uint16_t buffer[300];
    int bufferIdx = 0;

    if (numPairs > 150) {
        Serial.println("[IR] Error: Code too long for buffer");
        m_currentIndex++;
        return;
    }

    // Bruce/Ken Shirriff's read_bits logic (MSB-first, stateful)
    uint8_t bitsLeft = 0;
    uint8_t currentByte = 0;
    uint32_t codePtrIdx = 0;

    for (uint16_t i = 0; i < numPairs; i++) {
        uint8_t timeIndex = 0;
        for (uint8_t bit = 0; bit < bitTime; bit++) {
            if (bitsLeft == 0) {
                currentByte = pgm_read_byte(codesPtr + codePtrIdx++);
                bitsLeft = 8;
            }
            bitsLeft--;
            timeIndex |= (((currentByte >> bitsLeft) & 1) << (bitTime - 1 - bit));
        }
        
        // Get timing pair (Mark and Space are in 10us units in TV-B-Gone database)
        uint16_t offtime = pgm_read_word(timesPtr + (timeIndex * 2));
        uint16_t ontime = pgm_read_word(timesPtr + (timeIndex * 2 + 1));
        
        // Note: Bruce's order in rawData is [off, on] but modulated properly by sendRaw.
        // In the original DB, times[ti] is MARK, times[ti+1] is SPACE usually.
        // Wait, Bruce code says:
        // offtime = powerCode->times[ti];    // read word 1 - ontime
        // ontime = powerCode->times[ti + 1]; // read word 2 - offtime
        // rawData[k * 2] = offtime * 10;
        // rawData[(k * 2) + 1] = ontime * 10;
        
        buffer[bufferIdx++] = offtime * 10;
        buffer[bufferIdx++] = ontime * 10;
    }

    // sendRaw's carrier is in Hz; the Sony-range repeat policy lives in emitIrBurst.
    emitIrBurst(*m_irSend, buffer, bufferIdx, freq);

    Serial.printf("[IR] Blasted code %d/%d (Freq: %dkHz, Pairs: %d)\n", 
                  m_currentIndex + 1, m_totalCodes, freq, numPairs);

    m_currentIndex++;
    m_lastBlastTime = millis();
}

} // namespace ir
} // namespace adversary
