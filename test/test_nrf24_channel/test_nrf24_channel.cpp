/**
 * @file test_nrf24_channel.cpp
 * @brief Regression for the pure nRF24 channel->frequency map (slice-0006).
 *
 * nrf24ChannelToMHz() is the one host-testable seam of the NRF24 driver: it maps
 * an RF channel to its centre frequency and must fail loud (no write, no clamp)
 * on an out-of-range channel so a bad channel never resolves to a real band.
 */

#include <unity.h>

#include "hal/expansion/nrf24.h"

using adversary::hal::nrf24ChannelToMHz;
using adversary::hal::NRF24_MAX_CHANNEL;

void setUp(void) {}
void tearDown(void) {}

void test_band_edges_and_midpoint(void) {
    uint16_t mhz = 0;
    TEST_ASSERT_TRUE(nrf24ChannelToMHz(0, &mhz));
    TEST_ASSERT_EQUAL_UINT16(2400, mhz);

    TEST_ASSERT_TRUE(nrf24ChannelToMHz(125, &mhz));
    TEST_ASSERT_EQUAL_UINT16(2525, mhz);

    TEST_ASSERT_TRUE(nrf24ChannelToMHz(42, &mhz));
    TEST_ASSERT_EQUAL_UINT16(2442, mhz);  // Wi-Fi ch 7 territory, a sanity anchor
}

void test_rejects_channel_above_max(void) {
    uint16_t mhz = 0xBEEF;  // sentinel: must be left untouched on failure
    TEST_ASSERT_FALSE(nrf24ChannelToMHz(NRF24_MAX_CHANNEL + 1, &mhz));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, mhz);

    TEST_ASSERT_FALSE(nrf24ChannelToMHz(255, &mhz));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, mhz);
}

void test_rejects_null_out(void) {
    TEST_ASSERT_FALSE(nrf24ChannelToMHz(0, nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_band_edges_and_midpoint);
    RUN_TEST(test_rejects_channel_above_max);
    RUN_TEST(test_rejects_null_out);
    return UNITY_END();
}
