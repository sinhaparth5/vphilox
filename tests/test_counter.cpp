// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1: 128-bit little-endian counter arithmetic. O(1) seeking is only
// correct if this is.

#include <gtest/gtest.h>

#include <random>

#include "vphilox/counter.hpp"

using vphilox::counter4;
using vphilox::counter_add;
using vphilox::counter_advanced;
using vphilox::counter_retreated;
using vphilox::counter_sub;
using vphilox::u32;
using vphilox::u64;

TEST(Counter, AddsWithoutCarry) {
    counter4 c{{1, 2, 3, 4}};
    counter_add(c, 10);
    EXPECT_EQ(c, (counter4{{11, 2, 3, 4}}));
}

TEST(Counter, CarriesAcrossWord0) {
    counter4 c{{0xFFFFFFFFu, 0, 0, 0}};
    counter_add(c, 1);
    EXPECT_EQ(c, (counter4{{0, 1, 0, 0}}));
}

TEST(Counter, CarriesAcrossEveryWord) {
    counter4 c{{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0}};
    counter_add(c, 1);
    EXPECT_EQ(c, (counter4{{0, 0, 0, 1}}));
}

TEST(Counter, WrapsAt2Pow128) {
    counter4 c{{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}};
    counter_add(c, 1);
    EXPECT_EQ(c, counter4{});
}

TEST(Counter, HandlesDeltaWiderThanOneWord) {
    counter4 c{};
    counter_add(c, 0x1'0000'0000ull);
    EXPECT_EQ(c, (counter4{{0, 1, 0, 0}}));
}

TEST(Counter, AddIsAssociative) {
    // Seeking by n must equal stepping n times -- the whole O(1) claim.
    counter4 stepwise{{0xFFFFFF00u, 7, 0, 0}};
    for (int i = 0; i < 1000; ++i) counter_add(stepwise, 1);

    const counter4 jumped = counter_advanced(counter4{{0xFFFFFF00u, 7, 0, 0}}, 1000);
    EXPECT_EQ(stepwise, jumped);
}

TEST(Counter, AddZeroIsIdentity) {
    const counter4 c{{5, 6, 7, 8}};
    EXPECT_EQ(counter_advanced(c, 0), c);
}

TEST(Counter, SubtractUndoesAdd) {
    // The property serialization leans on: recovering a caller-visible
    // position means stepping back exactly as far as the engine ran ahead.
    std::mt19937_64 rng{1};
    for (int i = 0; i < 20000; ++i) {
        const counter4 start{{static_cast<u32>(rng()), static_cast<u32>(rng()),
                              static_cast<u32>(rng()), static_cast<u32>(rng())}};
        const u64 delta = rng();

        counter4 c = start;
        counter_add(c, delta);
        counter_sub(c, delta);
        ASSERT_EQ(c, start) << "delta " << delta;
    }
}

TEST(Counter, SubtractBorrowsAcrossEveryWord) {
    EXPECT_EQ(counter_retreated(counter4{{0, 1, 0, 0}}, 1), (counter4{{0xFFFFFFFFu, 0, 0, 0}}));
    EXPECT_EQ(counter_retreated(counter4{{0, 0, 1, 0}}, 1),
              (counter4{{0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0}}));
    EXPECT_EQ(counter_retreated(counter4{{0, 0, 0, 1}}, 1),
              (counter4{{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0}}));
}

TEST(Counter, SubtractWrapsAtZero) {
    // Mirrors counter_add's silent wrap at 2^128 rather than saturating.
    EXPECT_EQ(counter_retreated(counter4{}, 1),
              (counter4{{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}}));
}

TEST(Counter, SubtractHandlesDeltaWiderThanOneWord) {
    EXPECT_EQ(counter_retreated(counter4{}, 0x1'0000'0000ull),
              (counter4{{0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}}));
}

TEST(Counter, SubtractZeroIsIdentity) {
    const counter4 c{{5, 6, 7, 8}};
    EXPECT_EQ(counter_retreated(c, 0), c);
}
