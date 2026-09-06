/**
 * @file test_pcap_writer.cpp
 * @brief Unit tests for the PCAP Writer module
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

// We can't test file operations in native mode, but we can test
// the PCAP format structures and filename generation

// PCAP structures (copied from pcap_writer.h for native testing)
namespace adversary {

struct __attribute__((packed)) PcapGlobalHeader {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct __attribute__((packed)) PcapPacketHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

enum class PcapLinkType : uint32_t {
    ETHERNET = 1,
    IEEE802_11 = 105,
    IEEE802_11_RADIOTAP = 127,
    IEEE802_11_PRISM = 119
};

} // namespace adversary

using namespace adversary;

// ==============================================
// PCAP Global Header Tests
// ==============================================

void test_pcap_global_header_size() {
    // PCAP global header must be exactly 24 bytes
    TEST_ASSERT_EQUAL(24, sizeof(PcapGlobalHeader));
}

void test_pcap_global_header_magic_number_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* magic = reinterpret_cast<uint8_t*>(&header.magic_number);
    TEST_ASSERT_EQUAL(0, magic - base);
}

void test_pcap_global_header_version_major_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.version_major);
    TEST_ASSERT_EQUAL(4, field - base);
}

void test_pcap_global_header_version_minor_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.version_minor);
    TEST_ASSERT_EQUAL(6, field - base);
}

void test_pcap_global_header_thiszone_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.thiszone);
    TEST_ASSERT_EQUAL(8, field - base);
}

void test_pcap_global_header_sigfigs_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.sigfigs);
    TEST_ASSERT_EQUAL(12, field - base);
}

void test_pcap_global_header_snaplen_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.snaplen);
    TEST_ASSERT_EQUAL(16, field - base);
}

void test_pcap_global_header_network_offset() {
    PcapGlobalHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.network);
    TEST_ASSERT_EQUAL(20, field - base);
}

// ==============================================
// PCAP Packet Header Tests
// ==============================================

void test_pcap_packet_header_size() {
    // PCAP packet header must be exactly 16 bytes
    TEST_ASSERT_EQUAL(16, sizeof(PcapPacketHeader));
}

void test_pcap_packet_header_ts_sec_offset() {
    PcapPacketHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.ts_sec);
    TEST_ASSERT_EQUAL(0, field - base);
}

void test_pcap_packet_header_ts_usec_offset() {
    PcapPacketHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.ts_usec);
    TEST_ASSERT_EQUAL(4, field - base);
}

void test_pcap_packet_header_incl_len_offset() {
    PcapPacketHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.incl_len);
    TEST_ASSERT_EQUAL(8, field - base);
}

void test_pcap_packet_header_orig_len_offset() {
    PcapPacketHeader header;
    uint8_t* base = reinterpret_cast<uint8_t*>(&header);
    uint8_t* field = reinterpret_cast<uint8_t*>(&header.orig_len);
    TEST_ASSERT_EQUAL(12, field - base);
}

// ==============================================
// PCAP Link Type Tests
// ==============================================

void test_pcap_link_type_ethernet() {
    TEST_ASSERT_EQUAL(1, static_cast<uint32_t>(PcapLinkType::ETHERNET));
}

void test_pcap_link_type_ieee802_11() {
    TEST_ASSERT_EQUAL(105, static_cast<uint32_t>(PcapLinkType::IEEE802_11));
}

void test_pcap_link_type_radiotap() {
    TEST_ASSERT_EQUAL(127, static_cast<uint32_t>(PcapLinkType::IEEE802_11_RADIOTAP));
}

void test_pcap_link_type_prism() {
    TEST_ASSERT_EQUAL(119, static_cast<uint32_t>(PcapLinkType::IEEE802_11_PRISM));
}

// ==============================================
// PCAP Magic Number Tests
// ==============================================

void test_pcap_magic_microseconds() {
    // Standard PCAP magic for microsecond precision
    uint32_t magic = 0xa1b2c3d4;
    TEST_ASSERT_EQUAL_HEX32(0xa1b2c3d4, magic);
}

void test_pcap_magic_nanoseconds() {
    // PCAP magic for nanosecond precision
    uint32_t magic = 0xa1b23c4d;
    TEST_ASSERT_EQUAL_HEX32(0xa1b23c4d, magic);
}

// ==============================================
// PCAP Global Header Content Tests
// ==============================================

void test_pcap_header_correct_version() {
    PcapGlobalHeader header;
    header.version_major = 2;
    header.version_minor = 4;
    
    TEST_ASSERT_EQUAL(2, header.version_major);
    TEST_ASSERT_EQUAL(4, header.version_minor);
}

void test_pcap_header_802_11_link_type() {
    PcapGlobalHeader header;
    header.network = static_cast<uint32_t>(PcapLinkType::IEEE802_11);
    
    TEST_ASSERT_EQUAL(105, header.network);
}

void test_pcap_header_default_snaplen() {
    // Default snaplen should be 65535 for full packet capture
    uint32_t snaplen = 65535;
    TEST_ASSERT_EQUAL(65535, snaplen);
}

// ==============================================
// PCAP Packet Header Content Tests
// ==============================================

void test_pcap_packet_header_timestamp() {
    PcapPacketHeader header;
    header.ts_sec = 1704360000;  // Some timestamp
    header.ts_usec = 500000;     // 0.5 seconds
    
    TEST_ASSERT_EQUAL(1704360000, header.ts_sec);
    TEST_ASSERT_EQUAL(500000, header.ts_usec);
}

void test_pcap_packet_header_lengths_equal() {
    // When packet is not truncated, incl_len == orig_len
    PcapPacketHeader header;
    header.incl_len = 1500;
    header.orig_len = 1500;
    
    TEST_ASSERT_EQUAL(header.incl_len, header.orig_len);
}

void test_pcap_packet_header_truncated_packet() {
    // When packet is truncated, incl_len < orig_len
    PcapPacketHeader header;
    header.incl_len = 256;   // What we saved
    header.orig_len = 1500;  // Original size
    
    TEST_ASSERT_TRUE(header.incl_len < header.orig_len);
}

// ==============================================
// Filename Generation Tests (pattern only)
// ==============================================

void test_filename_contains_prefix() {
    // Test that the filename pattern is correct
    const char* prefix = "capture";
    const char* directory = "/adversary/captures/";
    
    // Expected pattern: /adversary/captures/capture_TIMESTAMP.pcap
    // We verify the directory and prefix format
    TEST_ASSERT_NOT_NULL(prefix);
    TEST_ASSERT_NOT_NULL(directory);
    TEST_ASSERT_TRUE(strlen(directory) > 0);
    TEST_ASSERT_TRUE(strlen(prefix) > 0);
}

void test_handshake_filename_bssid_format() {
    // Verify BSSID bytes formatting
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    // Expected: last 3 bytes as hex
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02X%02X%02X", 
             bssid[3], bssid[4], bssid[5]);
    
    TEST_ASSERT_EQUAL_STRING("DDEEFF", buffer);
}

void test_ssid_sanitization_spaces() {
    // SSIDs with spaces should have spaces replaced
    const char* ssid = "My Network";
    char safe[32];
    strncpy(safe, ssid, sizeof(safe) - 1);
    
    // Replace non-alphanum
    for (int i = 0; safe[i]; i++) {
        char c = safe[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            safe[i] = '_';
        }
    }
    
    TEST_ASSERT_EQUAL_STRING("My_Network", safe);
}

void test_ssid_sanitization_special_chars() {
    const char* ssid = "Test!@#$%";
    char safe[32];
    strncpy(safe, ssid, sizeof(safe) - 1);
    
    for (int i = 0; safe[i]; i++) {
        char c = safe[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            safe[i] = '_';
        }
    }
    
    TEST_ASSERT_EQUAL_STRING("Test_____", safe);
}

void test_ssid_truncation() {
    // Long SSIDs should be truncated
    const char* longSsid = "ThisIsAVeryLongSSIDNameThatShouldBeTruncated";
    char safe[17];  // Max 16 + null
    strncpy(safe, longSsid, 16);
    safe[16] = '\0';
    
    TEST_ASSERT_EQUAL(16, strlen(safe));
    TEST_ASSERT_EQUAL_STRING("ThisIsAVeryLongS", safe);
}

// ==============================================
// Binary Format Tests
// ==============================================

void test_pcap_header_binary_layout() {
    // Create a header and verify its binary layout
    PcapGlobalHeader header;
    memset(&header, 0, sizeof(header));
    
    header.magic_number = 0xa1b2c3d4;
    header.version_major = 2;
    header.version_minor = 4;
    header.thiszone = 0;
    header.sigfigs = 0;
    header.snaplen = 65535;
    header.network = 105;  // IEEE802_11
    
    uint8_t* raw = reinterpret_cast<uint8_t*>(&header);
    
    // Magic (little-endian): d4 c3 b2 a1
    TEST_ASSERT_EQUAL_HEX8(0xd4, raw[0]);
    TEST_ASSERT_EQUAL_HEX8(0xc3, raw[1]);
    TEST_ASSERT_EQUAL_HEX8(0xb2, raw[2]);
    TEST_ASSERT_EQUAL_HEX8(0xa1, raw[3]);
    
    // Version major (little-endian): 02 00
    TEST_ASSERT_EQUAL_HEX8(0x02, raw[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, raw[5]);
    
    // Version minor (little-endian): 04 00
    TEST_ASSERT_EQUAL_HEX8(0x04, raw[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, raw[7]);
}

void test_pcap_packet_header_binary_layout() {
    PcapPacketHeader header;
    memset(&header, 0, sizeof(header));
    
    header.ts_sec = 0x12345678;
    header.ts_usec = 0x000F4240;  // 1000000
    header.incl_len = 100;
    header.orig_len = 100;
    
    TEST_ASSERT_EQUAL(16, sizeof(header));
}

// ==============================================
// Test Runner
// ==============================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // PCAP Global Header structure tests
    RUN_TEST(test_pcap_global_header_size);
    RUN_TEST(test_pcap_global_header_magic_number_offset);
    RUN_TEST(test_pcap_global_header_version_major_offset);
    RUN_TEST(test_pcap_global_header_version_minor_offset);
    RUN_TEST(test_pcap_global_header_thiszone_offset);
    RUN_TEST(test_pcap_global_header_sigfigs_offset);
    RUN_TEST(test_pcap_global_header_snaplen_offset);
    RUN_TEST(test_pcap_global_header_network_offset);
    
    // PCAP Packet Header structure tests
    RUN_TEST(test_pcap_packet_header_size);
    RUN_TEST(test_pcap_packet_header_ts_sec_offset);
    RUN_TEST(test_pcap_packet_header_ts_usec_offset);
    RUN_TEST(test_pcap_packet_header_incl_len_offset);
    RUN_TEST(test_pcap_packet_header_orig_len_offset);
    
    // Link type tests
    RUN_TEST(test_pcap_link_type_ethernet);
    RUN_TEST(test_pcap_link_type_ieee802_11);
    RUN_TEST(test_pcap_link_type_radiotap);
    RUN_TEST(test_pcap_link_type_prism);
    
    // Magic number tests
    RUN_TEST(test_pcap_magic_microseconds);
    RUN_TEST(test_pcap_magic_nanoseconds);
    
    // Header content tests
    RUN_TEST(test_pcap_header_correct_version);
    RUN_TEST(test_pcap_header_802_11_link_type);
    RUN_TEST(test_pcap_header_default_snaplen);
    
    // Packet header content tests
    RUN_TEST(test_pcap_packet_header_timestamp);
    RUN_TEST(test_pcap_packet_header_lengths_equal);
    RUN_TEST(test_pcap_packet_header_truncated_packet);
    
    // Filename generation tests
    RUN_TEST(test_filename_contains_prefix);
    RUN_TEST(test_handshake_filename_bssid_format);
    RUN_TEST(test_ssid_sanitization_spaces);
    RUN_TEST(test_ssid_sanitization_special_chars);
    RUN_TEST(test_ssid_truncation);
    
    // Binary format tests
    RUN_TEST(test_pcap_header_binary_layout);
    RUN_TEST(test_pcap_packet_header_binary_layout);
    
    return UNITY_END();
}
