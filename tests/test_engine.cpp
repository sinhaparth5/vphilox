// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 3: the C++20 engine wrapper -- concept conformance, refill buffer
// correctness, O(1) seeking, and stream independence.

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include "vphilox/vphilox.hpp"

using vphilox::engine;

TEST(Engine, SatisfiesUniformRandomBitGenerator) {
    static_assert(std::uniform_random_bit_generator<engine>);
    static_assert(std::same_as<engine::result_type, vphilox::u32>);
    static_assert(engine::min() == 0);
    static_assert(engine::max() == 0xFFFFFFFFu);
    SUCCEED();
}

TEST(Engine, IsDeterministicForAGivenSeed) {
    engine a{42}, b{42};
    for (int i = 0; i < 10000; ++i) ASSERT_EQ(a(), b());
}

TEST(Engine, DifferentSeedsGiveDifferentStreams) {
    engine a{1}, b{2};
    int same = 0;
    for (int i = 0; i < 1000; ++i) {
        if (a() == b()) ++same;
    }
    EXPECT_LT(same, 5) << "streams for distinct keys should not correlate";
}

TEST(Engine, OutputMatchesTheRawKernel) {
    // The buffering layer must be invisible: the nth output of the engine is
    // word n of the raw Philox stream for that key, no reordering, no gaps.
    const vphilox::key2 k{{7u, 0u}};
    engine g{k, vphilox::counter4{}};

    constexpr std::size_t blocks = 32;
    std::vector<vphilox::u32> expected(blocks * vphilox::block_words);
    vphilox::detail::kernel_scalar::generate<vphilox::default_rounds>(
        vphilox::counter4{}, k, expected.data(), blocks);

    for (std::size_t i = 0; i < expected.size(); ++i) {
        ASSERT_EQ(g(), expected[i]) << "word " << i;
    }
}

TEST(Engine, SpansMultipleRefills) {
    // Cross the buffer boundary many times over; an off-by-one in the cursor
    // shows up as a repeated or skipped word.
    engine g{99};
    std::vector<vphilox::u32> v(vphilox::refill_words * 10);
    std::generate(v.begin(), v.end(), std::ref(g));

    const std::set<vphilox::u32> unique(v.begin(), v.end());
    EXPECT_EQ(unique.size(), v.size()) << "duplicate outputs suggest a refill bug";
}

TEST(Engine, DiscardMatchesRepeatedCalls) {
    // O(1) seeking is the headline property; it must be exact, not close.
    for (vphilox::u64 n : {0ull, 1ull, 7ull, 31ull, 32ull, 33ull, 1000ull, 100000ull}) {
        SCOPED_TRACE(n);
        engine stepped{2024}, jumped{2024};

        for (vphilox::u64 i = 0; i < n; ++i) (void)stepped();
        jumped.discard(n);

        for (int i = 0; i < 8; ++i) ASSERT_EQ(stepped(), jumped()) << "after discard(" << n << ")";
    }
}

TEST(Engine, DiscardIsCheapForHugeJumps) {
    // Not a timing assertion -- just proof that a 2^40 jump returns at all,
    // which a stateful engine could not do.
    engine g{1};
    g.discard(1ull << 40);
    SUCCEED();
}

TEST(Engine, ResetRestartsTheStream) {
    engine g{7};
    const auto first = g();
    for (int i = 0; i < 1000; ++i) (void)g();
    g.reset();
    EXPECT_EQ(g(), first);
}

TEST(Engine, WorksWithStandardDistributions) {
    engine g{5};

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    for (int i = 0; i < 1000; ++i) {
        const double d = uni(g);
        ASSERT_GE(d, 0.0);
        ASSERT_LT(d, 1.0);
    }

    std::normal_distribution<double> norm(0.0, 1.0);
    double sum = 0.0;
    constexpr int kN = 100000;
    for (int i = 0; i < kN; ++i) sum += norm(g);
    EXPECT_NEAR(sum / kN, 0.0, 0.02);
}

TEST(Engine, WorksWithStdShuffle) {
    std::vector<int> v(1000);
    std::iota(v.begin(), v.end(), 0);
    const auto original = v;

    engine g{31337};
    std::shuffle(v.begin(), v.end(), g);

    EXPECT_NE(v, original);
    std::sort(v.begin(), v.end());
    EXPECT_EQ(v, original) << "shuffle must be a permutation";
}

TEST(Engine, ParallelStreamsAreIndependent) {
    // The usage pattern the library exists for: one engine per worker, keyed
    // by worker id, no synchronisation, no overlap.
    constexpr int kWorkers = 8;
    std::vector<std::vector<vphilox::u32>> streams(kWorkers);

    for (int w = 0; w < kWorkers; ++w) {
        engine g{static_cast<vphilox::u64>(w)};
        streams[static_cast<std::size_t>(w)].resize(512);
        std::generate(streams[static_cast<std::size_t>(w)].begin(),
                      streams[static_cast<std::size_t>(w)].end(), std::ref(g));
    }

    for (int a = 0; a < kWorkers; ++a) {
        for (int b = a + 1; b < kWorkers; ++b) {
            EXPECT_NE(streams[static_cast<std::size_t>(a)], streams[static_cast<std::size_t>(b)]);
        }
    }
}

TEST(Engine, ReportsItsBackend) {
    const auto b = engine::which_backend();
    EXPECT_STRNE(vphilox::backend_name(b), "unknown");
    // Informational: makes CI logs say which kernel actually ran.
    std::cout << "[          ] vphilox backend: " << vphilox::backend_name(b)
              << " (v" << vphilox::version_string << ")\n";
}

TEST(Engine, BufferIsCacheAligned) {
    engine g{0};
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&g) % alignof(engine), 0u);
    EXPECT_GE(alignof(engine), vphilox::cacheline_size);
}
