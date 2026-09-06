/**
 * @file event_bus.h
 * @brief Decoupled publish/subscribe event system
 * 
 * The EventBus enables loose coupling between modules by providing
 * a central event routing mechanism. Modules publish events without
 * knowing who will handle them, and screens/handlers subscribe to
 * events they care about.
 * 
 * Thread Safety:
 * - publish() is safe to call from any task
 * - queue() is safe to call from ISRs (uses mutex-protected queue)
 * - processQueue() should only be called from main loop
 * 
 * Example:
 * @code
 * // Publisher (in module)
 * EventData event(EventType::HANDSHAKE_CAPTURED);
 * strncpy(event.payload.handshake.filename, filename, 63);
 * EventBus::getInstance().publish(event);
 * 
 * // Subscriber (in screen)
 * handshakeSubId_ = EventBus::getInstance().subscribe(
 *     EventType::HANDSHAKE_CAPTURED,
 *     [this](const EventData& e) {
 *         showNotification(e.payload.handshake.filename);
 *     }
 * );
 * 
 * // Cleanup (in destructor)
 * EventBus::getInstance().unsubscribe(handshakeSubId_);
 * @endcode
 */

#pragma once

#include <functional>
#include <vector>
#include <cstdint>
#include "event_data.h"

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace adversary {

/**
 * @brief Central event bus for decoupled module communication
 */
class EventBus {
public:
    /**
     * @brief Event handler function type
     */
    using Handler = std::function<void(const EventData&)>;
    
    /**
     * @brief Unique identifier for a subscription
     */
    using HandlerId = uint16_t;
    
    /**
     * @brief Invalid handler ID constant
     */
    static constexpr HandlerId INVALID_HANDLER_ID = 0;
    
    /**
     * @brief Get singleton instance
     */
    static EventBus& getInstance();
    
    // Delete copy/move
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    
    /**
     * @brief Subscribe to an event type
     * @param type Event type to listen for
     * @param handler Callback function invoked when event is published
     * @return Handler ID for unsubscribing, or INVALID_HANDLER_ID on failure
     */
    HandlerId subscribe(EventType type, Handler handler);
    
    /**
     * @brief Unsubscribe from events
     * @param id Handler ID returned from subscribe()
     * @return true if handler was found and removed
     */
    bool unsubscribe(HandlerId id);
    
    /**
     * @brief Publish an event immediately
     * 
     * Calls all subscribed handlers synchronously. Safe to call from
     * any FreeRTOS task, but handlers should be fast to avoid blocking.
     * 
     * @param event Event data to publish
     */
    void publish(const EventData& event);
    
    /**
     * @brief Queue an event for deferred processing
     * 
     * Use this from ISRs or when you don't want to block on handler
     * execution. Events are processed on next processQueue() call.
     * 
     * @param event Event data to queue
     * @return true if event was queued, false if queue is full
     */
    bool queue(const EventData& event);
    
    /**
     * @brief Process all queued events
     * 
     * Call this from the main loop. Processes all pending events
     * and invokes their handlers.
     */
    void processQueue();
    
    /**
     * @brief Get number of subscribers for an event type
     * @param type Event type to query
     * @return Number of active subscriptions
     */
    size_t getSubscriberCount(EventType type) const;
    
    /**
     * @brief Get total number of subscriptions across all event types
     */
    size_t getTotalSubscriptions() const;
    
    /**
     * @brief Get number of pending queued events
     */
    size_t getQueueSize() const;
    
    /**
     * @brief Enable/disable debug logging
     */
    void setDebugLogging(bool enabled) { debugLogging_ = enabled; }
    
    /**
     * @brief Reset the EventBus state (for testing)
     * 
     * Clears all subscriptions and the event queue.
     */
    void reset();

    /**
     * @brief Release all non-essential memory (shrink vectors)
     * 
     * Called during memory-critical operations like canvas restoration
     * to free small heap fragments that prevent large contiguous allocations.
     */
    void releaseMemory();

private:
    EventBus();
    ~EventBus();
    
    /**
     * @brief Subscription entry
     */
    struct Subscription {
        HandlerId id;
        Handler handler;
    };
    
    /**
     * @brief Get event type index for array access
     */
    static size_t typeIndex(EventType type);
    
    // Subscription storage - buckets by event type range
    // Using buckets instead of sparse array to save memory
    static constexpr size_t NUM_BUCKETS = 10;
    std::vector<Subscription> buckets_[NUM_BUCKETS];
    
    // Map from handler ID to (bucket, event type) for O(1) unsubscribe
    struct HandlerInfo {
        size_t bucket;
        EventType type;
    };
    std::vector<std::pair<HandlerId, HandlerInfo>> handlerMap_;
    
    // Event queue for deferred processing
    static constexpr size_t MAX_QUEUE_SIZE = 16;
    std::vector<EventData> eventQueue_;
    
#ifdef ESP32
    SemaphoreHandle_t queueMutex_;
#endif
    
    HandlerId nextId_;
    bool debugLogging_;
};

} // namespace adversary
