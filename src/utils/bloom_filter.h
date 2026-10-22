/**
 * @file bloom_filter.h
 * @brief Fixed-size Bloom filter for bounded-memory set membership (ADR-0103).
 *
 * A Bloom filter answers "have I probably seen this key?" in O(1) time and, crucially here,
 * O(1) *memory* that is fixed at compile time: the bit array is inline, so adding more keys
 * never grows the footprint. That is the whole point for wardriving dedup, where an exact
 * std::unordered_set grows with unique-device count and exhausts the thin BOTH-mode heap
 * (ADR-0103 diagnosis: ~84 B/device slope to OOM).
 *
 * Error mode is false POSITIVE only, never a false negative: test() may return true for a key
 * that was never added, with probability ~ fillRatio()^HashCount, but a key that WAS added
 * always tests true. The caller sizes BitCount/HashCount for its expected key count n and
 * target false-positive rate p via the standard relations:
 *   BitCount  m = ceil(-n * ln(p) / (ln 2)^2)
 *   HashCount k = round((m / n) * ln 2)
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace adversary {
namespace utils {

template <size_t BitCount, size_t HashCount>
class BloomFilter {
    static_assert(BitCount > 0, "BitCount must be positive");
    static_assert(HashCount > 0, "HashCount must be positive");

public:
    /// Reset to empty.
    void clear() {
        bits_.fill(0);
        setBits_ = 0;
    }

    /// Insert a key. Idempotent: re-adding a present key leaves the filter unchanged.
    void add(uint64_t key) {
        uint64_t h1 = mix(key);
        uint64_t h2 = mix(h1) | 1u;  // force odd so the stride cannot stall on one residue
        for (size_t i = 0; i < HashCount; ++i) {
            const size_t bit = static_cast<size_t>((h1 + i * h2) % BitCount);
            uint8_t& byte = bits_[bit >> 3];
            const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7u));
            if (!(byte & mask)) {
                byte |= mask;
                ++setBits_;
            }
        }
    }

    /// True if @p key may have been added; false means it definitely was not.
    bool test(uint64_t key) const {
        uint64_t h1 = mix(key);
        uint64_t h2 = mix(h1) | 1u;
        for (size_t i = 0; i < HashCount; ++i) {
            const size_t bit = static_cast<size_t>((h1 + i * h2) % BitCount);
            if (!(bits_[bit >> 3] & static_cast<uint8_t>(1u << (bit & 7u)))) {
                return false;
            }
        }
        return true;
    }

    /// Fraction of bits set (0..1); a load proxy. False-positive rate ~ fillRatio()^HashCount.
    float fillRatio() const {
        return static_cast<float>(setBits_) / static_cast<float>(BitCount);
    }

    static constexpr size_t bitCount() { return BitCount; }
    static constexpr size_t byteCount() { return (BitCount + 7u) / 8u; }

private:
    /// splitmix64 finaliser: a strong 64-bit avalanche mixer. Two derived hashes feed the
    /// Kirsch-Mitzenmacher double-hashing scheme (h_i = h1 + i*h2), giving HashCount
    /// independent-enough probes from one key without HashCount separate hash functions.
    static uint64_t mix(uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    std::array<uint8_t, (BitCount + 7u) / 8u> bits_{};
    size_t setBits_ = 0;
};

}  // namespace utils
}  // namespace adversary
