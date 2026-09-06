/**
 * @file littlefs_backend.h
 * @brief LittleFS storage backend for devices without SD card
 * 
 * Uses ESP32's internal flash for persistent storage on M5Stick.
 */

#pragma once

#include "storage_backend.h"

#ifdef ESP32
#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#endif

namespace adversary {

/**
 * @brief LittleFS storage backend for internal flash
 */
class LittleFSBackend : public IStorageBackend {
public:
    static LittleFSBackend& getInstance() {
        static LittleFSBackend instance;
        return instance;
    }
    
    LittleFSBackend(const LittleFSBackend&) = delete;
    LittleFSBackend& operator=(const LittleFSBackend&) = delete;
    
    bool init() override {
#ifdef ESP32
        if (m_initialized) return true;
        
        Serial.println("[LittleFS] Initializing internal flash storage...");
        
        // Format if mount fails (first time)
        if (!LittleFS.begin(true)) {  // true = format if failed
            Serial.println("[LittleFS] Mount failed even after format!");
            m_status = StorageStatus::ERROR;
            return false;
        }
        
        Serial.printf("[LittleFS] Mounted! Total: %u bytes, Used: %u bytes\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());
        
        m_initialized = true;
        m_status = StorageStatus::READY;
        
        // Create directory structure
        createDirectories();
        
        return true;
#else
        return false;
#endif
    }
    
    bool isReady() const override { return m_initialized; }
    StorageStatus status() const override { return m_status; }
    
    bool exists(const char* path) override {
#ifdef ESP32
        if (!m_initialized) return false;
        return LittleFS.exists(path);
#else
        (void)path;
        return false;
#endif
    }
    
    bool mkdir(const char* path) override {
#ifdef ESP32
        if (!m_initialized) return false;
        if (LittleFS.exists(path)) return true;
        return LittleFS.mkdir(path);
#else
        (void)path;
        return false;
#endif
    }
    
    bool remove(const char* path) override {
#ifdef ESP32
        if (!m_initialized) return false;
        return LittleFS.remove(path);
#else
        (void)path;
        return false;
#endif
    }
    
    bool readFile(const char* path, char* buffer, size_t maxSize) override {
#ifdef ESP32
        if (!m_initialized) return false;
        
        File file = LittleFS.open(path, "r");
        if (!file) {
            Serial.printf("[LittleFS] Failed to open %s for reading\n", path);
            return false;
        }
        
        size_t len = file.size();
        if (len >= maxSize) {
            len = maxSize - 1;
        }
        
        file.readBytes(buffer, len);
        buffer[len] = '\0';
        file.close();
        
        return true;
#else
        (void)path; (void)buffer; (void)maxSize;
        return false;
#endif
    }
    
    bool writeFile(const char* path, const char* data, size_t len) override {
#ifdef ESP32
        if (!m_initialized) return false;
        
        File file = LittleFS.open(path, "w");
        if (!file) {
            Serial.printf("[LittleFS] Failed to open %s for writing\n", path);
            return false;
        }
        
        size_t written = file.write((const uint8_t*)data, len);
        file.close();
        
        return written == len;
#else
        (void)path; (void)data; (void)len;
        return false;
#endif
    }
    
    bool appendFile(const char* path, const char* data, size_t len) override {
#ifdef ESP32
        if (!m_initialized) return false;
        
        File file = LittleFS.open(path, "a");
        if (!file) {
            Serial.printf("[LittleFS] Failed to open %s for appending\n", path);
            return false;
        }
        
        size_t written = file.write((const uint8_t*)data, len);
        file.close();
        
        return written == len;
#else
        (void)path; (void)data; (void)len;
        return false;
#endif
    }
    
    size_t totalBytes() const override {
#ifdef ESP32
        return m_initialized ? LittleFS.totalBytes() : 0;
#else
        return 0;
#endif
    }
    
    size_t usedBytes() const override {
#ifdef ESP32
        return m_initialized ? LittleFS.usedBytes() : 0;
#else
        return 0;
#endif
    }

private:
    LittleFSBackend() : m_initialized(false), m_status(StorageStatus::NOT_INITIALIZED) {}
    
    void createDirectories() {
#ifdef ESP32
        // Create standard directories
        mkdir("/adversary");
        mkdir("/adversary/settings");
        mkdir("/adversary/captures");
        mkdir("/adversary/whitelist");
        Serial.println("[LittleFS] Directory structure created");
#endif
    }
    
    bool m_initialized;
    StorageStatus m_status;
};

/**
 * @brief Get the system storage backend
 * 
 * Returns LittleFS on M5Stick, or nullptr (use SD) on other devices.
 */
inline IStorageBackend* getStorageBackend() {
#if defined(TARGET_M5STICK)
    return &LittleFSBackend::getInstance();
#else
    return nullptr;  // Use SD Manager on Cardputer
#endif
}

} // namespace adversary
