/**
 * @file event_bus.cpp
 * @brief EventBus implementation
 */

#include "event_bus.h"
#include <cstdio>

#ifdef ESP32
#include <Arduino.h>
#endif

namespace adversary {

// ============================================================================
// EventData Implementation
// ============================================================================

EventData::EventData(EventType t) : type(t) {
#ifdef ESP32
    timestamp = millis();
#else
    timestamp = 0;
#endif
    memset(&payload, 0, sizeof(payload));
}

// ============================================================================
// Event Type String Conversion
// ============================================================================

const char* eventTypeToString(EventType type) {
    switch (type) {
        // WiFi Scanning
        case EventType::WIFI_SCAN_STARTED:     return "WIFI_SCAN_STARTED";
        case EventType::WIFI_SCAN_COMPLETED:   return "WIFI_SCAN_COMPLETED";
        case EventType::WIFI_NETWORK_FOUND:    return "WIFI_NETWORK_FOUND";
        
        // Attacks
        case EventType::ATTACK_STARTED:        return "ATTACK_STARTED";
        case EventType::ATTACK_STOPPED:        return "ATTACK_STOPPED";
        case EventType::ATTACK_STATE_CHANGED:  return "ATTACK_STATE_CHANGED";
        case EventType::DEAUTH_PACKET_SENT:    return "DEAUTH_PACKET_SENT";
        case EventType::BEACON_SPAM_TICK:      return "BEACON_SPAM_TICK";
        case EventType::PROBE_FLOOD_TICK:      return "PROBE_FLOOD_TICK";
        
        // Handshake
        case EventType::HANDSHAKE_CAPTURED:    return "HANDSHAKE_CAPTURED";
        case EventType::HANDSHAKE_SAVED:       return "HANDSHAKE_SAVED";
        case EventType::PMKID_CAPTURED:        return "PMKID_CAPTURED";
        case EventType::EAPOL_CAPTURED:        return "EAPOL_CAPTURED";
        
        // Evil Twin / Karma
        case EventType::CREDENTIAL_CAPTURED:   return "CREDENTIAL_CAPTURED";
        case EventType::CLIENT_CONNECTED:      return "CLIENT_CONNECTED";
        case EventType::CLIENT_DISCONNECTED:   return "CLIENT_DISCONNECTED";
        case EventType::PROBE_REQUEST_RECEIVED: return "PROBE_REQUEST_RECEIVED";
        case EventType::AP_STARTED:            return "AP_STARTED";
        case EventType::AP_STOPPED:            return "AP_STOPPED";
        
        
        // Sniffer
        case EventType::PACKET_CAPTURED:       return "PACKET_CAPTURED";
        case EventType::SNIFFER_STARTED:       return "SNIFFER_STARTED";
        case EventType::SNIFFER_STOPPED:       return "SNIFFER_STOPPED";
        
        // BLE
        case EventType::BLE_DEVICE_FOUND:      return "BLE_DEVICE_FOUND";
        case EventType::BLE_SCAN_STARTED:      return "BLE_SCAN_STARTED";
        case EventType::BLE_SCAN_STOPPED:      return "BLE_SCAN_STOPPED";
        case EventType::BLE_ATTACK_STARTED:    return "BLE_ATTACK_STARTED";
        case EventType::BLE_ATTACK_STOPPED:    return "BLE_ATTACK_STOPPED";
        case EventType::BLE_CONNECTED:         return "BLE_CONNECTED";
        case EventType::BLE_DISCONNECTED:      return "BLE_DISCONNECTED";
        
        // RFID
        case EventType::RFID_TAG_DETECTED:     return "RFID_TAG_DETECTED";
        case EventType::RFID_TAG_LOST:         return "RFID_TAG_LOST";
        case EventType::RFID_AUDIT_PROGRESS:   return "RFID_AUDIT_PROGRESS";
        case EventType::RFID_AUDIT_COMPLETED:  return "RFID_AUDIT_COMPLETED";
        case EventType::RFID_KEY_FOUND:        return "RFID_KEY_FOUND";
        
        // Wardriving
        case EventType::WARDRIVING_STARTED:    return "WARDRIVING_STARTED";
        case EventType::WARDRIVING_STOPPED:    return "WARDRIVING_STOPPED";
        case EventType::WARDRIVING_NETWORK_LOGGED: return "WARDRIVING_NETWORK_LOGGED";
        
        // System
        case EventType::STATE_CHANGED:         return "STATE_CHANGED";
        case EventType::GPS_FIX_ACQUIRED:      return "GPS_FIX_ACQUIRED";
        case EventType::GPS_FIX_LOST:          return "GPS_FIX_LOST";
        case EventType::BATTERY_LOW:           return "BATTERY_LOW";
        case EventType::BATTERY_CRITICAL:      return "BATTERY_CRITICAL";
        case EventType::SD_ERROR:              return "SD_ERROR";
        case EventType::SD_READY:              return "SD_READY";
        case EventType::WIFI_CONNECTED:        return "WIFI_CONNECTED";
        case EventType::WIFI_DISCONNECTED:     return "WIFI_DISCONNECTED";
        
        // UI
        case EventType::SCREEN_CHANGED:        return "SCREEN_CHANGED";
        case EventType::TOAST_SHOWN:           return "TOAST_SHOWN";
        case EventType::MENU_ACTION:           return "MENU_ACTION";
        
        // IR
        case EventType::IR_TRANSMISSION_STARTED:   return "IR_TRANSMISSION_STARTED";
        case EventType::IR_TRANSMISSION_COMPLETED: return "IR_TRANSMISSION_COMPLETED";
        
        default:                               return "UNKNOWN_EVENT";
    }
}

// ============================================================================
// EventBus Implementation
// ============================================================================

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

EventBus::EventBus() 
    : nextId_(1)
    , debugLogging_(false) 
{
#ifdef ESP32
    queueMutex_ = xSemaphoreCreateMutex();
#endif
    handlerMap_.reserve(32);  // Pre-allocate for typical usage
}

EventBus::~EventBus() {
#ifdef ESP32
    if (queueMutex_) {
        vSemaphoreDelete(queueMutex_);
    }
#endif
}

void EventBus::reset() {
#ifdef ESP32
    if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
#endif
        for (int i = 0; i < NUM_BUCKETS; ++i) {
            buckets_[i].clear();
        }
        handlerMap_.clear();
        eventQueue_.clear();
        nextId_ = 1;
#ifdef ESP32
        xSemaphoreGive(queueMutex_);
    }
#endif
}

