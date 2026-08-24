// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// The throughput matrix is only worth publishing if the generators it
// compares against are the real algorithms. Nothing here tests vphilox; it
// tests that the baselines are honest.
//
// xoshiro is ours only in its wrapping, so it is checked against the vendored
// upstream C itself -- run side by side, not against a second copy of the same
// transliteration. pcg64 is vendored verbatim, so there is nothing to
// transliterate; it is pinned to fixed outputs instead, which is what would
// catch a truncated download or a silent upstream bump.

#include <gtest/gtest.h>

// These have to be included at global scope, before the namespace blocks
// below. The vendored .c files open with #include <stdint.h>, and f2x.c adds
// <string.h>; if their guards are not already set, those includes land inside
// a namespace and drag all of stdint.h in with them. Order within this block
// does not matter -- being above the namespaces does.
#include <concepts>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "vphilox/vphilox.hpp"

#include "third_party/xoshiro256plusplus.hpp"

// Upstream verbatim, each in its own namespace: both files define next() at
// file scope, and their file-scope state comes along with it.
namespace upstream_splitmix {
#include "xoshiro/splitmix64.c"
}
namespace upstream_xoshiro {
#include "xoshiro/xoshiro256plusplus.c"
}

#include "pcg-cpp/pcg_random.hpp"

using vphilox_bench::splitmix64;
using vphilox_bench::xoshiro256plusplus;

namespace {

constexpr int kDraws = 4096;

}  // namespace

TEST(ThirdParty, SplitmixMatchesUpstreamC) {
    constexpr std::uint64_t seed = 0x0123456789ABCDEFull;

    upstream_splitmix::x = seed;
    splitmix64 ours(seed);

    for (int i = 0; i < kDraws; ++i) {
        EXPECT_EQ(ours(), upstream_splitmix::next()) << "draw " << i;
    }
}

TEST(ThirdParty, XoshiroMatchesUpstreamC) {
    constexpr std::uint64_t seed = 0xDEADBEEFull;

    // Seed upstream's globals exactly the way the wrapper seeds itself: four
    // splitmix64 draws into s[0..3].
    upstream_splitmix::x = seed;
    for (int i = 0; i < 4; ++i) upstream_xoshiro::s[i] = upstream_splitmix::next();

    xoshiro256plusplus ours(seed);

    for (int i = 0; i < kDraws; ++i) {
        EXPECT_EQ(ours(), upstream_xoshiro::next()) << "draw " << i;
    }
}

TEST(ThirdParty, XoshiroAgreesForSeveralSeeds) {
    for (std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{~0ull},
                               std::uint64_t{0x9E3779B97F4A7C15ull}}) {
        upstream_splitmix::x = seed;
        for (int i = 0; i < 4; ++i) upstream_xoshiro::s[i] = upstream_splitmix::next();

        xoshiro256plusplus ours(seed);
        for (int i = 0; i < 256; ++i) {
            ASSERT_EQ(ours(), upstream_xoshiro::next()) << "seed " << seed << " draw " << i;
        }
    }
}

// Generated from the vendored pcg-cpp at the hash recorded in
// third_party/README.md. These exist to fail loudly if that copy ever changes.
TEST(ThirdParty, Pcg64MatchesRecordedOutputs) {
    pcg64 g(42u);
    EXPECT_EQ(g(), 2915081201720324186ull);
    EXPECT_EQ(g(), 13533757442135995717ull);
    EXPECT_EQ(g(), 13172715927431628928ull);
}

// The matrix drives all of these through the same harness, so they have to be
// interchangeable at the type level.
TEST(ThirdParty, AllSatisfyUniformRandomBitGenerator) {
    static_assert(std::uniform_random_bit_generator<xoshiro256plusplus>);
    static_assert(std::uniform_random_bit_generator<pcg64>);
    static_assert(std::uniform_random_bit_generator<vphilox::engine>);
    SUCCEED();
}
