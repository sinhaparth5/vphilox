// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1: known-answer tests against the Random123 reference vectors.
//
// Source: the `kat_vectors` file shipped with Random123 (Salmon, Moraes, Dror,
// Shaw, "Parallel Random Numbers: As Easy as 1, 2, 3", SC'11). The three
// standard inputs are all-zeros, all-ones, and the digits of pi.
//
// These values are the contract. Every kernel added later -- AVX2, AVX-512,
// NEON -- has to reproduce them, and any change that breaks this test has
// changed the generator, not fixed it.

#include <gtest/gtest.h>

#include <vector>

#include "vphilox/detail/kernel_scalar.hpp"
#include "vphilox/philox.hpp"

using vphilox::counter4;
using vphilox::key2;
using vphilox::detail::philox4x32;

namespace {

struct kat_case {
    const char* name;
    counter4    ctr;
    key2        key;
    counter4    expected;
};

const kat_case kat_philox4x32_10[] = {
    {"zeros",
     counter4{{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
     key2{{0x00000000u, 0x00000000u}},
     counter4{{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u}}},

    {"ones",
     counter4{{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}},
     key2{{0xffffffffu, 0xffffffffu}},
     counter4{{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu}}},

    {"pi",
     counter4{{0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}},
     key2{{0xa4093822u, 0x299f31d0u}},
     counter4{{0xd16cfe09u, 0x94fdccebu, 0x5001e420u, 0x24126ea1u}}},
};

}  // namespace

TEST(ReferenceVectors, Philox4x32_10) {
    for (const auto& c : kat_philox4x32_10) {
        SCOPED_TRACE(c.name);
        EXPECT_EQ(philox4x32<10>(c.ctr, c.key), c.expected);
    }
}

TEST(ReferenceVectors, ScalarKernelMatchesCoreFunction) {
    // The batched kernel entry point must agree with the single-block core.
    const key2 k{{0xa4093822u, 0x299f31d0u}};
    const counter4 base{{7, 0, 0, 0}};

    constexpr std::size_t blocks = 16;
    std::vector<vphilox::u32> out(blocks * vphilox::block_words);
    vphilox::detail::kernel_scalar::generate<10>(base, k, out.data(), blocks);

    for (std::size_t i = 0; i < blocks; ++i) {
        SCOPED_TRACE(i);
        const counter4 expected = philox4x32<10>(vphilox::counter_advanced(base, i), k);
        for (std::size_t w = 0; w < vphilox::block_words; ++w) {
            EXPECT_EQ(out[i * vphilox::block_words + w], expected.v[w]);
        }
    }
}

TEST(ReferenceVectors, RoundCountChangesOutput) {
    // Cheap guard against a loop that silently ignores its round parameter.
    const counter4 c{{1, 2, 3, 4}};
    const key2 k{{5, 6}};
    EXPECT_NE(philox4x32<7>(c, k), philox4x32<10>(c, k));
    EXPECT_NE(philox4x32<10>(c, k), philox4x32<11>(c, k));
}

TEST(ReferenceVectors, IsConstexpr) {
    // The core must be usable at compile time -- it is a pure function of
    // (key, counter) with no state to initialise.
    constexpr counter4 r = philox4x32<10>(counter4{}, key2{});
    static_assert(r.v[0] == 0x6627e8d5u);
    static_assert(r.v[3] == 0x9b00dbd8u);
    SUCCEED();
}
