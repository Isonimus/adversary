/**
 * @file test_wifi_frame.cpp
 * @brief Regression for the pure 802.11 frame filter behind the WiFi-AP tracker (ADR-0130).
 *
 * The WiFi-AP tracker locks to a channel and feeds its proximity core only the RSSI of the
 * target AP's own transmissions. The one piece of device-path logic that can be wrong is the
 * frame test: accept a management frame from the target BSSID, reject everything else. A
 * false accept would feed another AP's signal into the meter (hunting the wrong box); a false
 * reject on a valid beacon would starve the meter. Both are caught here, off-hardware.
 */

#include <cstring>

#include <unity.h>

#include "core/wifi_frame.h"

using adversary::managementFrameFromBssid;

void setUp(void) {}
void tearDown(void) {}

namespace {

const uint8_t kTargetBssid[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
const uint8_t kOtherBssid[6]  = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};

// Build a minimal 802.11 header: frame-control byte 0, then duration + Address 1 (broadcast),
// then Address 2 = the given transmitter. 16 bytes, exactly through Address 2.
void buildHeader(uint8_t out[16], uint8_t frameControl0, const uint8_t addr2[6]) {
    memset(out, 0, 16);
    out[0] = frameControl0;              // frame control: version/type/subtype
    out[1] = 0x00;                       // frame control flags
    out[2] = 0x00; out[3] = 0x00;        // duration
    memset(out + 4, 0xFF, 6);            // Address 1 (destination): broadcast for a beacon
    memcpy(out + 10, addr2, 6);          // Address 2 (transmitter)
}

// Frame-control byte 0 for a beacon: version 0, type 0 (management), subtype 8 -> 0x80.
constexpr uint8_t kBeaconFc0 = 0x80;
// Frame-control byte 0 for a data frame: type 2 -> (2 << 2) = 0x08.
constexpr uint8_t kDataFc0 = 0x08;

} // namespace

// A beacon transmitted by the target BSSID is the AP's own signal: accept it.
void test_beacon_from_target_accepted(void) {
    uint8_t frame[16];
    buildHeader(frame, kBeaconFc0, kTargetBssid);
    TEST_ASSERT_TRUE(managementFrameFromBssid(frame, sizeof(frame), kTargetBssid));
}

// A beacon from a different BSSID must not feed our meter (that is a different box).
void test_beacon_from_other_bssid_rejected(void) {
    uint8_t frame[16];
    buildHeader(frame, kBeaconFc0, kOtherBssid);
    TEST_ASSERT_FALSE(managementFrameFromBssid(frame, sizeof(frame), kTargetBssid));
}

// A data frame (even from the target) is not what we track: the filter is management-only,
// and the predicate enforces it so a mis-set radio filter cannot leak data frames in.
void test_data_frame_from_target_rejected(void) {
    uint8_t frame[16];
    buildHeader(frame, kDataFc0, kTargetBssid);
    TEST_ASSERT_FALSE(managementFrameFromBssid(frame, sizeof(frame), kTargetBssid));
}

// A frame too short to contain Address 2 cannot be matched: reject, never read past the end.
void test_short_frame_rejected(void) {
    uint8_t frame[16];
    buildHeader(frame, kBeaconFc0, kTargetBssid);
    TEST_ASSERT_FALSE(managementFrameFromBssid(frame, 15, kTargetBssid));
}

// Exactly 16 bytes is the boundary: Address 2 is fully present, so a match is valid.
void test_min_length_boundary_accepted(void) {
    uint8_t frame[16];
    buildHeader(frame, kBeaconFc0, kTargetBssid);
    TEST_ASSERT_TRUE(managementFrameFromBssid(frame, 16, kTargetBssid));
}

// A null buffer is rejected, not dereferenced.
void test_null_frame_rejected(void) {
    TEST_ASSERT_FALSE(managementFrameFromBssid(nullptr, 64, kTargetBssid));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_beacon_from_target_accepted);
    RUN_TEST(test_beacon_from_other_bssid_rejected);
    RUN_TEST(test_data_frame_from_target_rejected);
    RUN_TEST(test_short_frame_rejected);
    RUN_TEST(test_min_length_boundary_accepted);
    RUN_TEST(test_null_frame_rejected);
    return UNITY_END();
}
