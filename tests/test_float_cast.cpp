// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 3: IEEE-754 mantissa injection.
//
// The conversion trades entropy for latency -- 24 of 32 bits survive for
// float, 53 of 64 for double. These tests pin down what it must still
// guarantee: the half-open range, and uniformity across the representable
// grid.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include "vphilox/float_cast.hpp"
#include "vphilox/philox.hpp"

using vphilox::to_double01;
using vphilox::to_float01;

TEST(FloatCast, EndpointsAreCorrect) {
    EXPECT_EQ(to_float01(0u), 0.0f);
    // All ones maps to the largest representable value below 1.0.
    EXPECT_LT(to_float01(0xFFFFFFFFu), 1.0f);
    EXPECT_FLOAT_EQ(to_float01(0xFFFFFFFFu), 1.0f - std::ldexp(1.0f, -23));

    EXPECT_EQ(to_double01(0ull), 0.0);
    EXPECT_LT(to_double01(0xFFFFFFFFFFFFFFFFull), 1.0);
}

TEST(FloatCast, StaysInHalfOpenUnitInterval) {
    vphilox::engine g{0xC0FFEEull};
    for (int i = 0; i < 200000; ++i) {
        const float f = to_float01(g());
        ASSERT_GE(f, 0.0f);
        ASSERT_LT(f, 1.0f);
    }
}

TEST(FloatCast, DoubleStaysInHalfOpenUnitInterval) {
    vphilox::engine g{0xC0FFEEull};
    for (int i = 0; i < 100000; ++i) {
        const double d = g.next_double();
        ASSERT_GE(d, 0.0);
        ASSERT_LT(d, 1.0);
    }
}

TEST(FloatCast, IsMonotonicInTheTop23Bits) {
    // Injection must preserve ordering; a botched shift or mask would not.
    for (vphilox::u32 hi = 0; hi < (1u << 12); ++hi) {
        const vphilox::u32 a = hi << 9;
        const vphilox::u32 b = a | 0x1FFu;  // same top 23 bits, junk below
        EXPECT_EQ(to_float01(a), to_float01(b)) << "low 9 bits must be discarded";
        if (hi > 0) {
            EXPECT_LT(to_float01((hi - 1u) << 9), to_float01(a));
        }
    }
}

TEST(FloatCast, IsRoughlyUniform) {
    // Coarse chi-square-free check: 16 buckets, 1.6M draws, each bucket within
    // 2% of expectation. Real statistical validation is PractRand in Phase 4;
    // this only catches a gross bias.
    constexpr int kBuckets = 16;
    constexpr int kDraws   = 1'600'000;
    std::array<int, kBuckets> hist{};

    vphilox::engine g{12345ull};
    for (int i = 0; i < kDraws; ++i) {
        const auto b = static_cast<int>(to_float01(g()) * kBuckets);
        ASSERT_GE(b, 0);
        ASSERT_LT(b, kBuckets);
        ++hist[static_cast<std::size_t>(b)];
    }

    const double expect = static_cast<double>(kDraws) / kBuckets;
    for (int i = 0; i < kBuckets; ++i) {
        EXPECT_NEAR(hist[static_cast<std::size_t>(i)], expect, expect * 0.02) << "bucket " << i;
    }
}

TEST(FloatCast, IsConstexpr) {
    static_assert(to_float01(0u) == 0.0f);
    static_assert(to_double01(0ull) == 0.0);
    SUCCEED();
}
