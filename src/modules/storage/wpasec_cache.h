/**
 * @file wpasec_cache.h
 * @brief Single-file WPA-SEC status cache for fast lookups
 * 
 * Follows M5Porkchop pattern: single cache file loaded at startup,
 * all status checks are in-memory (zero SD reads during SSL operations).
 * 
 * WPA-SEC status lives ONLY here, not in per-file metadata.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "config/config.h"

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <map>
#endif

namespace adversary {

// Forward declaration - WpaSecStatus enum is defined in wpasec_service.h
// We use the enum values directly here to avoid circular include
enum class WpaSecStatus : uint8_t;

/**
 * @brief Cache entry for a single handshake
 */
struct WpaSecCacheEntry {
    WpaSecStatus status = WpaSecStatus::NOT_UPLOADED;
    char password[65] = {0};  ///< Cracked password (if status == CRACKED)
    uint32_t crackedAt = 0;   ///< Timestamp when cracked
    
    WpaSecCacheEntry() = default;
};

/**
 * @brief Singleton WPA-SEC status cache
 * 
 * Loads from /sd/wpasec_cache.json at startup.
 * All lookups are O(1) memory access - zero SD reads.
 */
class WpaSecCache {
public:
    static WpaSecCache& getInstance() {
        static WpaSecCache instance;
        return instance;
    }
    
    /**
     * @brief Clear cache to free memory before SSL operations
     * Cache will be reloaded from SD after SSL completes
     */
    void clearForSSL() {
#ifdef ESP32
        size_t heapBefore = ESP.getFreeHeap();
        cache_.clear();
        // Force std::map to release memory
        std::map<String, WpaSecCacheEntry>().swap(cache_);
        size_t heapAfter = ESP.getFreeHeap();
        Serial.printf("[WpaSecCache] Cleared for SSL: freed %d bytes\n", (int)(heapAfter - heapBefore));
#endif
    }
    
    /**
     * @brief Load cache from SD card (call once at startup)
     * @return Number of entries loaded
     */
    int load() {
#ifdef ESP32
        cache_.clear();
        
        if (!SD.exists(CACHE_PATH)) {
            Serial.println("[WpaSecCache] No cache file, starting fresh");
            return 0;
        }
        
        File file = SD.open(CACHE_PATH, FILE_READ);
        if (!file) {
            Serial.println("[WpaSecCache] Failed to open cache file");
            return 0;
        }
        
        // Check heap before allocating DynamicJsonDocument
        size_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 25000) {  // Need ~25KB for safe 4KB JSON allocation
            file.close();
            Serial.printf("[WpaSecCache] Low heap at boot: %u bytes - skipping cache load\n", freeHeap);
            Serial.println("[WpaSecCache] Cache will be rebuilt from uploads/refreshes");
            return 0;
        }
        
        // Parse JSON (4KB for ~60 entries with passwords)
        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        
        if (err) {
            Serial.printf("[WpaSecCache] JSON parse error: %s (heap: %u)\n", err.c_str(), freeHeap);
            Serial.println("[WpaSecCache] Cache will be rebuilt from uploads/refreshes");
            return 0;
        }
        
        // Load entries: { "SSID": { "status": 2, "password": "xxx", "crackedAt": 123 }, ... }
        JsonObject root = doc.as<JsonObject>();
        for (JsonPair kv : root) {
            const char* ssid = kv.key().c_str();
            JsonObject entry = kv.value().as<JsonObject>();
            
            WpaSecCacheEntry e;
            e.status = static_cast<WpaSecStatus>(entry["status"] | 0);
            e.crackedAt = entry["crackedAt"] | 0;
            
            const char* pwd = entry["password"] | "";
            strncpy(e.password, pwd, sizeof(e.password) - 1);
            
            cache_[String(ssid)] = e;
        }
        
        Serial.printf("[WpaSecCache] Loaded %d entries\n", cache_.size());
        return cache_.size();
#else
        return 0;
#endif
    }
    
    /**
     * @brief Save cache to SD card
     * @return true on success
     */
    bool save() {
#ifdef ESP32
        DynamicJsonDocument doc(2048);
        
        for (const auto& kv : cache_) {
            JsonObject entry = doc[kv.first].to<JsonObject>();
            entry["status"] = static_cast<int>(kv.second.status);
            if (kv.second.status == WpaSecStatus::CRACKED) {
                entry["password"] = kv.second.password;
                entry["crackedAt"] = kv.second.crackedAt;
            }
        }
        
        File file = SD.open(CACHE_PATH, FILE_WRITE);
        if (!file) {
            Serial.println("[WpaSecCache] Failed to open cache for write");
            return false;
        }
        
        serializeJson(doc, file);
        file.close();
        
        Serial.printf("[WpaSecCache] Saved %d entries\n", cache_.size());
        return true;
#else
        return false;
#endif
    }
    
    /**
     * @brief Get status for an SSID (O(1) memory lookup)
     */
    WpaSecStatus getStatus(const char* ssid) const {
        if (!ssid) return WpaSecStatus::NOT_UPLOADED;
        
        auto it = cache_.find(String(ssid));
        if (it != cache_.end()) {
            return it->second.status;
        }
        return WpaSecStatus::NOT_UPLOADED;
    }
    
    /**
     * @brief Set status for an SSID
     */
    void setStatus(const char* ssid, WpaSecStatus status, const char* password = nullptr) {
        if (!ssid) return;
        
        WpaSecCacheEntry& e = cache_[String(ssid)];
        e.status = status;
        
        if (password && status == WpaSecStatus::CRACKED) {
            strncpy(e.password, password, sizeof(e.password) - 1);
#ifdef ESP32
            e.crackedAt = millis() / 1000;
#endif
        }
        
        dirty_ = true;
    }
    
    /**
     * @brief Get cracked password for an SSID
     * @return Password string or nullptr if not cracked
     */
    const char* getPassword(const char* ssid) const {
        if (!ssid) return nullptr;
        
        auto it = cache_.find(String(ssid));
        if (it != cache_.end() && it->second.status == WpaSecStatus::CRACKED) {
            return it->second.password[0] ? it->second.password : nullptr;
        }
        return nullptr;
    }
    
    /**
     * @brief Check if cache has unsaved changes
     */
    bool isDirty() const { return dirty_; }
    
    /**
     * @brief Save if dirty (call periodically or on shutdown)
     */
    void saveIfDirty() {
        if (dirty_) {
            save();
            dirty_ = false;
        }
    }
    
    /**
     * @brief Get number of cached entries
     */
    size_t size() const { return cache_.size(); }
    
    /**
     * @brief Remove an SSID from the cache
     * @param ssid SSID to remove
     * @return true if entry was found and removed
     */
    bool removeEntry(const char* ssid) {
        if (!ssid) return false;
        
#ifdef ESP32
        auto it = cache_.find(String(ssid));
        if (it != cache_.end()) {
            cache_.erase(it);
            dirty_ = true;
            Serial.printf("[WpaSecCache] Removed entry: %s\n", ssid);
            return true;
        }
#else
        (void)ssid;
#endif
        return false;
    }

private:
    WpaSecCache() = default;
    ~WpaSecCache() = default;
    WpaSecCache(const WpaSecCache&) = delete;
    WpaSecCache& operator=(const WpaSecCache&) = delete;
    
    static constexpr const char* CACHE_PATH = "/adversary/cache/wpasec_cache.json";
    
#ifdef ESP32
    std::map<String, WpaSecCacheEntry> cache_;  // map instead of unordered_map (String has no hash)
#endif
    bool dirty_ = false;
};

} // namespace adversary
