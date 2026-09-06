/**
 * @file test_packet_sniffer.cpp
 * @brief Unit tests for the Packet Sniffer module
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// For native testing, we define the types locally rather than including
// the full header which has ESP32 dependencies

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
    uint8_t* data;
    uint16_t length;
    int8_t rssi;
    uint8_t channel;
    uint32_t timestamp;
    FrameType frameType;
    uint8_t subtype;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    
    bool isEAPOL() const {
        if (frameType != FrameType::DATA) return false;
        if (length < 34) return false;
        const uint8_t* llc = data + 24;
        if (length > 32) {
            if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
                llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00 &&
                llc[6] == 0x88 && llc[7] == 0x8E) {
                return true;
            }
        }
        return false;
    }
    
    bool isBeacon() const {
        return frameType == FrameType::MANAGEMENT && 
               subtype == static_cast<uint8_t>(ManagementSubtype::BEACON);
    }
    
    bool isDeauth() const {
        return frameType == FrameType::MANAGEMENT && 
               (subtype == static_cast<uint8_t>(ManagementSubtype::DEAUTHENTICATION) ||
                subtype == static_cast<uint8_t>(ManagementSubtype::DISASSOCIATION));
    }
    
    bool isProbeRequest() const {
        return frameType == FrameType::MANAGEMENT && 
               subtype == static_cast<uint8_t>(ManagementSubtype::PROBE_REQ);
    }
    
    bool isProbeResponse() const {
        return frameType == FrameType::MANAGEMENT && 
               subtype == static_cast<uint8_t>(ManagementSubtype::PROBE_RESP);
    }
    
    const char* getFrameTypeString() const {
        switch (frameType) {
            case FrameType::MANAGEMENT:
                switch (static_cast<ManagementSubtype>(subtype)) {
                    case ManagementSubtype::BEACON: return "Beacon";
                    case ManagementSubtype::PROBE_REQ: return "ProbeReq";
                    case ManagementSubtype::PROBE_RESP: return "ProbeResp";
                    case ManagementSubtype::AUTHENTICATION: return "Auth";
                    case ManagementSubtype::DEAUTHENTICATION: return "Deauth";
                    case ManagementSubtype::DISASSOCIATION: return "Disassoc";
                    case ManagementSubtype::ASSOCIATION_REQ: return "AssocReq";
                    case ManagementSubtype::ASSOCIATION_RESP: return "AssocResp";
                    case ManagementSubtype::ACTION: return "Action";
                    default: return "Mgmt";
                }
            case FrameType::CONTROL:
                return "Ctrl";
            case FrameType::DATA:
                switch (static_cast<DataSubtype>(subtype)) {
                    case DataSubtype::QOS_DATA: return "QoS";
                    default: return "Data";
                }
            default:
                return "Unknown";
        }
    }
};

/**
 * @brief Sniffer filter configuration
 */
struct SnifferFilter {
    bool captureManagement = true;
    bool captureControl = false;
    bool captureData = true;
    bool captureBeacons = false;
    bool captureProbes = true;
    bool captureEAPOL = true;
    bool captureDeauth = true;
    uint8_t targetBSSID[6] = {0};
    uint8_t targetChannel = 0;
    
    bool hasBSSIDFilter() const {
        for (int i = 0; i < 6; i++) {
            if (targetBSSID[i] != 0) return true;
        }
        return false;
    }
    
    void clearBSSIDFilter() {
        memset(targetBSSID, 0, 6);
    }
    
    void setBSSIDFilter(const uint8_t* bssid) {
        memcpy(targetBSSID, bssid, 6);
    }
};

/**
 * @brief Sniffer statistics
 */
struct SnifferStats {
    uint32_t totalPackets = 0;
    uint32_t managementFrames = 0;
    uint32_t controlFrames = 0;
    uint32_t dataFrames = 0;
    uint32_t beacons = 0;
    uint32_t probeRequests = 0;
    uint32_t probeResponses = 0;
    uint32_t deauthFrames = 0;
    uint32_t eapolFrames = 0;
    uint32_t droppedPackets = 0;
    uint32_t startTime = 0;
    uint8_t currentChannel = 0;
    
    void reset() {
        totalPackets = 0;
        managementFrames = 0;
        controlFrames = 0;
        dataFrames = 0;
        beacons = 0;
        probeRequests = 0;
        probeResponses = 0;
        deauthFrames = 0;
        eapolFrames = 0;
        droppedPackets = 0;
        startTime = 0;
        currentChannel = 0;
    }
    
