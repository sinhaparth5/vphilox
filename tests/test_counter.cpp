// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1: 128-bit little-endian counter arithmetic. O(1) seeking is only
// correct if this is.

#include <gtest/gtest.h>

#include "vphilox/counter.hpp"

using vphilox::counter4;
using vphilox::counter_add;
using vphilox::counter_advanced;

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
