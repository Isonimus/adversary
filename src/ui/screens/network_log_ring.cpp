/**
 * @file network_log_ring.cpp
 * @brief NetworkLogRing implementation (pure, display-free).
 */

#include "network_log_ring.h"

#include <cstring>

namespace adversary {

size_t NetworkLogRing::typeCount(wardriving::ObservationType type) const {
    size_t n = 0;
    for (size_t i = 0; i < count_; i++) {
        if (entries_[i].type == type) {
            n++;
        }
    }
    return n;
}

void NetworkLogRing::eraseOldestOfType(wardriving::ObservationType type) {
    // Entries are kept oldest-first, so the first match is the oldest of its type. Close the
    // gap by shifting the tail down one slot; count_ shrinks by one.
    for (size_t i = 0; i < count_; i++) {
        if (entries_[i].type == type) {
            for (size_t j = i + 1; j < count_; j++) {
                entries_[j - 1] = entries_[j];
            }
            count_--;
            return;
        }
    }
}

void NetworkLogRing::append(const char* name, const uint8_t* bssid, int8_t rssi,
                            uint8_t channel, uint8_t security, wardriving::ObservationType type) {
    // Already shown? Dedup by (type, bssid) across every slot. A WiFi BSSID and a BLE BD_ADDR
    // can be byte-equal, so the type is part of the key (same rule as the manager's
    // observationKey). A hit is dropped, not refreshed, so each distinct radio shows once.
    for (size_t i = 0; i < count_; i++) {
        if (entries_[i].type == type && memcmp(entries_[i].bssid, bssid, 6) == 0) {
            return;
        }
    }

    if (count_ == CAPACITY) {
        // Full: free a slot without starving either radio. A type still under its reservation
        // must grow, so it takes a slot from the other type, which is therefore over its own
        // reservation (the reserves sum to at most CAPACITY). A type already at or over its
        // reservation evicts its own oldest instead, leaving the other type's guarantee intact.
        const wardriving::ObservationType other =
            (type == wardriving::ObservationType::WIFI) ? wardriving::ObservationType::BLE
                                                        : wardriving::ObservationType::WIFI;
        const size_t reserve =
            (type == wardriving::ObservationType::WIFI) ? WIFI_RESERVE : BLE_RESERVE;
        eraseOldestOfType(typeCount(type) < reserve ? other : type);
    }

    LoggedNetwork& entry = entries_[count_];
    const char* safeName = name ? name : "";
    std::strncpy(entry.ssid, safeName, sizeof(entry.ssid) - 1);
    entry.ssid[sizeof(entry.ssid) - 1] = '\0';
    std::memcpy(entry.bssid, bssid, sizeof(entry.bssid));
    entry.rssi = rssi;
    entry.channel = channel;
    entry.security = security;
    entry.type = type;
    count_++;
}

} // namespace adversary
