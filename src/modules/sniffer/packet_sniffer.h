/**
 * @file packet_sniffer.h
 * @brief 802.11 Packet Sniffer Module
 * 
 * Captures raw WiFi packets using ESP32 promiscuous mode.
 * Supports filtering by frame type, channel, and MAC address.
 */

#ifndef ADVERSARY_PACKET_SNIFFER_H
#define ADVERSARY_PACKET_SNIFFER_H

#include <cstdint>
#include <functional>
#include <vector>
#include <string>

#ifdef ARDUINO
#include <esp_wifi_types.h>
#endif

namespace adversary {

/**
 * @brief 802.11 frame types
 */
enum class FrameType : uint8_t {
    MANAGEMENT = 0,
    CONTROL = 1,
    DATA = 2,
    EXTENSION = 3
};

/**
 * @brief 802.11 management frame subtypes
 */
enum class ManagementSubtype : uint8_t {
    ASSOCIATION_REQ = 0,
    ASSOCIATION_RESP = 1,
    REASSOCIATION_REQ = 2,
    REASSOCIATION_RESP = 3,
    PROBE_REQ = 4,
    PROBE_RESP = 5,
    TIMING_ADV = 6,
    BEACON = 8,
    ATIM = 9,
    DISASSOCIATION = 10,
    AUTHENTICATION = 11,
    DEAUTHENTICATION = 12,
    ACTION = 13,
    ACTION_NO_ACK = 14
};

/**
 * @brief 802.11 data frame subtypes
 */
enum class DataSubtype : uint8_t {
    DATA = 0,
    DATA_CF_ACK = 1,
    DATA_CF_POLL = 2,
    DATA_CF_ACK_POLL = 3,
    NULL_DATA = 4,
    CF_ACK = 5,
    CF_POLL = 6,
    CF_ACK_POLL = 7,
    QOS_DATA = 8,
    QOS_DATA_CF_ACK = 9,
    QOS_DATA_CF_POLL = 10,
    QOS_DATA_CF_ACK_POLL = 11,
    QOS_NULL = 12,
    QOS_CF_POLL = 14,
    QOS_CF_ACK_POLL = 15
};

/**
 * @brief Captured packet information
 */
struct CapturedPacket {
    uint8_t* data;              ///< Raw packet data (caller must not free)
    uint16_t length;            ///< Packet length in bytes
    int8_t rssi;                ///< Signal strength
    uint8_t channel;            ///< Channel packet was received on
    uint32_t timestamp;         ///< Capture timestamp (micros)
    FrameType frameType;        ///< 802.11 frame type
    uint8_t subtype;            ///< Frame subtype
    
    // Parsed addresses (from header)
    uint8_t addr1[6];           ///< Address 1 (receiver/destination)
    uint8_t addr2[6];           ///< Address 2 (transmitter/source)
    uint8_t addr3[6];           ///< Address 3 (BSSID or other)
    
    /**
     * @brief Check if this is an EAPOL (handshake) frame
     */
    bool isEAPOL() const;
    
    /**
     * @brief Check if this is a beacon frame
     */
    bool isBeacon() const;
    
    /**
     * @brief Check if this is a deauth frame
     */
    bool isDeauth() const;
    
    /**
     * @brief Check if this is a probe request
     */
    bool isProbeRequest() const;
    
    /**
     * @brief Check if this is a probe response
     */
    bool isProbeResponse() const;
    
    /**
     * @brief Get frame type as string
     */
    const char* getFrameTypeString() const;
};

/**
 * @brief Sniffer filter configuration
 */
struct SnifferFilter {
    bool captureManagement = true;   ///< Capture management frames
    bool captureControl = false;     ///< Capture control frames
    bool captureData = true;         ///< Capture data frames
    bool captureBeacons = false;     ///< Include beacon frames (can be noisy)
    bool captureProbes = true;       ///< Include probe requests/responses
    bool captureEAPOL = true;        ///< Capture EAPOL (handshake) frames
    bool captureDeauth = true;       ///< Capture deauth/disassoc frames
    
    uint8_t targetBSSID[6] = {0};    ///< Filter by BSSID (all zeros = any)
    uint8_t targetChannel = 0;       ///< Filter by channel (0 = all/hopping)
    
    /**
     * @brief Check if BSSID filter is set
     */
    bool hasBSSIDFilter() const;
    
    /**
     * @brief Clear BSSID filter
     */
    void clearBSSIDFilter();
    
