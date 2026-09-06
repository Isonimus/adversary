/**
 * @file classic_bt.h
 * @brief Bluetooth Classic helper for bonding (M5Stick focus)
 */

#pragma once

#include <cstdint>
#include <string>

#ifdef ESP32
#ifndef CONFIG_IDF_TARGET_ESP32S3 // Not for S3
#define HAS_CLASSIC_BT
#endif
#endif

namespace adversary {
namespace ble {

/**
 * @brief Simple wrapper for ESP32 Classic Bluetooth bonding
 */
class ClassicBT {
public:
    static ClassicBT& getInstance();

    bool init();
    void deinit();
    
    /**
     * @brief Attempt to bond with a classic device
     * @param address Classic BD_ADDR (Extracted from BLE handshake)
     */
    bool startBonding(const uint8_t* address);
    
    /**
     * @brief Check if currently bonding
     */
    bool isBonding() const { return m_isBonding; }
    
    /**
     * @brief Get last status/error
     */
    const char* getStatus() const { return m_status.c_str(); }

private:
    ClassicBT();
    bool m_isBonding;
    std::string m_status;
};

} // namespace ble
} // namespace adversary
