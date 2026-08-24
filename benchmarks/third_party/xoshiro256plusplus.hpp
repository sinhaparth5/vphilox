// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// An instantiable xoshiro256++ and splitmix64, transliterated from the
// vendored upstream sources in xoshiro/.
//
// Upstream holds its state in file-scope statics and exposes next(). The
// throughput matrix wants several generators alive at once and the scaling
// benchmark wants one per thread, so the state moves into a class. Nothing
// else changes: the arithmetic below is line for line upstream, and
// tests/test_third_party_generators.cpp pins it there by running both.
//
// See third_party/README.md for provenance and licensing. The algorithms are
// Blackman and Vigna's, released into the public domain; only this wrapping
// is ours.

#ifndef VPHILOX_BENCH_THIRD_PARTY_XOSHIRO256PLUSPLUS_HPP
#define VPHILOX_BENCH_THIRD_PARTY_XOSHIRO256PLUSPLUS_HPP

#include <cstdint>
#include <limits>

namespace vphilox_bench {

/// splitmix64: upstream's recommended seeder for xoshiro's 256-bit state.
class splitmix64 {
public:
    using result_type = std::uint64_t;

    explicit splitmix64(std::uint64_t seed) noexcept : x_(seed) {}

    result_type operator()() noexcept {
        std::uint64_t z = (x_ += 0x9e3779b97f4a7c15ull);
        z               = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z               = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t x_;
};

/// xoshiro256++ 1.0.
class xoshiro256plusplus {
public:
    using result_type = std::uint64_t;

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept {
        return std::numeric_limits<std::uint64_t>::max();
    }

    /// Seeded as upstream recommends: all 256 bits from splitmix64, which also
    /// rules out the everywhere-zero state the generator cannot leave.
    explicit xoshiro256plusplus(std::uint64_t seed) noexcept {
        splitmix64 sm(seed);
        for (auto& w : s_) w = sm();
    }

    result_type operator()() noexcept {
        const std::uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
        const std::uint64_t t      = s_[1] << 17;

        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];

        s_[2] ^= t;

        s_[3] = rotl(s_[3], 45);

        return result;
    }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    std::uint64_t s_[4]{};
};

}  // namespace vphilox_bench

#endif  // VPHILOX_BENCH_THIRD_PARTY_XOSHIRO256PLUSPLUS_HPP