void EventBus::releaseMemory() {
#ifdef ESP32
    if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
#endif
        // Shrink event queue to minimum
        eventQueue_.clear();
        eventQueue_.shrink_to_fit();
#ifdef ESP32
        xSemaphoreGive(queueMutex_);
    }
#endif
}

size_t EventBus::typeIndex(EventType type) {
    // Map event type to bucket index based on ranges
    uint16_t t = static_cast<uint16_t>(type);
    if (t < 110) return 0;       // WiFi (100-109)
    if (t < 220) return 1;       // Attacks (200-219)
    if (t < 230) return 2;       // Handshake (220-229)
    if (t < 250) return 3;       // AP (230-249)
    if (t < 260) return 4;       // Sniffer (250-259)
    if (t < 320) return 5;       // BLE (300-319)
    if (t < 340) return 6;       // RFID/Wardriving (320-339)
    if (t < 420) return 7;       // System (400-419)
    if (t < 440) return 8;       // UI/IR (420-439)
    return 9;                    // Other
}

EventBus::HandlerId EventBus::subscribe(EventType type, Handler handler) {
    if (!handler) {
        return INVALID_HANDLER_ID;
    }
    
    size_t bucket = typeIndex(type);
    HandlerId id = nextId_++;
    
    // Prevent ID overflow (unlikely but safe)
    if (nextId_ == INVALID_HANDLER_ID) {
        nextId_ = 1;
    }
    
    buckets_[bucket].push_back({id, handler});
    handlerMap_.push_back({id, {bucket, type}});
    
#ifdef ESP32
    if (debugLogging_) {
        Serial.printf("[EventBus] Subscribed id=%u to %s (bucket %zu)\n", 
                      id, eventTypeToString(type), bucket);
    }
#endif
    
    return id;
}

bool EventBus::unsubscribe(HandlerId id) {
    if (id == INVALID_HANDLER_ID) {
        return false;
    }
    
    // Find handler info
    for (auto it = handlerMap_.begin(); it != handlerMap_.end(); ++it) {
        if (it->first == id) {
            size_t bucket = it->second.bucket;
            
            // Remove from bucket
            auto& subs = buckets_[bucket];
            for (auto subIt = subs.begin(); subIt != subs.end(); ++subIt) {
                if (subIt->id == id) {
                    subs.erase(subIt);
                    break;
                }
            }
            
            // Remove from handler map
            handlerMap_.erase(it);
            
#ifdef ESP32
            if (debugLogging_) {
                Serial.printf("[EventBus] Unsubscribed id=%u\n", id);
            }
#endif
            return true;
        }
    }
    
    return false;
}


void EventBus::publish(const EventData& event) {
    size_t bucket = typeIndex(event.type);
    
    
    // Invoke all handlers for this event type
    for (const auto& sub : buckets_[bucket]) {
        // Check if this subscription matches the event type
        // (bucket may contain multiple event types)
        for (const auto& mapping : handlerMap_) {
            if (mapping.first == sub.id && mapping.second.type == event.type) {
                sub.handler(event);
                break;
            }
        }
    }
}

bool EventBus::queue(const EventData& event) {
#ifdef ESP32
    if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
#endif
    
    bool success = false;
    if (eventQueue_.size() < MAX_QUEUE_SIZE) {
        eventQueue_.push_back(event);
        success = true;
    }
    
#ifdef ESP32
    xSemaphoreGive(queueMutex_);
#endif
    
#ifdef ESP32
    if (debugLogging_ && success) {
        Serial.printf("[EventBus] Queued %s (queue size: %zu)\n", 
                      eventTypeToString(event.type), eventQueue_.size());
    }
#endif
    
    return success;
}

void EventBus::processQueue() {
    std::vector<EventData> toProcess;
    
#ifdef ESP32
    if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
#endif
        toProcess = std::move(eventQueue_);
        eventQueue_.clear();
        // NOTE: No reserve() here — it fragments heap by placing a ~2.8KB block
        // in the middle of free memory, preventing 64KB canvas restoration.
#ifdef ESP32
        xSemaphoreGive(queueMutex_);
    }
#endif
    
    for (const auto& event : toProcess) {
        publish(event);
    }
}

size_t EventBus::getSubscriberCount(EventType type) const {
    size_t count = 0;
    for (const auto& mapping : handlerMap_) {
        if (mapping.second.type == type) {
            count++;
        }
    }
    return count;
}

size_t EventBus::getTotalSubscriptions() const {
    return handlerMap_.size();
}

size_t EventBus::getQueueSize() const {
#ifdef ESP32
    size_t size = 0;
    if (xSemaphoreTake(const_cast<SemaphoreHandle_t>(queueMutex_), pdMS_TO_TICKS(10)) == pdTRUE) {
        size = eventQueue_.size();
        xSemaphoreGive(const_cast<SemaphoreHandle_t>(queueMutex_));
    }
    return size;
#else
    return eventQueue_.size();
#endif
}

} // namespace adversary