    /**
     * @brief Set BSSID filter
     */
    void setBSSIDFilter(const uint8_t* bssid);
};

/**
 * @brief Sniffer state
 */
enum class SnifferState : uint8_t {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};

/**
 * @brief Sniffer statistics
 */
struct SnifferStats {
    uint32_t totalPackets = 0;       ///< Total packets captured
    uint32_t managementFrames = 0;   ///< Management frames count
    uint32_t controlFrames = 0;      ///< Control frames count
    uint32_t dataFrames = 0;         ///< Data frames count
    uint32_t beacons = 0;            ///< Beacon count
    uint32_t probeRequests = 0;      ///< Probe request count
    uint32_t probeResponses = 0;     ///< Probe response count
    uint32_t deauthFrames = 0;       ///< Deauth/disassoc count
    uint32_t eapolFrames = 0;        ///< EAPOL frames (handshake)
    uint32_t droppedPackets = 0;     ///< Packets dropped (buffer full)
    uint32_t startTime = 0;          ///< Capture start time
    uint8_t currentChannel = 0;      ///< Current channel
    
    /**
     * @brief Reset all statistics
     */
    void reset();
    
    /**
     * @brief Get capture duration in seconds
     */
    uint32_t getDurationSeconds() const;
    
    /**
     * @brief Get packets per second rate
     */
    float getPacketsPerSecond() const;
};

/**
 * @brief Packet sniffer using ESP32 promiscuous mode
 * 
 * Captures raw 802.11 frames with optional filtering.
 * Supports channel hopping for comprehensive capture.
 */
class PacketSniffer {
public:
    /// Callback for each captured packet (internal use for screens)
    using PacketCallback = std::function<void(const CapturedPacket&)>;
    
    /**
     * @brief Get singleton instance
     */
    static PacketSniffer& getInstance();
    
    // Prevent copying
    PacketSniffer(const PacketSniffer&) = delete;
    PacketSniffer& operator=(const PacketSniffer&) = delete;
    
    /**
     * @brief Initialize the sniffer
     * @return true if successful
     */
    bool init();
    
    /**
     * @brief Start packet capture
     * @param callback Optional callback for internal packet processing
     * @return true if started successfully
     */
    bool start(PacketCallback callback = nullptr);
    
    /**
     * @brief Stop packet capture
     */
    void stop();
    
    /**
     * @brief Check and process any pending operations
     */
    void update();
    
    /**
     * @brief Get current state
     */
    SnifferState getState() const { return m_state; }
    
    /**
     * @brief Check if sniffer is running
     */
    bool isRunning() const { return m_state == SnifferState::RUNNING; }
    
    /**
     * @brief Get current filter settings
     */
    SnifferFilter& getFilter() { return m_filter; }
    const SnifferFilter& getFilter() const { return m_filter; }
    
    /**
     * @brief Set filter settings
     */
    void setFilter(const SnifferFilter& filter) { m_filter = filter; }
    
    /**
     * @brief Get capture statistics
     */
    const SnifferStats& getStats() const { return m_stats; }
    
    /**
     * @brief Reset statistics
     */
    void resetStats() { m_stats.reset(); }
    
    /**
     * @brief Set channel
     * @param channel WiFi channel (1-14)
     */
    bool setChannel(uint8_t channel);
    
    /**
     * @brief Get current channel
     */
    uint8_t getChannel() const { return m_stats.currentChannel; }
    
    /**
     * @brief Enable/disable channel hopping
     * @param enable Enable hopping
     * @param intervalMs Hop interval in milliseconds
     */
    void setChannelHopping(bool enable, uint32_t intervalMs = 200);
    
    /**
     * @brief Check if channel hopping is enabled
     */
    bool isChannelHopping() const { return m_hopEnabled; }
    
    /**
     * @brief Target a specific BSSID for capture
     */
    void targetBSSID(const uint8_t* bssid);
    
    /**
     * @brief Clear BSSID targeting
     */
    void clearTarget();

private:
    PacketSniffer();
    ~PacketSniffer();
    
    /**
     * @brief Promiscuous mode callback (static for C API)
     */
#ifdef ARDUINO
    static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#else
    static void promiscuousCallback(void* buf, uint16_t type);
#endif
    
    /**
     * @brief Process a received packet
     */
    void processPacket(void* buf, uint16_t type);
    
    /**
     * @brief Parse 802.11 header and validate against ESP32 type
     * @param espType The wifi_promiscuous_pkt_type_t from ESP32
     */
    bool parseHeader(const uint8_t* data, uint16_t len, CapturedPacket& packet, uint16_t espType);
    
    /**
     * @brief Check if packet passes filter
     */
    bool passesFilter(const CapturedPacket& packet);
    
    /**
     * @brief Hop to next channel
     */
    void hopChannel();
    
    SnifferState m_state;
    SnifferFilter m_filter;
    SnifferStats m_stats;
    PacketCallback m_callback;  // Internal callback for screens
    
    bool m_initialized;
    bool m_hopEnabled;
    uint32_t m_hopInterval;
    uint32_t m_lastHopTime;
    uint8_t m_hopChannelIndex;
    
    static constexpr uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10};
    static constexpr uint8_t HOP_CHANNEL_COUNT = 14;
};

} // namespace adversary

#endif // ADVERSARY_PACKET_SNIFFER_H
