// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Seeding surface: the u64 value seed, the std::seed_seq path, and the
// overload resolution that keeps the two apart.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>
#include <type_traits>
#include <vector>

#include "vphilox/vphilox.hpp"

namespace {

using vphilox::engine;
using vphilox::u32;
using vphilox::u64;

/// First `n` outputs of a freshly seeded engine.
template <class Seed>
std::vector<u32> stream_of(Seed&& seed, std::size_t n = 64) {
    engine e(std::forward<Seed>(seed));
    std::vector<u32> out(n);
    for (auto& w : out) w = e();
    return out;
}

}  // namespace

TEST(Seeding, SeedSeqConstructorIsAvailable) {
    static_assert(std::is_constructible_v<engine, std::seed_seq&>);
    // Only as an lvalue, matching every standard engine.
    static_assert(!std::is_constructible_v<engine, std::seed_seq&&>);
}

TEST(Seeding, SeedSeqDrawsExactlyTheKey) {
    std::seed_seq q{1, 2, 3, 4};

    std::array<u32, vphilox::key_words> expected{};
    q.generate(expected.begin(), expected.end());

    engine e(q);
    EXPECT_EQ(e.key().v[0], expected[0]);
    EXPECT_EQ(e.key().v[1], expected[1]);
}

TEST(Seeding, SeedSeqLeavesTheCounterAtZero) {
    std::seed_seq q{9, 8, 7};
    engine e(q);
    EXPECT_EQ(e.counter(), vphilox::counter4{});
}

TEST(Seeding, EquivalentSeedSeqsGiveTheSameStream) {
    std::seed_seq a{42, 43, 44};
    std::seed_seq b{42, 43, 44};
    EXPECT_EQ(stream_of(a), stream_of(b));
}

TEST(Seeding, DifferentSeedSeqsGiveDifferentStreams) {
    std::seed_seq a{1};
    std::seed_seq b{2};
    EXPECT_NE(stream_of(a), stream_of(b));
}

TEST(Seeding, ConstructorAndSeedMemberAgree) {
    std::seed_seq a{5, 6, 7};
    std::seed_seq b{5, 6, 7};

    engine constructed(a);

    engine reseeded(u64{0xDEADBEEFCAFEF00Dull});
    reseeded.discard(1000);  // somewhere well into another stream
    reseeded.seed(b);

    EXPECT_EQ(constructed.key(), reseeded.key());
    for (int i = 0; i < 64; ++i) EXPECT_EQ(constructed(), reseeded()) << "word " << i;
}

// The regression the constraint exists for. Unconstrained, the template
// constructor accepts any non-const lvalue, so the engine advertises
// constructibility from types it cannot seed from and the mismatch only
// surfaces inside the constructor body. Verified by hand: dropping the
// requires-clause flips the first assertion below to true.
TEST(Seeding, NonSeedSequenceTypesAreNotConstructibleFrom) {
    struct Widget {
        int x;
    };
    static_assert(!std::is_constructible_v<engine, Widget&>);
    static_assert(!std::is_constructible_v<engine, std::vector<u32>&>);
    static_assert(std::is_constructible_v<engine, std::seed_seq&>);
}

// Seeding from an integer lvalue takes the value overload whatever its width.
// This holds with or without the constraint above -- it is a guarantee about
// the seeding API, not a probe of the constraint.
TEST(Seeding, IntegerLvaluesOfAnyWidthSeedByValue) {
    int as_int           = 7;
    unsigned as_unsigned = 7u;
    u32 as_u32           = 7u;
    u64 as_u64           = 7ull;

    for (const auto& e : {engine(as_int), engine(as_unsigned), engine(as_u32), engine(as_u64)}) {
        EXPECT_EQ(e.key().v[0], 7u);
        EXPECT_EQ(e.key().v[1], 0u);
    }
}

TEST(Seeding, U64SplitsAcrossBothKeyWords) {
    engine e(u64{0x0123456789ABCDEFull});
    EXPECT_EQ(e.key().v[0], 0x89ABCDEFu);
    EXPECT_EQ(e.key().v[1], 0x01234567u);
}

TEST(Seeding, SeedU64MatchesTheConstructor) {
    engine constructed(u64{7});

    engine reseeded(u64{99});
    reseeded.discard(4096);
    reseeded.seed(u64{7});

    EXPECT_EQ(constructed.key(), reseeded.key());
    EXPECT_EQ(constructed.counter(), reseeded.counter());
    for (int i = 0; i < 64; ++i) EXPECT_EQ(constructed(), reseeded()) << "word " << i;
}

TEST(Seeding, ReseedingRewindsAnAdvancedEngine) {
    engine e(u64{123});
    const std::vector<u32> first = stream_of(u64{123});

    e.discard(10'000);
    e.seed(u64{123});

    for (std::size_t i = 0; i < first.size(); ++i) EXPECT_EQ(e(), first[i]) << "word " << i;
}

// The point of the exercise: an mt19937 call site should port by changing
// the type and nothing else.
TEST(Seeding, PortsAnMt19937CallSite) {
    const std::vector<std::uint32_t> entropy{0xC0FFEEu, 0xBADF00Du, 0x5EEDu};
    std::seed_seq q(entropy.begin(), entropy.end());

    engine e(q);
    std::uniform_int_distribution<int> dist(0, 99);

    for (int i = 0; i < 1000; ++i) {
        const int v = dist(e);
        ASSERT_GE(v, 0);
        ASSERT_LE(v, 99);
    }
}