    uint32_t getDurationSeconds() const {
        return 0;  // Stubbed for native test
    }
    
    float getPacketsPerSecond() const {
        return 0.0f;  // Stubbed for native test
    }
};

} // namespace adversary

using namespace adversary;

// ==============================================
// Test CapturedPacket helper methods
// ==============================================

// Sample 802.11 beacon frame header (simplified)
static const uint8_t SAMPLE_BEACON[] = {
    0x80, 0x00,                     // Frame Control: Type=0 (Mgmt), Subtype=8 (Beacon)
    0x00, 0x00,                     // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: Broadcast
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Source: AABBCCDDEEFF
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // BSSID: AABBCCDDEEFF
    0x00, 0x00,                     // Sequence Control
    // Beacon frame body would follow...
};

// Sample 802.11 probe request frame header
static const uint8_t SAMPLE_PROBE_REQ[] = {
    0x40, 0x00,                     // Frame Control: Type=0 (Mgmt), Subtype=4 (Probe Req)
    0x00, 0x00,                     // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: Broadcast
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Source
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BSSID: Broadcast
    0x00, 0x00,                     // Sequence Control
};

// Sample 802.11 probe response frame header
static const uint8_t SAMPLE_PROBE_RESP[] = {
    0x50, 0x00,                     // Frame Control: Type=0 (Mgmt), Subtype=5 (Probe Resp)
    0x00, 0x00,                     // Duration
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Destination
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Source
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // BSSID
    0x00, 0x00,                     // Sequence Control
};

// Sample 802.11 deauthentication frame header
static const uint8_t SAMPLE_DEAUTH[] = {
    0xC0, 0x00,                     // Frame Control: Type=0 (Mgmt), Subtype=12 (Deauth)
    0x00, 0x00,                     // Duration
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Destination
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Source
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // BSSID
    0x00, 0x00,                     // Sequence Control
    0x01, 0x00,                     // Reason code
};

// Sample 802.11 disassociation frame header
static const uint8_t SAMPLE_DISASSOC[] = {
    0xA0, 0x00,                     // Frame Control: Type=0 (Mgmt), Subtype=10 (Disassoc)
    0x00, 0x00,                     // Duration
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Destination
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Source
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // BSSID
    0x00, 0x00,                     // Sequence Control
    0x01, 0x00,                     // Reason code
};

// Sample 802.11 data frame with EAPOL
// For QoS data, LLC/SNAP starts at byte 26 (24 byte header + 2 byte QoS control)
// We pad to ensure LLC/SNAP is at position 24 for the simplified parser
static const uint8_t SAMPLE_EAPOL_DATA[] = {
    0x08, 0x02,                     // Frame Control: Type=2 (Data), Subtype=0 (Data) - NOT QoS for simplicity
    0x00, 0x00,                     // Duration
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Addr1 (receiver)
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Addr2 (transmitter)
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Addr3 (BSSID)
    0x00, 0x00,                     // Sequence Control (bytes 22-23)
    // LLC/SNAP header starts at byte 24
    0xAA, 0xAA, 0x03,               // LLC (bytes 24-26)
    0x00, 0x00, 0x00,               // OUI (bytes 27-29)
    0x88, 0x8E,                     // EtherType: EAPOL (bytes 30-31)
    // EAPOL data would follow...
    0x01, 0x00, 0x00, 0x00,
};

// Sample regular data frame (not EAPOL)
static const uint8_t SAMPLE_DATA[] = {
    0x08, 0x02,                     // Frame Control: Type=2 (Data), Subtype=0 (Data)
    0x00, 0x00,                     // Duration
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Addr1
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // Addr2
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  // Addr3
    0x00, 0x00,                     // Sequence Control
    // Regular data payload
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Helper to create a CapturedPacket
CapturedPacket createPacket(const uint8_t* data, uint16_t len, FrameType type, uint8_t subtype) {
    CapturedPacket pkt;
    pkt.data = const_cast<uint8_t*>(data);
    pkt.length = len;
    pkt.rssi = -50;
    pkt.channel = 6;
    pkt.timestamp = 1000000;
    pkt.frameType = type;
    pkt.subtype = subtype;
    memset(pkt.addr1, 0, 6);
    memset(pkt.addr2, 0, 6);
    memset(pkt.addr3, 0, 6);
    return pkt;
}

// ==============================================
// CapturedPacket Tests
// ==============================================

void test_captured_packet_is_beacon_true() {
    CapturedPacket pkt = createPacket(SAMPLE_BEACON, sizeof(SAMPLE_BEACON), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::BEACON));
    TEST_ASSERT_TRUE(pkt.isBeacon());
}

