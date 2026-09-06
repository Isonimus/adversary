/**
 * @file pcap_writer.cpp
 * @brief PCAP file writer implementation
 */

#include "pcap_writer.h"
#include "hal/storage/sd_manager.h"
#include "modules/sniffer/packet_sniffer.h"
#include <SD.h>
#include <ctime>
#include <cstring>

namespace adversary {

// Magic numbers
static const uint32_t PCAP_MAGIC_MICROSECONDS = 0xa1b2c3d4;
[[maybe_unused]] static const uint32_t PCAP_MAGIC_NANOSECONDS = 0xa1b23c4d;

// Version
static const uint16_t PCAP_VERSION_MAJOR = 2;
static const uint16_t PCAP_VERSION_MINOR = 4;

PcapWriter::PcapWriter()
    : m_file()
    , m_state(PcapWriterState::CLOSED)
    , m_linkType(PcapLinkType::IEEE802_11)
    , m_packetCount(0)
    , m_bytesWritten(0)
    , m_startTime(0)
    , m_channel(1)
{
}

PcapWriter::~PcapWriter()
{
    close();
}

bool PcapWriter::open(const char* filename, PcapLinkType linkType, uint32_t snaplen)
{
    // Close any existing file
    if (m_file) {
        close();
    }
    
    // Check if SD is available
    if (!SDManager::getInstance().isReady()) {
        Serial.println("[PCAP] SD card not ready");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    // Ensure directory exists
    std::string path(filename);
    size_t lastSlash = path.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string dir = path.substr(0, lastSlash);
        if (!SD.exists(dir.c_str())) {
            if (!SDManager::getInstance().createDirectory(dir.c_str())) {
                Serial.printf("[PCAP] Failed to create directory: %s\n", dir.c_str());
                m_state = PcapWriterState::ERROR;
                return false;
            }
        }
    }
    
    // Open file for writing
    m_file = SD.open(filename, FILE_WRITE);
    if (!m_file) {
        Serial.printf("[PCAP] Failed to open file: %s\n", filename);
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    // Store filename
    m_filename = filename;
    
    // Write global header
    if (!writeGlobalHeader(linkType, snaplen)) {
        close();
        return false;
    }
    
    // Initialize counters
    m_packetCount = 0;
    m_bytesWritten = sizeof(PcapGlobalHeader);
    m_startTime = millis();
    m_linkType = linkType;
    m_state = PcapWriterState::OPEN;
    
    Serial.printf("[PCAP] Opened file: %s (link type %u)\n", filename, static_cast<uint32_t>(linkType));
    return true;
}

void PcapWriter::close()
{
    if (m_file) {
        m_file.close();
    }
    
    if (m_state == PcapWriterState::OPEN) {
        Serial.printf("[PCAP] Closed file: %s (%u packets, %u bytes)\n", 
                      m_filename.c_str(), m_packetCount, m_bytesWritten);
    }
    
    m_state = PcapWriterState::CLOSED;
    m_filename.clear();
}

bool PcapWriter::writeGlobalHeader(PcapLinkType linkType, uint32_t snaplen)
{
    PcapGlobalHeader header;
    header.magic_number = PCAP_MAGIC_MICROSECONDS;
    header.version_major = PCAP_VERSION_MAJOR;
    header.version_minor = PCAP_VERSION_MINOR;
    header.thiszone = 0;      // UTC
    header.sigfigs = 0;       // All tools set this to 0
    header.snaplen = snaplen;
    header.network = static_cast<uint32_t>(linkType);
    
    size_t written = m_file.write(reinterpret_cast<const uint8_t*>(&header), 
                                   sizeof(header));
    if (written != sizeof(header)) {
        Serial.println("[PCAP] Failed to write global header");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    return true;
}

bool PcapWriter::writePacket(const uint8_t* data, uint16_t length, uint32_t timestampUs)
{
    if (m_state != PcapWriterState::OPEN || !m_file) {
        return false;
    }
    
    // Calculate timestamp
    uint32_t now = millis();
    uint32_t elapsed = now - m_startTime;
    
    PcapPacketHeader header;
    
    if (timestampUs > 0) {
        // Use provided timestamp
        header.ts_sec = timestampUs / 1000000;
        header.ts_usec = timestampUs % 1000000;
    } else {
        // Use current time relative to capture start
        header.ts_sec = elapsed / 1000;
        header.ts_usec = (elapsed % 1000) * 1000;
    }
    
    header.incl_len = length;
    header.orig_len = length;
    
    // Write packet header
    size_t written = m_file.write(reinterpret_cast<const uint8_t*>(&header), 
                                   sizeof(header));
    if (written != sizeof(header)) {
        Serial.println("[PCAP] Failed to write packet header");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    // Write packet data
    written = m_file.write(data, length);
    if (written != length) {
        Serial.println("[PCAP] Failed to write packet data");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    m_packetCount++;
    m_bytesWritten += sizeof(header) + length;
    
    return true;
}

bool PcapWriter::writePacket(const CapturedPacket& packet)
{
    return writePacket(packet.data, packet.length, packet.timestamp);
}

bool PcapWriter::writePacketWithRadiotap(const uint8_t* data, uint16_t length, uint32_t timestampUs)
{
    if (m_state != PcapWriterState::OPEN || !m_file) {
        return false;
    }
    
    // Build radiotap header
    RadiotapHeader radiotap;
    radiotap.init(m_channel);
    
    // Calculate timestamp
    uint32_t now = millis();
    uint32_t elapsed = now - m_startTime;
    
    PcapPacketHeader header;
    
    if (timestampUs > 0) {
        header.ts_sec = timestampUs / 1000000;
        header.ts_usec = timestampUs % 1000000;
    } else {
        header.ts_sec = elapsed / 1000;
        header.ts_usec = (elapsed % 1000) * 1000;
    }
    
    // Total length = radiotap header + 802.11 frame
    uint32_t totalLen = sizeof(RadiotapHeader) + length;
    header.incl_len = totalLen;
    header.orig_len = totalLen;
    
    // Write packet header
    size_t written = m_file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    if (written != sizeof(header)) {
        Serial.println("[PCAP] Failed to write packet header");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    // Write radiotap header
    written = m_file.write(reinterpret_cast<const uint8_t*>(&radiotap), sizeof(radiotap));
    if (written != sizeof(radiotap)) {
        Serial.println("[PCAP] Failed to write radiotap header");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    // Write 802.11 frame data
    written = m_file.write(data, length);
    if (written != length) {
        Serial.println("[PCAP] Failed to write packet data");
        m_state = PcapWriterState::ERROR;
        return false;
    }
    
    m_packetCount++;
    m_bytesWritten += sizeof(header) + sizeof(radiotap) + length;
    
    return true;
}

void PcapWriter::flush()
{
    if (m_file && m_state == PcapWriterState::OPEN) {
        m_file.flush();
    }
}

std::string PcapWriter::generateFilename(const char* prefix, const char* directory)
{
    // Get current time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // Ensure directory ends with /
    std::string dir(directory);
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    
    char buffer[128];
    
    if (timeinfo && now > 1000000000) {
        // Valid time available
        snprintf(buffer, sizeof(buffer), "%s%s_%04d%02d%02d_%02d%02d%02d.pcap",
                 dir.c_str(), prefix,
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        // No RTC, use millis
        uint32_t ms = millis();
        snprintf(buffer, sizeof(buffer), "%s%s_%u.pcap",
                 dir.c_str(), prefix, ms);
    }
    
    return std::string(buffer);
}

std::string PcapWriter::generateHandshakeFilename(const char* ssid, 
                                                   const uint8_t* bssid,
                                                   const char* directory)
{
    (void)bssid;  // No longer used in filename
    
    // Create safe SSID (replace problematic chars) - use FULL SSID for WPA-SEC matching
    std::string safeSsid;
    if (ssid && ssid[0] != '\0') {
        safeSsid = ssid;
        // Replace unsafe characters for filesystem
        for (char& c : safeSsid) {
            if (!isalnum(c) && c != '-' && c != '_') {
                c = '_';
            }
        }
        // No length limit - use full SSID for matching
    } else {
        safeSsid = "hidden";
    }
    
    // Ensure directory ends with /
    std::string dir(directory);
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    
    // Simple format: {safeSsid}_{millis}.pcap
    char buffer[192];
    uint32_t ms = millis();
    snprintf(buffer, sizeof(buffer), "%s%s_%lu.pcap",
             dir.c_str(), safeSsid.c_str(), (unsigned long)ms);
    
    return std::string(buffer);
}

} // namespace adversary
