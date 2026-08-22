// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_avx512.hpp
//
// STATUS: Phase 2, not implemented. Falls through to the scalar kernel.
//
// Plan: the AVX2 kernel widened to __m512i -- 8 counters per register, 8 wide
// multiplies per _mm512_mul_epu32. Beyond the width change:
//   - _mm512_permutexvar_epi32 replaces the shuffle/blend pairs, so the word
//     permutation costs one instruction instead of several.
//   - Watch the downclocking: on Skylake-SP/Ice Lake, sustained 512-bit
//     integer work drops the core frequency. Phase 4 must benchmark AVX-512
//     against AVX2 on the same part before dispatch prefers it by default.
//   - Requires avx512f + avx512dq (dq for the 64-bit integer ops).
//
// Exit criterion: bit-for-bit equality with kernel_scalar.

#ifndef VPHILOX_DETAIL_KERNEL_AVX512_HPP
#define VPHILOX_DETAIL_KERNEL_AVX512_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_AVX512
#include <immintrin.h>
#endif

namespace vphilox::detail {

struct kernel_avx512 {
    static constexpr const char* name             = "avx512";
    static constexpr std::size_t preferred_blocks = 8;
    static constexpr bool implemented             = false;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
        // TODO(phase-2): replace with the interleaved AVX-512 kernel.
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_AVX512_HPP
