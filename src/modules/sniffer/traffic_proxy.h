/**
 * @file traffic_proxy.h
 * @brief NAT-based traffic proxy for AP modes
 * 
 * Provides transparent traffic forwarding with DNS and HTTP logging
 * for Evil Twin and Karma AP attacks. Alternative to captive portal mode.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>
#include <map>
#include <string>
#include "../../utils/mac_utils.h"

#ifdef ESP32
#include <Arduino.h>
#include <WiFi.h>
#endif

namespace adversary {

// =============================================================================
// Data Structures
// =============================================================================

/**
 * @brief Captured DNS query
 */
struct DNSQuery {
    uint32_t timestamp = 0;
    uint8_t clientMac[6] = {0};
    char domain[128] = {0};
    
    void reset() {
        timestamp = 0;
        memset(clientMac, 0, 6);
        memset(domain, 0, sizeof(domain));
    }
};

/**
 * @brief Captured HTTP request
 */
struct HTTPCapture {
    uint32_t timestamp = 0;
    uint8_t clientMac[6] = {0};
    char method[8] = {0};       // GET, POST, etc.
    char host[64] = {0};        // Host header
    char path[128] = {0};       // URL path
    char cookies[256] = {0};    // Cookie header (truncated)
    char postData[512] = {0};   // POST body (truncated)
    uint16_t postLen = 0;
    
    void reset() {
        timestamp = 0;
        memset(clientMac, 0, 6);
        memset(method, 0, sizeof(method));
        memset(host, 0, sizeof(host));
        memset(path, 0, sizeof(path));
        memset(cookies, 0, sizeof(cookies));
        memset(postData, 0, sizeof(postData));
        postLen = 0;
    }
};

/**
 * @brief Traffic statistics
 */
struct TrafficStats {
    uint32_t totalPackets = 0;
    uint32_t bytesForwarded = 0;
    uint32_t dnsQueries = 0;
    uint32_t httpRequests = 0;
    uint32_t httpsConnections = 0;
    uint32_t uniqueDomains = 0;
    
    void reset() {
        totalPackets = 0;
        bytesForwarded = 0;
        dnsQueries = 0;
        httpRequests = 0;
        httpsConnections = 0;
        uniqueDomains = 0;
    }
};

/**
 * @brief Traffic entry type for display
 */
enum class TrafficEntryType : uint8_t {
    DNS = 0,
    HTTP = 1,
    FORM = 2
};

/**
 * @brief Single traffic entry for per-client feed
 */
struct TrafficEntry {
    uint32_t timestamp = 0;
    TrafficEntryType type = TrafficEntryType::DNS;
    char data[128] = {0};
    
    void reset() {
        timestamp = 0;
        type = TrafficEntryType::DNS;
        memset(data, 0, sizeof(data));
    }
};

/**
 * @brief Per-client traffic tracking
 */
struct ClientTraffic {
    uint8_t mac[6] = {0};
    uint8_t ip[4] = {0};
    uint32_t connectTime = 0;
    uint32_t packetCount = 0;
    uint32_t dnsCount = 0;
    uint32_t httpCount = 0;
    uint32_t formCount = 0;
    std::vector<TrafficEntry> entries;
    static constexpr size_t MAX_ENTRIES = 50;  // Limit entries per client
    
    void addEntry(TrafficEntryType type, const char* data) {
        if (entries.size() >= MAX_ENTRIES) {
            entries.erase(entries.begin());  // Remove oldest
        }
        TrafficEntry entry;
        entry.timestamp = millis();
        entry.type = type;
        strncpy(entry.data, data, sizeof(entry.data) - 1);
        entries.push_back(entry);
        
        switch (type) {
            case TrafficEntryType::DNS: dnsCount++; break;
            case TrafficEntryType::HTTP: httpCount++; break;
            case TrafficEntryType::FORM: formCount++; break;
        }
    }
    
    void reset() {
        memset(mac, 0, 6);
        memset(ip, 0, 4);
        connectTime = 0;
        packetCount = 0;
        dnsCount = 0;
        httpCount = 0;
        formCount = 0;
        entries.clear();
    }
    
    // Get MAC as string for display/map key
    static void macToString(const uint8_t* mac, char* buf, size_t len) {
        utils::formatMacBytes(mac, buf, len);
    }
};

/**
 * @brief Proxy state
 */
enum class ProxyState : uint8_t {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};


// =============================================================================
// TrafficProxy Class
// =============================================================================

/**
 * @brief NAT-based transparent traffic proxy
 * 
 * Acts as a man-in-the-middle between SoftAP clients and upstream WiFi.
 * Logs DNS queries and HTTP requests while forwarding traffic.
 */
class TrafficProxy {
public:
    static TrafficProxy& getInstance();
    
