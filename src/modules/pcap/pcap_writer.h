/**
 * @file pcap_writer.h
 * @brief PCAP file writer for packet captures
 * 
 * Writes captured packets in PCAP format compatible with
 * Wireshark and other packet analysis tools.
 */

#ifndef ADVERSARY_PCAP_WRITER_H
#define ADVERSARY_PCAP_WRITER_H

#include <cstdint>
#include <string>
#include <FS.h>

namespace adversary {

// Forward declaration
struct CapturedPacket;

/**
 * @brief PCAP file global header (24 bytes)
 */
struct __attribute__((packed)) PcapGlobalHeader {
    uint32_t magic_number;   ///< Magic number (0xa1b2c3d4 or 0xa1b23c4d for ns)
    uint16_t version_major;  ///< Major version (2)
    uint16_t version_minor;  ///< Minor version (4)
    int32_t  thiszone;       ///< GMT to local correction
    uint32_t sigfigs;        ///< Accuracy of timestamps
    uint32_t snaplen;        ///< Max length of captured packets
    uint32_t network;        ///< Data link type (105 = IEEE 802.11)
};

/**
 * @brief PCAP packet header (16 bytes)
 */
struct __attribute__((packed)) PcapPacketHeader {
    uint32_t ts_sec;         ///< Timestamp seconds
    uint32_t ts_usec;        ///< Timestamp microseconds
    uint32_t incl_len;       ///< Number of bytes saved
    uint32_t orig_len;       ///< Original packet length
};

/**
 * @brief Minimal radiotap header for 802.11 captures
 * 
 * This is a minimal 8-byte header with NO optional fields.
 * Based on M5Porkchop implementation for WPA-SEC compatibility.
 * github.com/0ct0sec/M5Porkchop
 */
struct __attribute__((packed)) RadiotapHeader {
    uint8_t  it_version;     ///< Radiotap version (always 0)
    uint8_t  it_pad;         ///< Padding for alignment
    uint16_t it_len;         ///< Total header length (8 bytes)
    uint32_t it_present;     ///< Bitmask of present fields (0 = none)
    
    // Initialize minimal header (8 bytes, no optional fields)
    void init(uint8_t channel = 1) {
        (void)channel;  // Not used in minimal header
        it_version = 0;
        it_pad = 0;
        it_len = 8;      // 8 bytes total (minimal)
        it_present = 0;  // No optional fields
    }
};

/**
 * @brief PCAP link types
 */
enum class PcapLinkType : uint32_t {
    ETHERNET = 1,            ///< Ethernet (not used for WiFi)
    IEEE802_11 = 105,        ///< Raw 802.11 frames
    IEEE802_11_RADIOTAP = 127, ///< 802.11 with radiotap header
    IEEE802_11_PRISM = 119   ///< 802.11 with Prism header
};

/**
 * @brief PCAP writer state
 */
enum class PcapWriterState : uint8_t {
    CLOSED,
    OPEN,
    ERROR
};

/**
 * @brief PCAP file writer
 * 
 * Creates and writes PCAP format files to SD card.
 * Compatible with Wireshark, tcpdump, and other tools.
 */
class PcapWriter {
public:
    /**
     * @brief Constructor
     */
    PcapWriter();
    
    /**
     * @brief Destructor - closes file if open
     */
    ~PcapWriter();
    
    // Prevent copying
    PcapWriter(const PcapWriter&) = delete;
    PcapWriter& operator=(const PcapWriter&) = delete;
    
    /**
     * @brief Open a new PCAP file
     * @param filename Full path to the file
     * @param linkType Link layer type (default: 802.11)
     * @param snaplen Maximum capture length
     * @return true if file opened successfully
     */
    bool open(const char* filename, 
              PcapLinkType linkType = PcapLinkType::IEEE802_11,
              uint32_t snaplen = 65535);
    
    /**
     * @brief Close the current file
     */
    void close();
    
    /**
     * @brief Write a packet to the file
     * @param data Raw packet data
     * @param length Packet length
     * @param timestampUs Timestamp in microseconds (0 = use current time)
     * @return true if written successfully
     */
    bool writePacket(const uint8_t* data, uint16_t length, uint32_t timestampUs = 0);
    
    /**
     * @brief Write a CapturedPacket to the file
     * @param packet The captured packet
     * @return true if written successfully
     */
    bool writePacket(const CapturedPacket& packet);
    
    /**
     * @brief Write a packet with radiotap header (for WPA-SEC compatibility)
     * @param data Raw 802.11 frame data (without radiotap)
     * @param length Frame length
     * @param timestampUs Timestamp in microseconds (0 = use current time)
     * @return true if written successfully
     */
    bool writePacketWithRadiotap(const uint8_t* data, uint16_t length, uint32_t timestampUs = 0);
    
    /**
     * @brief Set channel for radiotap header
     * @param channel WiFi channel (1-14)
     */
    void setChannel(uint8_t channel) { m_channel = channel; }
    
    /**
     * @brief Get current channel
     */
    uint8_t getChannel() const { return m_channel; }
    
    /**
     * @brief Flush any buffered data to disk
     */
    void flush();
    
    /**
     * @brief Get current state
     */
    PcapWriterState getState() const { return m_state; }
    
    /**
     * @brief Check if file is open
     */
    bool isOpen() const { return m_state == PcapWriterState::OPEN; }
    
    /**
     * @brief Check if any data was written (for showing stats even on error)
     */
    bool hasWrittenData() const { return m_bytesWritten > 0; }
    
    /**
     * @brief Check if writer is in error state
     */
    bool hasError() const { return m_state == PcapWriterState::ERROR; }
    
    /**
     * @brief Get number of packets written
     */
    uint32_t getPacketCount() const { return m_packetCount; }
    
    /**
     * @brief Get bytes written
     */
    uint32_t getBytesWritten() const { return m_bytesWritten; }
    
    /**
     * @brief Get current filename
     */
    const std::string& getFilename() const { return m_filename; }
    
    /**
     * @brief Generate a filename with timestamp
     * @param prefix File prefix (e.g., "capture")
     * @param directory Directory path (e.g., "/adversary/captures/")
     * @return Full path like "/adversary/captures/capture_20260104_143022.pcap"
     */
    static std::string generateFilename(const char* prefix, const char* directory);
    
    /**
     * @brief Generate a handshake capture filename
     * @param ssid Network SSID (full SSID, will be sanitized)
     * @param bssid Network BSSID (unused, kept for API compatibility)
     * @param directory Directory path
     * @return Full path like "/adversary/captures/handshakes/MyNetwork_123456.pcap"
     */
    static std::string generateHandshakeFilename(const char* ssid, 
                                                  const uint8_t* bssid,
                                                  const char* directory);

private:
    /**
     * @brief Write the PCAP global header
     */
    bool writeGlobalHeader(PcapLinkType linkType, uint32_t snaplen);
    
    fs::File m_file;            ///< File handle (Arduino FS)
    PcapWriterState m_state;
    PcapLinkType m_linkType;    ///< Current link type
    std::string m_filename;
    uint32_t m_packetCount;
    uint32_t m_bytesWritten;
    uint32_t m_startTime;       ///< Capture start time (millis)
    uint8_t m_channel;          ///< WiFi channel for radiotap header
};

} // namespace adversary

#endif // ADVERSARY_PCAP_WRITER_H