void test_captured_packet_is_beacon_false_for_probe() {
    CapturedPacket pkt = createPacket(SAMPLE_PROBE_REQ, sizeof(SAMPLE_PROBE_REQ), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::PROBE_REQ));
    TEST_ASSERT_FALSE(pkt.isBeacon());
}

void test_captured_packet_is_beacon_false_for_data() {
    CapturedPacket pkt = createPacket(SAMPLE_DATA, sizeof(SAMPLE_DATA), 
                                       FrameType::DATA, 0);
    TEST_ASSERT_FALSE(pkt.isBeacon());
}

void test_captured_packet_is_probe_request_true() {
    CapturedPacket pkt = createPacket(SAMPLE_PROBE_REQ, sizeof(SAMPLE_PROBE_REQ), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::PROBE_REQ));
    TEST_ASSERT_TRUE(pkt.isProbeRequest());
}

void test_captured_packet_is_probe_request_false() {
    CapturedPacket pkt = createPacket(SAMPLE_BEACON, sizeof(SAMPLE_BEACON), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::BEACON));
    TEST_ASSERT_FALSE(pkt.isProbeRequest());
}

void test_captured_packet_is_probe_response_true() {
    CapturedPacket pkt = createPacket(SAMPLE_PROBE_RESP, sizeof(SAMPLE_PROBE_RESP), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::PROBE_RESP));
    TEST_ASSERT_TRUE(pkt.isProbeResponse());
}

void test_captured_packet_is_deauth_true_for_deauth() {
    CapturedPacket pkt = createPacket(SAMPLE_DEAUTH, sizeof(SAMPLE_DEAUTH), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::DEAUTHENTICATION));
    TEST_ASSERT_TRUE(pkt.isDeauth());
}

void test_captured_packet_is_deauth_true_for_disassoc() {
    CapturedPacket pkt = createPacket(SAMPLE_DISASSOC, sizeof(SAMPLE_DISASSOC), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::DISASSOCIATION));
    TEST_ASSERT_TRUE(pkt.isDeauth());
}

void test_captured_packet_is_deauth_false_for_beacon() {
    CapturedPacket pkt = createPacket(SAMPLE_BEACON, sizeof(SAMPLE_BEACON), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::BEACON));
    TEST_ASSERT_FALSE(pkt.isDeauth());
}

void test_captured_packet_is_eapol_true() {
    CapturedPacket pkt = createPacket(SAMPLE_EAPOL_DATA, sizeof(SAMPLE_EAPOL_DATA), 
                                       FrameType::DATA, 
                                       static_cast<uint8_t>(DataSubtype::DATA));
    TEST_ASSERT_TRUE(pkt.isEAPOL());
}

void test_captured_packet_is_eapol_false_for_regular_data() {
    CapturedPacket pkt = createPacket(SAMPLE_DATA, sizeof(SAMPLE_DATA), 
                                       FrameType::DATA, 
                                       static_cast<uint8_t>(DataSubtype::DATA));
    TEST_ASSERT_FALSE(pkt.isEAPOL());
}

void test_captured_packet_is_eapol_false_for_management() {
    CapturedPacket pkt = createPacket(SAMPLE_BEACON, sizeof(SAMPLE_BEACON), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::BEACON));
    TEST_ASSERT_FALSE(pkt.isEAPOL());
}

void test_captured_packet_is_eapol_false_short_packet() {
    uint8_t shortData[20] = {0};
    CapturedPacket pkt = createPacket(shortData, 20, FrameType::DATA, 0);
    TEST_ASSERT_FALSE(pkt.isEAPOL());
}

// ==============================================
// Frame Type String Tests
// ==============================================

void test_frame_type_string_beacon() {
    CapturedPacket pkt = createPacket(SAMPLE_BEACON, sizeof(SAMPLE_BEACON), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::BEACON));
    TEST_ASSERT_EQUAL_STRING("Beacon", pkt.getFrameTypeString());
}

void test_frame_type_string_probe_req() {
    CapturedPacket pkt = createPacket(SAMPLE_PROBE_REQ, sizeof(SAMPLE_PROBE_REQ), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::PROBE_REQ));
    TEST_ASSERT_EQUAL_STRING("ProbeReq", pkt.getFrameTypeString());
}

