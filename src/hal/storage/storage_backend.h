/**
 * @file storage_backend.h
 * @brief Abstract storage interface for file operations
 * 
 * Provides unified file storage using SD (Cardputer) or LittleFS (M5Stick).
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace adversary {

/**
 * @brief Storage backend status
 */
enum class StorageStatus : uint8_t {
    NOT_INITIALIZED,
    READY,
    NO_STORAGE,
    ERROR
};

/**
 * @brief Abstract storage interface
 */
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;
    
    virtual bool init() = 0;
    virtual bool isReady() const = 0;
    virtual StorageStatus status() const = 0;
    
    // File operations
    virtual bool exists(const char* path) = 0;
    virtual bool mkdir(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    
    // Read/write entire file
    virtual bool readFile(const char* path, char* buffer, size_t maxSize) = 0;
    virtual bool writeFile(const char* path, const char* data, size_t len) = 0;
    virtual bool appendFile(const char* path, const char* data, size_t len) = 0;
    
    // Get total and used space
    virtual size_t totalBytes() const = 0;
    virtual size_t usedBytes() const = 0;
};

} // namespace adversary
