// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 3: IEEE-754 mantissa injection.
//
// The conversion trades entropy for latency -- 23 of 32 bits survive for
// float, 52 of 64 for double. These tests pin down what it must still
// guarantee: the half-open range, that every surviving bit really does
// survive, and uniformity across the representable grid.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

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

TEST(FloatCast, InjectedBitsSurviveConversionExactly) {
    // Both steps of the conversion are exact, so nothing may be lost. The
    // bit_cast lands in [1, 2); subtracting 1.0f from a value in [1, 2) is
    // exact by Sterbenz's lemma; and the result k * 2^-23 needs only 23
    // significant bits, so adding 1.0f back is exact too. The 23 injected bits
    // must therefore round-trip bit for bit. If they did not, the conversion
    // would be quietly discarding entropy it was handed -- which no range or
    // uniformity check would catch, because a stuck low bit still yields
    // values in [0, 1) that look uniform at coarse resolution.
    //
    // Exhaustive over every representable output, not sampled: there are only
    // 2^23 of them.
    constexpr vphilox::u32 kGrid = 1u << 23;
    std::uint64_t mismatches     = 0;
    vphilox::u32 first_bad       = 0;

    for (vphilox::u32 k = 0; k < kGrid; ++k) {
        const vphilox::u32 u    = k << 9;  // the 23 bits injection will keep
        const float f           = to_float01(u);
        const vphilox::u32 back = std::bit_cast<vphilox::u32>(f + 1.0f) & 0x7FFFFFu;
        if (back != k) {
            if (mismatches == 0) first_bad = k;
            ++mismatches;
        }
    }

    EXPECT_EQ(mismatches, 0u) << "first mismatch at mantissa " << first_bad;
}

TEST(FloatCast, DoubleInjectedBitsSurviveConversionExactly) {
    // Same argument at 52 bits. 2^52 is not exhaustible, so drive it from the
    // engine instead -- which also covers the next_double() word pairing.
    vphilox::engine g{0xABCDEFull};
    std::uint64_t mismatches = 0;

    for (int i = 0; i < 500000; ++i) {
        const std::uint64_t lo = g();
        const std::uint64_t hi = g();
        const std::uint64_t u  = (hi << 32) | lo;
        const double d         = to_double01(u);
        const auto back        = std::bit_cast<std::uint64_t>(d + 1.0) & 0xFFFFFFFFFFFFFull;
        if (back != (u >> 12)) ++mismatches;
    }

    EXPECT_EQ(mismatches, 0u);
}

TEST(FloatCast, IsUniformByChiSquared) {
    // The bucket check above catches only gross bias -- 2% on 16 buckets would
    // miss a skew confined to part of the range. This is the real test: 1024
    // bins over 2^23 draws, compared against the chi-square distribution it
    // should follow.
    //
    // The bound is two-sided on purpose. A statistic far *below* the degrees
    // of freedom means the counts track expectation more closely than chance
    // allows, which is its own defect signature (and what you would see if the
    // low bits were being dropped onto a coarser grid than claimed).
    //
    // Nothing here is flaky: the seed is fixed and the bit stream is frozen, so
    // the counts -- and therefore the statistic -- are identical on every
    // backend, compiler, and platform. A failure means the stream changed.
    constexpr int kBins        = 1024;
    constexpr std::uint64_t kN = 1ull << 23;

    std::vector<std::uint64_t> hist(kBins, 0);
    vphilox::engine g{0x5EEDull};

    std::vector<vphilox::u32> buf(1u << 16);
    std::uint64_t drawn = 0;
    while (drawn < kN) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), kN - drawn));
        g.generate_n(buf.data(), want);
        for (std::size_t i = 0; i < want; ++i) {
            const auto b = static_cast<std::size_t>(to_float01(buf[i]) * kBins);
            ASSERT_LT(b, static_cast<std::size_t>(kBins));
            ++hist[b];
        }
        drawn += want;
    }

    const double expect = static_cast<double>(kN) / kBins;
    double chi2         = 0.0;
    for (int i = 0; i < kBins; ++i) {
        const double d = static_cast<double>(hist[static_cast<std::size_t>(i)]) - expect;
        chi2 += d * d / expect;
    }

    // df = 1023, mean = df, sd = sqrt(2*df) ~= 45.2. Five sigma each way.
    constexpr double kDf = kBins - 1;
    const double sigma   = std::sqrt(2.0 * kDf);
    const double lo      = kDf - 5.0 * sigma;
    const double hi      = kDf + 5.0 * sigma;
    EXPECT_GT(chi2, lo) << "chi2=" << chi2 << " suspiciously uniform";
    EXPECT_LT(chi2, hi) << "chi2=" << chi2 << " biased";
}

TEST(FloatCast, MatchesUniformCdfByKolmogorovSmirnov) {
    // Chi-square is insensitive to how the deviation is arranged across bins;
    // KS is sensitive to a systematic drift in the CDF, which is what a
    // conversion that skews toward one end of the interval would produce.
    constexpr int kBins        = 1 << 20;  // far finer than 1/sqrt(N)
    constexpr std::uint64_t kN = 1ull << 23;

    std::vector<std::uint32_t> hist(kBins, 0);
    vphilox::engine g{0xD1CEull};

    std::vector<vphilox::u32> buf(1u << 16);
    std::uint64_t drawn = 0;
    while (drawn < kN) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), kN - drawn));
        g.generate_n(buf.data(), want);
        for (std::size_t i = 0; i < want; ++i) {
            auto b = static_cast<std::size_t>(to_float01(buf[i]) * kBins);
            if (b >= static_cast<std::size_t>(kBins)) b = kBins - 1;
            ++hist[b];
        }
        drawn += want;
    }

    double d_max      = 0.0;
    std::uint64_t cum = 0;
    for (int i = 0; i < kBins; ++i) {
        cum += hist[static_cast<std::size_t>(i)];
        const double ecdf  = static_cast<double>(cum) / static_cast<double>(kN);
        const double upper = static_cast<double>(i + 1) / kBins;
        const double lower = static_cast<double>(i) / kBins;
        d_max              = std::max(d_max, std::abs(ecdf - upper));
        d_max              = std::max(d_max, std::abs(ecdf - lower));
    }

    // 1.36/sqrt(N) is the 95% critical value; 2.0/sqrt(N) is around 99.9%.
    // Deterministic stream, so this is a fixed pass/fail, not a coin flip.
    const double critical = 2.0 / std::sqrt(static_cast<double>(kN));
    EXPECT_LT(d_max, critical) << "KS D=" << d_max << " critical=" << critical;
}

TEST(FloatCast, IsConstexpr) {
    static_assert(to_float01(0u) == 0.0f);
    static_assert(to_double01(0ull) == 0.0);
    SUCCEED();
}