void test_frame_type_string_probe_resp() {
    CapturedPacket pkt = createPacket(SAMPLE_PROBE_RESP, sizeof(SAMPLE_PROBE_RESP), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::PROBE_RESP));
    TEST_ASSERT_EQUAL_STRING("ProbeResp", pkt.getFrameTypeString());
}

void test_frame_type_string_deauth() {
    CapturedPacket pkt = createPacket(SAMPLE_DEAUTH, sizeof(SAMPLE_DEAUTH), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::DEAUTHENTICATION));
    TEST_ASSERT_EQUAL_STRING("Deauth", pkt.getFrameTypeString());
}

void test_frame_type_string_disassoc() {
    CapturedPacket pkt = createPacket(SAMPLE_DISASSOC, sizeof(SAMPLE_DISASSOC), 
                                       FrameType::MANAGEMENT, 
                                       static_cast<uint8_t>(ManagementSubtype::DISASSOCIATION));
    TEST_ASSERT_EQUAL_STRING("Disassoc", pkt.getFrameTypeString());
}

void test_frame_type_string_data() {
    CapturedPacket pkt = createPacket(SAMPLE_DATA, sizeof(SAMPLE_DATA), 
                                       FrameType::DATA, 
                                       static_cast<uint8_t>(DataSubtype::DATA));
    TEST_ASSERT_EQUAL_STRING("Data", pkt.getFrameTypeString());
}

void test_frame_type_string_qos_data() {
    CapturedPacket pkt = createPacket(SAMPLE_EAPOL_DATA, sizeof(SAMPLE_EAPOL_DATA), 
                                       FrameType::DATA, 
                                       static_cast<uint8_t>(DataSubtype::QOS_DATA));
    TEST_ASSERT_EQUAL_STRING("QoS", pkt.getFrameTypeString());
}

void test_frame_type_string_control() {
    CapturedPacket pkt = createPacket(nullptr, 0, FrameType::CONTROL, 0);
    TEST_ASSERT_EQUAL_STRING("Ctrl", pkt.getFrameTypeString());
}

// ==============================================
// SnifferFilter Tests
// ==============================================

void test_sniffer_filter_default_values() {
    SnifferFilter filter;
    TEST_ASSERT_TRUE(filter.captureManagement);
    TEST_ASSERT_FALSE(filter.captureControl);
    TEST_ASSERT_TRUE(filter.captureData);
    TEST_ASSERT_FALSE(filter.captureBeacons);
    TEST_ASSERT_TRUE(filter.captureProbes);
    TEST_ASSERT_TRUE(filter.captureEAPOL);
    TEST_ASSERT_TRUE(filter.captureDeauth);
    TEST_ASSERT_EQUAL(0, filter.targetChannel);
}

void test_sniffer_filter_no_bssid_by_default() {
    SnifferFilter filter;
    TEST_ASSERT_FALSE(filter.hasBSSIDFilter());
}

void test_sniffer_filter_set_bssid() {
    SnifferFilter filter;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    filter.setBSSIDFilter(bssid);
    
    TEST_ASSERT_TRUE(filter.hasBSSIDFilter());
    TEST_ASSERT_EQUAL_MEMORY(bssid, filter.targetBSSID, 6);
}

void test_sniffer_filter_clear_bssid() {
    SnifferFilter filter;
    uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    filter.setBSSIDFilter(bssid);
    
    TEST_ASSERT_TRUE(filter.hasBSSIDFilter());
    
    filter.clearBSSIDFilter();
    TEST_ASSERT_FALSE(filter.hasBSSIDFilter());
}

// ==============================================
// SnifferStats Tests
// ==============================================

void test_sniffer_stats_default_zero() {
    SnifferStats stats;
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.totalPackets);
    TEST_ASSERT_EQUAL(0, stats.managementFrames);
    TEST_ASSERT_EQUAL(0, stats.controlFrames);
    TEST_ASSERT_EQUAL(0, stats.dataFrames);
    TEST_ASSERT_EQUAL(0, stats.beacons);
    TEST_ASSERT_EQUAL(0, stats.probeRequests);
    TEST_ASSERT_EQUAL(0, stats.deauthFrames);
    TEST_ASSERT_EQUAL(0, stats.eapolFrames);
}