    /**
     * @brief Start the proxy (requires active SoftAP and Station connection)
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop the proxy and NAT routing
     */
    void stop();
    
    /**
     * @brief Update proxy state (call from main loop)
     */
    void update();
    
    /**
     * @brief Get current proxy state
     */
    ProxyState getState() const { return state_; }
    bool isRunning() const { return state_ == ProxyState::RUNNING; }
    
    /**
     * @brief Increment packet count (called externally when KarmaAP handles packets)
     */
    void incrementPackets(uint16_t bytes) { 
        stats_.totalPackets++; 
        stats_.bytesForwarded += bytes;
    }
    
    // Statistics
    const TrafficStats& getStats() const { return stats_; }
    
    // DNS queries (circular buffer)
    const std::vector<DNSQuery>& getDNSQueries() const { return dnsQueries_; }
    size_t getDNSCount() const { return dnsCount_; }
    
    // HTTP captures (circular buffer)
    const std::vector<HTTPCapture>& getHTTPCaptures() const { return httpCaptures_; }
    size_t getHTTPCount() const { return httpCount_; }
    
    // Domain list
    const std::vector<const char*>& getUniqueDomains() const { return uniqueDomains_; }
    
    // Callbacks
    using DNSCallback = std::function<void(const DNSQuery&)>;
    using HTTPCallback = std::function<void(const HTTPCapture&)>;
    
    void setDNSCallback(DNSCallback cb) { dnsCallback_ = cb; }
    void setHTTPCallback(HTTPCallback cb) { httpCallback_ = cb; }
    
    // Per-client traffic tracking
    const std::map<std::string, ClientTraffic>& getClientTraffic() const { return clientTraffic_; }
    ClientTraffic* getClientByMac(const char* macStr);
    void addClientTrafficEntry(const uint8_t* mac, TrafficEntryType type, const char* data);
    void registerClient(const uint8_t* mac, const uint8_t* ip);
    std::vector<std::string> getClientMacList() const;
    
    // Save to SD
    bool saveDNSLog(const char* path);
    bool saveHTTPCaptures(const char* path);
    bool saveClientTraffic(const char* macStr);  // Save single client to captures/traffic/<MAC>.traffic.json
    void autoSave();  // Called from update() to periodically save
    
    // Memory management
    void allocateBuffers();
    void deallocateBuffers();
    
    // Promiscuous mode packet handler (static for C callback)
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    
private:
    TrafficProxy();
    ~TrafficProxy() = default;
    TrafficProxy(const TrafficProxy&) = delete;
    TrafficProxy& operator=(const TrafficProxy&) = delete;
    
    // NAT setup
    bool setupNAT();
    void teardownNAT();
    
    // Packet processing
    void processDNSPacket(const uint8_t* data, uint16_t len, const uint8_t* clientMac);
    void processHTTPPacket(const uint8_t* data, uint16_t len, const uint8_t* clientMac);
    
    // DNS parsing
    bool parseDNSQuery(const uint8_t* data, uint16_t len, char* domain, size_t domainLen);
    
    // Domain tracking
    void addUniqueDomain(const char* domain);
    
    // State
    ProxyState state_ = ProxyState::STOPPED;
    TrafficStats stats_;
    
    // Circular buffers
    static constexpr size_t MAX_DNS_QUERIES = 50;
    static constexpr size_t MAX_HTTP_CAPTURES = 20;
    static constexpr size_t MAX_UNIQUE_DOMAINS = 100;
    
    std::vector<DNSQuery> dnsQueries_;
    size_t dnsHead_ = 0;
    size_t dnsCount_ = 0;
    
    std::vector<HTTPCapture> httpCaptures_;
    size_t httpHead_ = 0;
    size_t httpCount_ = 0;
    
    std::vector<const char*> uniqueDomains_;
    char domainStorage_[MAX_UNIQUE_DOMAINS][64];
    size_t domainCount_ = 0;
    
    // Callbacks
    DNSCallback dnsCallback_;
    HTTPCallback httpCallback_;
    
    // NAT state
    bool natEnabled_ = false;
    bool promiscuousEnabled_ = false;
    
    // Auto-save
    uint32_t lastSaveTime_ = 0;
    static constexpr uint32_t AUTO_SAVE_INTERVAL_MS = 30000;  // Save every 30 seconds
    static constexpr const char* DNS_LOG_PATH = "/adversary/captures/dns_log.txt";
    static constexpr const char* HTTP_LOG_PATH = "/adversary/captures/http_captures.json";
    static constexpr const char* TRAFFIC_DIR = "/adversary/captures/traffic";
    
    // Per-client traffic (keyed by MAC string)
    std::map<std::string, ClientTraffic> clientTraffic_;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline TrafficProxy::TrafficProxy() {
    // Large buffers are allocated in start() to save RAM when idle
}

} // namespace adversary
