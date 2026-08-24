// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Conformance with C++26's std::philox_engine (issue #91).
//
// [rand.eng.philox] standardises the engine this library implements, and
// [rand.predef] defines std::philox4x32 with the same parameters vphilox uses.
// That makes the relationship worth pinning rather than assuming, because it
// decides what vphilox is *for*: if the streams agree, vphilox is an
// implementation of the standard engine with SIMD kernels, and `engine` can
// become an alias for std::philox4x32 once libraries ship it. If they did not
// agree, a caller wanting portable Philox would be better off waiting for the
// standard, and this library's whole positioning would be wrong.
//
// Two things are checked, at different strengths.
//
// The first is a known-answer test that holds today, on any toolchain:
// [rand.predef] requires the 10000th consecutive invocation of a
// default-constructed std::philox4x32 to produce 1955073260. vphilox seeded
// with the standard's default_seed produces exactly that, which pins the
// seeding correspondence (seed -> key[0], zero elsewhere), the counter start,
// and the order the four words of each block come out in. The reference
// vectors in test_reference_vectors.cpp already pin the core function against
// Salmon et al., so what this adds is the engine layer around it.
//
// The second compiles in only where a standard library has shipped the type,
// and compares whole streams directly. Until then the constant above is the
// evidence, and it is worth re-confirming against the standard text rather
// than against this comment.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "vphilox/vphilox.hpp"

namespace {

/// [rand.eng.philox] philox_engine::default_seed.
constexpr std::uint32_t kStdDefaultSeed = 20111115u;

/// [rand.predef] requires this of the 10000th consecutive invocation of a
/// default-constructed std::philox4x32.
constexpr std::uint32_t kStdPhilox4x32At10000 = 1955073260u;

TEST(StdPhiloxParity, MatchesTheStandardsRequiredValue) {
    vphilox::engine g{kStdDefaultSeed};
    for (int i = 1; i < 10000; ++i) (void)g();
    EXPECT_EQ(g(), kStdPhilox4x32At10000)
        << "vphilox no longer agrees with std::philox4x32 at the one point the "
           "standard pins, so `engine` could not alias it";
}

/// The correspondence is seed -> key word 0, zero elsewhere. If the words were
/// the other way round the value above would not match, so this records the
/// asymmetry rather than leaving it implicit.
TEST(StdPhiloxParity, KeyWordOrderIsNotSymmetric) {
    vphilox::engine swapped{vphilox::key2{{0u, kStdDefaultSeed}}, vphilox::counter4{}};
    for (int i = 1; i < 10000; ++i) (void)swapped();
    EXPECT_NE(swapped(), kStdPhilox4x32At10000);
}

/// Seeding by u64 puts the low half in key word 0, so a seed that fits in 32
/// bits reaches the same state the standard's 32-bit seed would.
TEST(StdPhiloxParity, SeedingAgreesForAnyThirtyTwoBitSeed) {
    for (const std::uint32_t s : {0u, 1u, 42u, kStdDefaultSeed, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
        vphilox::engine seeded{s};
        vphilox::engine explicitly{vphilox::key2{{s, 0u}}, vphilox::counter4{}};
        for (int i = 0; i < 16; ++i) {
            ASSERT_EQ(seeded(), explicitly()) << "seed " << s;
        }
    }
}

#ifdef __cpp_lib_philox_engine
/// Compiles in wherever a standard library has shipped the type. Whole-stream
/// equality, not a single point.
TEST(StdPhiloxParity, StreamMatchesStdPhilox4x32) {
    std::philox4x32 expected;
    vphilox::engine actual{kStdDefaultSeed};
    for (int i = 0; i < 4096; ++i) {
        ASSERT_EQ(static_cast<std::uint32_t>(expected()), actual()) << "output " << i;
    }
}

/// And that O(1) discard lands where the standard's discard lands.
TEST(StdPhiloxParity, DiscardAgreesWithStd) {
    std::philox4x32 expected;
    vphilox::engine actual{kStdDefaultSeed};
    expected.discard(1000003);
    actual.discard(1000003);
    for (int i = 0; i < 64; ++i) {
        ASSERT_EQ(static_cast<std::uint32_t>(expected()), actual()) << "output " << i;
    }
}
#endif  // __cpp_lib_philox_engine

}  // namespace