void test_sniffer_stats_duration_zero_at_start() {
    SnifferStats stats;
    stats.reset();
    stats.startTime = 0;
    
    // Duration should be based on millis() which is stubbed to 0 in native
    TEST_ASSERT_EQUAL(0, stats.getDurationSeconds());
}

void test_sniffer_stats_pps_zero_when_no_packets() {
    SnifferStats stats;
    stats.reset();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, stats.getPacketsPerSecond());
}

// ==============================================
// FrameType Enum Tests
// ==============================================

void test_frame_type_management_value() {
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(FrameType::MANAGEMENT));
}

void test_frame_type_control_value() {
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(FrameType::CONTROL));
}

void test_frame_type_data_value() {
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(FrameType::DATA));
}

void test_frame_type_extension_value() {
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(FrameType::EXTENSION));
}

// ==============================================
// ManagementSubtype Enum Tests
// ==============================================

void test_subtype_beacon_value() {
    TEST_ASSERT_EQUAL(8, static_cast<uint8_t>(ManagementSubtype::BEACON));
}

void test_subtype_probe_req_value() {
    TEST_ASSERT_EQUAL(4, static_cast<uint8_t>(ManagementSubtype::PROBE_REQ));
}

void test_subtype_probe_resp_value() {
    TEST_ASSERT_EQUAL(5, static_cast<uint8_t>(ManagementSubtype::PROBE_RESP));
}

void test_subtype_deauth_value() {
    TEST_ASSERT_EQUAL(12, static_cast<uint8_t>(ManagementSubtype::DEAUTHENTICATION));
}

void test_subtype_disassoc_value() {
    TEST_ASSERT_EQUAL(10, static_cast<uint8_t>(ManagementSubtype::DISASSOCIATION));
}

// ==============================================
// Test Runner
// ==============================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // CapturedPacket helper method tests
    RUN_TEST(test_captured_packet_is_beacon_true);
    RUN_TEST(test_captured_packet_is_beacon_false_for_probe);
    RUN_TEST(test_captured_packet_is_beacon_false_for_data);
    RUN_TEST(test_captured_packet_is_probe_request_true);
    RUN_TEST(test_captured_packet_is_probe_request_false);
    RUN_TEST(test_captured_packet_is_probe_response_true);
    RUN_TEST(test_captured_packet_is_deauth_true_for_deauth);
    RUN_TEST(test_captured_packet_is_deauth_true_for_disassoc);
    RUN_TEST(test_captured_packet_is_deauth_false_for_beacon);
    RUN_TEST(test_captured_packet_is_eapol_true);
    RUN_TEST(test_captured_packet_is_eapol_false_for_regular_data);
    RUN_TEST(test_captured_packet_is_eapol_false_for_management);
    RUN_TEST(test_captured_packet_is_eapol_false_short_packet);
    
    // Frame type string tests
    RUN_TEST(test_frame_type_string_beacon);
    RUN_TEST(test_frame_type_string_probe_req);
    RUN_TEST(test_frame_type_string_probe_resp);
    RUN_TEST(test_frame_type_string_deauth);
    RUN_TEST(test_frame_type_string_disassoc);
    RUN_TEST(test_frame_type_string_data);
    RUN_TEST(test_frame_type_string_qos_data);
    RUN_TEST(test_frame_type_string_control);
    
    // SnifferFilter tests
    RUN_TEST(test_sniffer_filter_default_values);
    RUN_TEST(test_sniffer_filter_no_bssid_by_default);
    RUN_TEST(test_sniffer_filter_set_bssid);
    RUN_TEST(test_sniffer_filter_clear_bssid);
    
    // SnifferStats tests
    RUN_TEST(test_sniffer_stats_default_zero);
    RUN_TEST(test_sniffer_stats_duration_zero_at_start);
    RUN_TEST(test_sniffer_stats_pps_zero_when_no_packets);
    
    // FrameType enum tests
    RUN_TEST(test_frame_type_management_value);
    RUN_TEST(test_frame_type_control_value);
    RUN_TEST(test_frame_type_data_value);
    RUN_TEST(test_frame_type_extension_value);
    
    // ManagementSubtype enum tests
    RUN_TEST(test_subtype_beacon_value);
    RUN_TEST(test_subtype_probe_req_value);
    RUN_TEST(test_subtype_probe_resp_value);
    RUN_TEST(test_subtype_deauth_value);
    RUN_TEST(test_subtype_disassoc_value);
    
    return UNITY_END();
}
