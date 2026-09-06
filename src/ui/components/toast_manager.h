/**
 * @file toast_manager.h
 * @brief Centralized toast notification manager for visual feedback
 * 
 * Provides non-blocking visual toast notifications with priority queue.
 * Toasts auto-dismiss after configurable duration.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "ui/theme.h"

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

/**
 * @brief Toast notification types with associated colors
 */
enum class ToastType : uint8_t {
    INFO,       ///< Neutral information (blue)
    SUCCESS,    ///< Operation completed (green)
    WARNING,    ///< Caution/attention (orange)
    ERROR       ///< Failure/error (red)
};

/**
 * @brief Toast display position
 */
enum class ToastPosition : uint8_t {
    TOP = 0,
    CENTER = 1,
    BOTTOM = 2
};

/**
 * @brief Toast priority levels
 */
enum class ToastPriority : uint8_t {
    PRIORITY_LOW = 0,
    PRIORITY_MEDIUM = 1,
    PRIORITY_HIGH = 2
};

/**
 * @brief Centralized toast notification manager
 * 
 * Singleton for displaying visual toast messages across all screens.
 * Supports priority queue for managing multiple notifications.
 */
class ToastManager {
public:
    static ToastManager& getInstance() {
        static ToastManager instance;
        return instance;
    }
    
    // Prevent copying
    ToastManager(const ToastManager&) = delete;
    ToastManager& operator=(const ToastManager&) = delete;
    
    /**
     * @brief Show a toast notification
     * @param message Text to display (max 63 chars)
     * @param type Toast type (affects color)
     * @param priority Queue priority
     * @param durationMs Auto-dismiss duration
     */
    void show(const char* message, ToastType type = ToastType::INFO,
              ToastPriority priority = ToastPriority::PRIORITY_MEDIUM,
              uint16_t durationMs = 2000) {
        enqueue(message, type, priority, durationMs);
        
        // If not currently showing, start showing
        if (!visible_) {
            showNext();
        }
    }
    
    /**
     * @brief Hide current toast immediately
     */
    void hide() {
        visible_ = false;
        showNext();  // Show next in queue if any
    }
    
    /**
     * @brief Check if a toast is visible
     */
    bool isVisible() const { return visible_; }
    
    /**
     * @brief Update auto-dismiss timer (call in main loop)
     */
    void update() {
#ifdef ESP32
        if (!visible_) return;
        
        uint32_t now = millis();
        if (now - showStartTime_ >= currentDurationMs_) {
            hide();
        }
#endif
    }
    
    /**
     * @brief Set toast display position
     */
    void setPosition(ToastPosition pos) { position_ = pos; }
    ToastPosition getPosition() const { return position_; }
    
    /**
     * @brief Render the toast (call after screen render)
     */
    template<typename Canvas>
    void render(Canvas& canvas) {
        if (!visible_) return;
        
        int16_t screenWidth = canvas.width();
        int16_t screenHeight = canvas.height();
        
        // Calculate toast dimensions
        int16_t textWidth = strlen(currentMessage_) * 6;
        int16_t toastWidth = textWidth + 24;  // Padding
        int16_t toastHeight = 20;
        
        // Clamp width to screen
        if (toastWidth > screenWidth - 20) {
            toastWidth = screenWidth - 20;
        }
        
        // Calculate position
        int16_t x = (screenWidth - toastWidth) / 2;
        int16_t y;
        
        switch (position_) {
            case ToastPosition::TOP:
                y = 24;  // Below header
                break;
            case ToastPosition::CENTER:
                y = (screenHeight - toastHeight) / 2;
                break;
            case ToastPosition::BOTTOM:
            default:
                y = screenHeight - toastHeight - 20;  // Above footer
                break;
        }
        
        // Get color based on type - used for text and border
        uint16_t typeColor = getTypeColor(currentType_);
        uint16_t bgColor = theme::BG_PRIMARY();
        
        // Draw toast background with rounded corners
        canvas.fillRoundRect(x, y, toastWidth, toastHeight, 4, bgColor);
        canvas.drawRoundRect(x, y, toastWidth, toastHeight, 4, typeColor);
        
        // Draw icon based on type (colored)
        canvas.setTextColor(typeColor);
        canvas.setTextSize(1);
        canvas.setCursor(x + 6, y + 6);
        
        switch (currentType_) {
            case ToastType::SUCCESS: {
                // Draw a checkmark with lines — the 0x10 glyph isn't in this font,
                // so it rendered blank (unlike the printable i/X/! icons). Two
                // offset strokes give it a little weight.
                int16_t ix = x + 6, iy = y + 6;
                canvas.drawLine(ix + 1, iy + 4, ix + 3, iy + 6, typeColor);
                canvas.drawLine(ix + 3, iy + 6, ix + 7, iy + 1, typeColor);
                canvas.drawLine(ix + 1, iy + 5, ix + 3, iy + 7, typeColor);
                canvas.drawLine(ix + 3, iy + 7, ix + 7, iy + 2, typeColor);
                break;
            }
            case ToastType::ERROR:
                canvas.print("X");
                break;
            case ToastType::WARNING:
                canvas.print("!");
                break;
            case ToastType::INFO:
            default:
                canvas.print("i");
                break;
        }
        
        // Draw message
        canvas.setCursor(x + 18, y + 6);
        
        // Truncate if too long
        int16_t maxChars = (toastWidth - 24) / 6;
        if ((int16_t)strlen(currentMessage_) > maxChars) {
            char truncated[64];
            strncpy(truncated, currentMessage_, maxChars - 3);
            truncated[maxChars - 3] = '\0';
            strcat(truncated, "...");
            canvas.print(truncated);
        } else {
            canvas.print(currentMessage_);
        }
    }

private:
    ToastManager() : visible_(false), position_(ToastPosition::TOP), queueCount_(0) {
        currentMessage_[0] = '\0';
    }
    
