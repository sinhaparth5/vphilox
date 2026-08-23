// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 2 gate: every SIMD kernel must be bit-for-bit identical to the scalar
// reference, for every counter, key, and block count -- including the tails
// that do not fill a vector register.
//
// The parity body below is generic, so implementing a kernel is all it takes
// to bring it into force: flip `implemented` to true on the kernel struct and
// these tests start exercising it instead of skipping.

#include <gtest/gtest.h>

#include <vector>

#include "vphilox/detail/dispatch.hpp"
#include "vphilox/detail/kernel_avx2.hpp"
#include "vphilox/detail/kernel_avx512.hpp"
#include "vphilox/detail/kernel_neon.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

using namespace vphilox;

namespace {

// Block counts chosen to straddle every plausible SIMD width, so tail handling
// is always covered no matter which interleaving Phase 2 settles on.
constexpr std::size_t kBlockCounts[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 64, 100};

const counter4 kCounters[] = {
    counter4{{0, 0, 0, 0}},
    counter4{{1, 0, 0, 0}},
    counter4{{0xFFFFFFFDu, 0, 0, 0}},            // carries mid-batch
    counter4{{0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0}},  // carries two words
    counter4{{0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}},
};

const key2 kKeys[] = {
    key2{{0, 0}},
    key2{{0xFFFFFFFFu, 0xFFFFFFFFu}},
    key2{{0xa4093822u, 0x299f31d0u}},
};

template <typename Kernel>
void expect_matches_scalar() {
    for (const auto& ctr : kCounters) {
        for (const auto& key : kKeys) {
            for (std::size_t blocks : kBlockCounts) {
                SCOPED_TRACE(testing::Message() << Kernel::name << " ctr=" << ctr.v[0]
                                                << " key=" << key.v[0] << " blocks=" << blocks);

                const std::size_t words = blocks * block_words;
                std::vector<u32> expected(words);
                std::vector<u32> actual(words, 0xDEADBEEFu);

                detail::kernel_scalar::generate<default_rounds>(ctr, key, expected.data(), blocks);
                Kernel::template generate<default_rounds>(ctr, key, actual.data(), blocks);

                EXPECT_EQ(expected, actual);
            }
        }
    }
}

/// The kernel contract says output must not depend on how the caller chunks
/// the request. Parity against scalar implies it, but only at the chunk
/// boundaries the matrix above happens to use. This asserts it directly:
/// one long run must equal the same run split at every offset, including the
/// splits that leave a vector iteration half-filled and the ones that hand the
/// second call a destination pointer no vector store can assume is aligned.
template <typename Kernel>
void expect_chunking_independent() {
    constexpr counter4 base{{0xFFFFFFFEu, 0u, 0u, 0u}};  // carries mid-run
    constexpr key2 key{{0xa4093822u, 0x299f31d0u}};
    constexpr std::size_t total = 40;

    std::vector<u32> whole(total * block_words);
    Kernel::template generate<default_rounds>(base, key, whole.data(), total);

    for (std::size_t split = 0; split <= total; ++split) {
        SCOPED_TRACE(testing::Message() << Kernel::name << " split=" << split);

        std::vector<u32> pieced(total * block_words, 0xDEADBEEFu);
        Kernel::template generate<default_rounds>(base, key, pieced.data(), split);
        Kernel::template generate<default_rounds>(
            counter_advanced(base, split), key, pieced.data() + split * block_words, total - split);

        EXPECT_EQ(whole, pieced);
    }
}

}  // namespace

TEST(KernelParity, Avx2) {
    if constexpr (!VPHILOX_HAS_AVX2) {
        GTEST_SKIP() << "AVX2 kernel not compiled in";
    } else if (!detail::kernel_avx2::implemented) {
        GTEST_SKIP() << "AVX2 kernel not implemented yet (Phase 2)";
    } else if (!detail::detect_cpu().avx2) {
        GTEST_SKIP() << "CPU does not support AVX2";
    } else {
        expect_matches_scalar<detail::kernel_avx2>();
        expect_chunking_independent<detail::kernel_avx2>();
    }
}

TEST(KernelParity, Avx512) {
    if constexpr (!VPHILOX_HAS_AVX512) {
        GTEST_SKIP() << "AVX-512 kernel not compiled in";
    } else if (!detail::kernel_avx512::implemented) {
        GTEST_SKIP() << "AVX-512 kernel not implemented yet (Phase 2)";
    } else if (!detail::detect_cpu().avx512) {
        GTEST_SKIP() << "CPU does not support AVX-512";
    } else {
        expect_matches_scalar<detail::kernel_avx512>();
        expect_chunking_independent<detail::kernel_avx512>();
    }
}

TEST(KernelParity, Neon) {
    if constexpr (!VPHILOX_HAS_NEON) {
        GTEST_SKIP() << "NEON kernel not compiled in";
    } else if (!detail::kernel_neon::implemented) {
        GTEST_SKIP() << "NEON kernel not implemented yet (Phase 2)";
    } else {
        expect_matches_scalar<detail::kernel_neon>();
        expect_chunking_independent<detail::kernel_neon>();
    }
}

TEST(KernelParity, ScalarMatchesItself) {
    // Sanity check on the harness itself, so a green suite full of skips is
    // still testing something.
    expect_matches_scalar<detail::kernel_scalar>();
    expect_chunking_independent<detail::kernel_scalar>();
}

TEST(Dispatch, ResolvesToAnAvailableBackend) {
    const backend b                 = active_backend<default_rounds>();
    const detail::cpu_features& cpu = detail::detect_cpu();

    switch (b) {
        case backend::avx512:
            EXPECT_TRUE(cpu.avx512);
            break;
        case backend::avx2:
            EXPECT_TRUE(cpu.avx2);
            break;
        case backend::neon:
            EXPECT_TRUE(cpu.neon);
            break;
        case backend::scalar:
            SUCCEED();
            break;
    }
    EXPECT_STRNE(backend_name(b), "unknown");
}

TEST(Dispatch, NeverSelectsAnUnimplementedKernel) {
    switch (active_backend<default_rounds>()) {
        case backend::avx2:
            EXPECT_TRUE(detail::kernel_avx2::implemented);
            break;
        case backend::avx512:
            EXPECT_TRUE(detail::kernel_avx512::implemented);
            break;
        case backend::neon:
            EXPECT_TRUE(detail::kernel_neon::implemented);
            break;
        case backend::scalar:
            SUCCEED();
            break;
    }
}
