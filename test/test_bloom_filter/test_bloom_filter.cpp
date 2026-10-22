/**
 * @file test_bloom_filter.cpp
 * @brief Regression for the bounded-dedup Bloom filter (ADR-0103, phase 2).
 *
 * The wardriving OOM fix hinges on two Bloom properties: (1) NO false negatives, a device we
 * added always tests as seen, so we never re-log it and never re-count it; and (2) the false-
 * positive rate stays within the design target at the design load, so we drop only a small,
 * known fraction of genuinely-new devices near capacity. Both are asserted here against the
 * production sizing (5000 keys, ~1% target), in the native build where the pure filter lives.
 */

#include <unity.h>
#include <cstdint>
#include "utils/bloom_filter.h"

using adversary::utils::BloomFilter;

// Production sizing from ADR-0103: n=5000 unique devices, p~=1% -> m~=48000 bits, k=7.
using SeenFilter = BloomFilter<48000, 7>;

// A deterministic 64-bit key stream (no <random> dependency, reproducible across hosts).
static uint64_t nextKey(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void setUp() {}
void tearDown() {}

// Property 1: never a false negative. Every added key must test true.
void test_no_false_negatives() {
    SeenFilter bloom;
    uint64_t state = 1;
    uint64_t added[5000];
    for (int i = 0; i < 5000; ++i) {
        added[i] = nextKey(state);
        bloom.add(added[i]);
    }
    for (int i = 0; i < 5000; ++i) {
        TEST_ASSERT_TRUE(bloom.test(added[i]));
    }
}

// Property 2: false-positive rate within target at the design load. Fill to n=5000, then probe
// a large disjoint key set; the FP fraction must stay near the ~1% design point (allow margin
// for the finite-sample estimate: assert < 2%).
void test_false_positive_rate_within_target() {
    SeenFilter bloom;
    uint64_t addState = 1;
    for (int i = 0; i < 5000; ++i) bloom.add(nextKey(addState));

    // Probe a disjoint stream (different seed, far from the added stream's trajectory).
    uint64_t probeState = 0xABCDEF0123456789ull;
    const int probes = 50000;
    int falsePositives = 0;
    for (int i = 0; i < probes; ++i) {
        if (bloom.test(nextKey(probeState))) ++falsePositives;
    }
    const float fpRate = static_cast<float>(falsePositives) / probes;
    TEST_ASSERT_TRUE_MESSAGE(fpRate < 0.02f, "Bloom FP rate exceeded 2% at design load");
}

// clear() empties the filter: a key added before clear tests false after (absent fresh key).
void test_clear_resets() {
    SeenFilter bloom;
    const uint64_t key = 0xDEADBEEFCAFEBABEull;
    bloom.add(key);
    TEST_ASSERT_TRUE(bloom.test(key));
    bloom.clear();
    TEST_ASSERT_FALSE(bloom.test(key));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bloom.fillRatio());
}

// fillRatio climbs with load and stays below 1 (the Kirsch-Mitzenmacher probes spread bits).
void test_fill_ratio_tracks_load() {
    SeenFilter bloom;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bloom.fillRatio());
    uint64_t state = 42;
    for (int i = 0; i < 2500; ++i) bloom.add(nextKey(state));
    const float half = bloom.fillRatio();
    for (int i = 0; i < 2500; ++i) bloom.add(nextKey(state));
    const float full = bloom.fillRatio();
    TEST_ASSERT_TRUE(half > 0.0f);
    TEST_ASSERT_TRUE(full > half);
    TEST_ASSERT_TRUE(full < 1.0f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_false_negatives);
    RUN_TEST(test_false_positive_rate_within_target);
    RUN_TEST(test_clear_resets);
    RUN_TEST(test_fill_ratio_tracks_load);
    return UNITY_END();
}
