/**
 * @file test_network_log_ring.cpp
 * @brief Unit tests for NetworkLogRing (wardriving Network Log view buffer).
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

#ifndef ESP32
#include "../common/arduino_mocks.h"
#endif

#include "ui/screens/network_log_ring.h"

using namespace adversary;
using wardriving::ObservationType;

static NetworkLogRing ring;

void setUp(void) {
    ring.clear();
}

void tearDown(void) {}

// Build a distinct MAC from a seed so each observation is a different radio.
static void mkMac(uint8_t out[6], uint8_t type, uint8_t seed) {
    out[0] = type;
    out[1] = out[2] = out[3] = out[4] = 0;
    out[5] = seed;
}

static size_t countType(ObservationType t) {
    size_t n = 0;
    for (size_t i = 0; i < ring.count(); i++) {
        if (ring.newestFirst(i).type == t) n++;
    }
    return n;
}

static void addWifi(uint8_t seed) {
    uint8_t mac[6];
    mkMac(mac, 0xAA, seed);
    ring.append("wifi", mac, -50, 6, 3, ObservationType::WIFI);
}

static void addBle(uint8_t seed) {
    uint8_t mac[6];
    mkMac(mac, 0xBB, seed);
    ring.append("ble", mac, -70, 0, 0, ObservationType::BLE);
}

/**
 * @brief The reported bug: under a BLE flood the live log dropped every WiFi row. With the
 * per-type reserve, WiFi rows already present survive any number of new BLE advertisers.
 *
 * Add 10 WiFi, then 40 distinct BLE (far past capacity). WiFi must keep all 10 (its reserve),
 * BLE fills the remaining 15, total is capacity. On the old single FIFO ring (reserve 0) the
 * BLE flood evicts WiFi to 0 and this fails.
 */
void test_wifi_survives_ble_flood(void) {
    for (uint8_t i = 0; i < 10; i++) addWifi(i);
    TEST_ASSERT_EQUAL(10, ring.count());

    for (uint8_t i = 0; i < 40; i++) addBle(i);

    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY, ring.count());
    TEST_ASSERT_EQUAL(10, countType(ObservationType::WIFI));
    TEST_ASSERT_EQUAL(15, countType(ObservationType::BLE));
}

/**
 * @brief Symmetric guarantee: a type arriving after the buffer is already full of the other
 * type still claims its reserve. 40 BLE first, then 20 WiFi -> WiFi reaches its reserve (10),
 * BLE holds the rest (15).
 */
void test_late_wifi_claims_reserve(void) {
    for (uint8_t i = 0; i < 40; i++) addBle(i);
    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY, ring.count());
    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY, countType(ObservationType::BLE));

    for (uint8_t i = 0; i < 20; i++) addWifi(i);

    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY, ring.count());
    TEST_ASSERT_EQUAL(NetworkLogRing::WIFI_RESERVE, countType(ObservationType::WIFI));
    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY - NetworkLogRing::WIFI_RESERVE,
                      countType(ObservationType::BLE));
}

/**
 * @brief Spare slots (capacity beyond the two reserves) go to the busier radio. Only 3 WiFi
 * exist, so BLE may use everything else, not just its own reserve.
 */
void test_spare_slots_follow_demand(void) {
    for (uint8_t i = 0; i < 3; i++) addWifi(i);
    for (uint8_t i = 0; i < 40; i++) addBle(i);

    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY, ring.count());
    TEST_ASSERT_EQUAL(3, countType(ObservationType::WIFI));
    TEST_ASSERT_EQUAL(NetworkLogRing::CAPACITY - 3, countType(ObservationType::BLE));
}

/**
 * @brief Dedup is by (type, bssid): the same radio re-observed is not re-added, but a WiFi
 * BSSID and a BLE BD_ADDR that are byte-equal are distinct rows.
 */
void test_dedup_by_type_and_bssid(void) {
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    ring.append("a", mac, -50, 6, 3, ObservationType::WIFI);
    ring.append("a", mac, -40, 6, 3, ObservationType::WIFI);   // same radio again
    TEST_ASSERT_EQUAL(1, ring.count());

    ring.append("b", mac, -60, 0, 0, ObservationType::BLE);    // same MAC, other radio
    TEST_ASSERT_EQUAL(2, ring.count());
    TEST_ASSERT_EQUAL(1, countType(ObservationType::WIFI));
    TEST_ASSERT_EQUAL(1, countType(ObservationType::BLE));
}

/**
 * @brief newestFirst(0) is the most recent append; order is most-recent-first.
 */
void test_newest_first_ordering(void) {
    uint8_t m0[6]; mkMac(m0, 0xAA, 1);
    uint8_t m1[6]; mkMac(m1, 0xAA, 2);
    uint8_t m2[6]; mkMac(m2, 0xAA, 3);
    ring.append("first",  m0, -50, 1, 3, ObservationType::WIFI);
    ring.append("second", m1, -50, 1, 3, ObservationType::WIFI);
    ring.append("third",  m2, -50, 1, 3, ObservationType::WIFI);

    TEST_ASSERT_EQUAL(3, ring.count());
    TEST_ASSERT_EQUAL_STRING("third",  ring.newestFirst(0).ssid);
    TEST_ASSERT_EQUAL_STRING("second", ring.newestFirst(1).ssid);
    TEST_ASSERT_EQUAL_STRING("first",  ring.newestFirst(2).ssid);
}

/**
 * @brief A null name (unnamed BLE advertiser) is stored as an empty string, not dereferenced.
 */
void test_null_name_stored_empty(void) {
    uint8_t mac[6]; mkMac(mac, 0xBB, 7);
    ring.append(nullptr, mac, -70, 0, 0, ObservationType::BLE);
    TEST_ASSERT_EQUAL(1, ring.count());
    TEST_ASSERT_EQUAL_STRING("", ring.newestFirst(0).ssid);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_survives_ble_flood);
    RUN_TEST(test_late_wifi_claims_reserve);
    RUN_TEST(test_spare_slots_follow_demand);
    RUN_TEST(test_dedup_by_type_and_bssid);
    RUN_TEST(test_newest_first_ordering);
    RUN_TEST(test_null_name_stored_empty);
    return UNITY_END();
}
