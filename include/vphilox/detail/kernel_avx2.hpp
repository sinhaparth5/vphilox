// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_avx2.hpp
//
// STATUS: Phase 2, not implemented. Falls through to the scalar kernel so the
// tree builds and the dispatch plumbing is testable end to end.
//
// Plan (see docs/VPhilox Development Phases.md, Phase 2):
//   - Hold N counters in SoA form: one __m256i per counter word, lane j
//     carrying counter j's word. Compiling against _mm256_mul_epu32, which
//     multiplies the low 32 bits of each of the four 64-bit lanes, gives 4
//     wide multiplies per instruction.
//   - OPEN DECISION: 4 counters/register (one 64-bit lane each, half the
//     register idle on the non-multiply ops) vs 8 counters/register (full
//     width, but two _mm256_mul_epu32 per multiply plus even/odd shuffles).
//     Benchmark both before committing; `preferred_blocks` below must then
//     match whichever wins.
//   - Round steps map to: _mm256_mul_epu32 (wide multiply),
//     _mm256_srli_epi64 (hi extraction), _mm256_xor_si256 (key XOR),
//     _mm256_shuffle_epi32 / _mm256_blend_epi32 (word permutation),
//     _mm256_add_epi32 (Weyl bump).
//   - Transpose SoA lanes back to AoS on store, or keep the interleaved order
//     and account for it in the ring buffer -- either is fine as long as
//     output matches the scalar kernel block for block.
//
// Exit criterion: bit-for-bit equality with kernel_scalar for every counter,
// key, and block count (tests/test_kernel_parity.cpp).

#ifndef VPHILOX_DETAIL_KERNEL_AVX2_HPP
#define VPHILOX_DETAIL_KERNEL_AVX2_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_AVX2
#include <immintrin.h>
#endif

namespace vphilox::detail {

struct kernel_avx2 {
    static constexpr const char* name = "avx2";

    // TODO(phase-2): set to the interleaving width actually chosen above.
    static constexpr std::size_t preferred_blocks = 4;

    /// True once the intrinsic path exists. Dispatch checks this alongside the
    /// runtime CPU probe, so a stubbed kernel is never selected.
    static constexpr bool implemented = false;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
        // TODO(phase-2): replace with the interleaved AVX2 kernel.
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_AVX2_HPP
