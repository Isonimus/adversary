#pragma once

/**
 * @file error_logger.h
 * @brief Lightweight error logging to SD card for field debugging
 * 
 * Provides simple error and warning logging to /adversary/logs/error.log
 * for diagnosing issues when serial output is not available.
 */

#include <cstdio>

#ifdef ESP32
#include <SD.h>
#include <FS.h>
#endif

namespace adversary {
namespace logger {

// Log file path
constexpr const char* ERROR_LOG_PATH = "/adversary/logs/error.log";

/**
 * @brief Log an error message to SD card
 * @param context Module or function context (e.g., "HandshakeSave")
 * @param message Error description
 */
inline void logError(const char* context, const char* message) {
#ifdef ESP32
    // Ensure logs directory exists
    if (!SD.exists("/adversary/logs")) {
        SD.mkdir("/adversary/logs");
    }
    
    File logFile = SD.open(ERROR_LOG_PATH, FILE_APPEND);
    if (!logFile) {
        Serial.printf("[Logger] Failed to open error log\n");
        return;
    }
    
    // Format: [ERROR] context: message | heap=XXXXX
    logFile.printf("[ERROR] %s: %s | heap=%lu\n", 
                   context, message, ESP.getFreeHeap());
    logFile.close();
    
    // Also print to serial if available
    Serial.printf("[ERROR] %s: %s\n", context, message);
#else
    // Native build - just print
    printf("[ERROR] %s: %s\n", context, message);
#endif
}

/**
 * @brief Log a warning message to SD card
 * @param context Module or function context
 * @param message Warning description
 */
inline void logWarning(const char* context, const char* message) {
#ifdef ESP32
    if (!SD.exists("/adversary/logs")) {
        SD.mkdir("/adversary/logs");
    }
    
    File logFile = SD.open(ERROR_LOG_PATH, FILE_APPEND);
    if (!logFile) {
        return;
    }
    
    logFile.printf("[WARN] %s: %s | heap=%lu\n", 
                   context, message, ESP.getFreeHeap());
    logFile.close();
    
    Serial.printf("[WARN] %s: %s\n", context, message);
#else
    printf("[WARN] %s: %s\n", context, message);
#endif
}

/**
 * @brief Log an error with additional numeric context
 * @param context Module or function context
 * @param message Error description
 * @param value Numeric value for context (e.g., error code, size)
 */
inline void logErrorWithValue(const char* context, const char* message, long value) {
#ifdef ESP32
    if (!SD.exists("/adversary/logs")) {
        SD.mkdir("/adversary/logs");
    }
    
    File logFile = SD.open(ERROR_LOG_PATH, FILE_APPEND);
    if (!logFile) {
        return;
    }
    
    logFile.printf("[ERROR] %s: %s (%ld) | heap=%lu\n", 
                   context, message, value, ESP.getFreeHeap());
    logFile.close();
    
    Serial.printf("[ERROR] %s: %s (%ld)\n", context, message, value);
#else
    printf("[ERROR] %s: %s (%ld)\n", context, message, value);
#endif
}

} // namespace logger
} // namespace adversary