    /**
     * @brief Get color for toast type
     */
    uint16_t getTypeColor(ToastType type) const {
        switch (type) {
            case ToastType::SUCCESS: return theme::SUCCESS();
            case ToastType::ERROR:   return theme::ERROR();
            case ToastType::WARNING: return theme::WARNING();
            case ToastType::INFO:
            default:                 return theme::INFO();
        }
    }
    
    /**
     * @brief Queued toast structure
     */
    struct QueuedToast {
        char message[64];
        ToastType type;
        ToastPriority priority;
        uint16_t durationMs;
    };
    
    /**
     * @brief Enqueue a toast with priority ordering
     */
    void enqueue(const char* message, ToastType type, ToastPriority priority, uint16_t durationMs) {
        if (queueCount_ >= QUEUE_SIZE) {
            // Queue full - drop lowest priority or oldest if same priority
            // For simplicity, just drop oldest
            for (uint8_t i = 0; i < queueCount_ - 1; i++) {
                queue_[i] = queue_[i + 1];
            }
            queueCount_--;
        }
        
        // Find insertion point based on priority
        uint8_t insertIdx = queueCount_;
        for (uint8_t i = 0; i < queueCount_; i++) {
            if (static_cast<uint8_t>(priority) > static_cast<uint8_t>(queue_[i].priority)) {
                insertIdx = i;
                break;
            }
        }
        
        // Shift items to make room
        for (uint8_t i = queueCount_; i > insertIdx; i--) {
            queue_[i] = queue_[i - 1];
        }
        
        // Insert new toast
        strncpy(queue_[insertIdx].message, message, 63);
        queue_[insertIdx].message[63] = '\0';
        queue_[insertIdx].type = type;
        queue_[insertIdx].priority = priority;
        queue_[insertIdx].durationMs = durationMs;
        queueCount_++;
    }
    
    /**
     * @brief Show next toast from queue
     */
    void showNext() {
        if (queueCount_ == 0) {
            visible_ = false;
            return;
        }
        
        // Dequeue first item (highest priority)
        strncpy(currentMessage_, queue_[0].message, 63);
        currentMessage_[63] = '\0';
        currentType_ = queue_[0].type;
        currentDurationMs_ = queue_[0].durationMs;
        
        // Shift queue
        for (uint8_t i = 0; i < queueCount_ - 1; i++) {
            queue_[i] = queue_[i + 1];
        }
        queueCount_--;
        
#ifdef ESP32
        showStartTime_ = millis();
#endif
        visible_ = true;
    }
    
    // Current toast state
    bool visible_;
    char currentMessage_[64];
    ToastType currentType_;
    uint16_t currentDurationMs_;
    uint32_t showStartTime_;
    
    // Settings
    ToastPosition position_;
    
    // Priority queue
    static constexpr uint8_t QUEUE_SIZE = 4;
    QueuedToast queue_[QUEUE_SIZE];
    uint8_t queueCount_;
};

// Convenience functions
inline void showToast(const char* message, ToastType type = ToastType::INFO) {
    ToastManager::getInstance().show(message, type, ToastPriority::PRIORITY_MEDIUM, 2500);
}

inline void showSuccessToast(const char* message) {
    ToastManager::getInstance().show(message, ToastType::SUCCESS, ToastPriority::PRIORITY_MEDIUM, 2500);
}

inline void showErrorToast(const char* message) {
    ToastManager::getInstance().show(message, ToastType::ERROR, ToastPriority::PRIORITY_HIGH, 3000);
}

inline void showWarningToast(const char* message) {
    ToastManager::getInstance().show(message, ToastType::WARNING, ToastPriority::PRIORITY_HIGH, 3000);
}

} // namespace adversary
