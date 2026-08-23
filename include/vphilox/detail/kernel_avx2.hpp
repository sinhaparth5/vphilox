// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_avx2.hpp
//
// AVX2 Philox4x32-R over eight interleaved counters.
//
// LANE LAYOUT (issue #12, docs/benchmarks/simd-lane-layout.md)
//
// Structure of arrays: four __m256i, one per counter word. Register x_w holds
// word w of eight consecutive blocks, with 32-bit lane j carrying block
// base + j:
//
//     x0 = [ c0.w0 c1.w0 c2.w0 c3.w0 c4.w0 c5.w0 c6.w0 c7.w0 ]
//     x1 = [ c0.w1 c1.w1 ...                            c7.w1 ]
//     x2, x3 likewise for words 2 and 3.
//
// All eight 32-bit lanes stay productive through the XOR, key broadcast, and
// permutation steps. The cost is that _mm256_mul_epu32 only multiplies the low
// half of each 64-bit lane, so each Philox multiply needs two of them -- one
// for the even 32-bit lanes, one for the odd lanes shifted down -- plus a
// repack. The benchmark in issue #12 showed that trade winning by 21% over a
// four-counter layout that avoids the split but idles half of every register.
//
// Because vphilox is header-only, the AVX2 entry points carry
// [[gnu::target("avx2")]] (via VPHILOX_TARGET) rather than relying on a
// translation-unit compile flag. A binary built without -mavx2 still contains
// this kernel and still starts on a CPU that cannot run it; dispatch keeps it
// unreachable there.

#ifndef VPHILOX_DETAIL_KERNEL_AVX2_HPP
#define VPHILOX_DETAIL_KERNEL_AVX2_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_AVX2
#include <immintrin.h>
#endif

namespace vphilox::detail {

/// Blocks processed per vector iteration: eight 32-bit lanes, one counter each.
/// A power of two, which the tail split below relies on.
inline constexpr std::size_t kernel_avx2_width = 8;

#if VPHILOX_HAS_AVX2

namespace avx2 {

/// Unsigned "a < b" per 32-bit lane. AVX2 only compares signed, so both sides
/// are biased by 2^31 first; the signed order of the biased values is the
/// unsigned order of the originals. Result is -1 in lanes where a < b, which
/// is exactly the shape needed to fold a carry in with _mm256_sub_epi32.
VPHILOX_TARGET("avx2")
inline __m256i cmplt_epu32(__m256i a, __m256i b) noexcept {
    const __m256i bias = _mm256_set1_epi32(static_cast<int>(0x80000000u));
    return _mm256_cmpgt_epi32(_mm256_xor_si256(b, bias), _mm256_xor_si256(a, bias));
}

/// Expand `base` into the eight-lane SoA counter registers, lane j holding
/// base + j as a 128-bit little-endian value.
///
/// Only word 0 gains the lane offset; words 1..3 move only when a carry
/// reaches them. `x - mask` adds one wherever mask is -1, so the carry chain
/// is a compare and a subtract per word with no branches. The carry out of
/// word 3 is dropped, matching counter_add's silent wrap at 2^128.
VPHILOX_TARGET("avx2")
inline void counter_lanes(const counter4& base, __m256i& x0, __m256i& x1, __m256i& x2,
                          __m256i& x3) noexcept {
    const __m256i offsets = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);

    const __m256i b0 = _mm256_set1_epi32(static_cast<int>(base.v[0]));
    x0               = _mm256_add_epi32(b0, offsets);
    __m256i carry    = cmplt_epu32(x0, b0);

    const __m256i b1 = _mm256_set1_epi32(static_cast<int>(base.v[1]));
    x1               = _mm256_sub_epi32(b1, carry);
    carry            = cmplt_epu32(x1, b1);

    const __m256i b2 = _mm256_set1_epi32(static_cast<int>(base.v[2]));
    x2               = _mm256_sub_epi32(b2, carry);
    carry            = cmplt_epu32(x2, b2);

    const __m256i b3 = _mm256_set1_epi32(static_cast<int>(base.v[3]));
    x3               = _mm256_sub_epi32(b3, carry);
}

/// Recombine a split wide multiply into the low words of all eight products.
///
/// `even` holds the 64-bit products of lanes 0,2,4,6 and `odd` those of lanes
/// 1,3,5,7. Shifting `odd` up by 32 puts its low halves in the odd 32-bit lane
/// positions, so a blend that takes odd-indexed lanes from it finishes the
/// interleave. Blend rather than and/or: one fewer instruction and no mask
/// constant to keep live.
VPHILOX_TARGET("avx2")
inline __m256i pack_lo(__m256i even, __m256i odd) noexcept {
    return _mm256_blend_epi32(even, _mm256_slli_epi64(odd, 32), 0xAA);
}

/// The same recombination for the high words: `even` shifted down leaves its
/// high halves in the even lanes, and `odd` already carries its high halves in
/// the odd lanes.
VPHILOX_TARGET("avx2")
inline __m256i pack_hi(__m256i even, __m256i odd) noexcept {
    return _mm256_blend_epi32(_mm256_srli_epi64(even, 32), odd, 0xAA);
}

/// Transpose the four SoA word registers back to array-of-structures block
/// order and write 32 words: b0w0..b0w3, b1w0..b1w3, ... b7w3.
///
/// unpack works within each 128-bit half, so the 32- and 64-bit unpacks leave
/// each register holding two blocks that are four apart (block 0 with block 4,
/// and so on). permute2x128 pairs the halves back into consecutive blocks.
/// Stores are unaligned: the kernel contract does not promise an aligned out.
VPHILOX_TARGET("avx2")
inline void store_blocks(__m256i x0, __m256i x1, __m256i x2, __m256i x3, u32* out) noexcept {
    const __m256i t0 = _mm256_unpacklo_epi32(x0, x1);  // w0,w1 of blocks 0,1 | 4,5
    const __m256i t1 = _mm256_unpackhi_epi32(x0, x1);  // w0,w1 of blocks 2,3 | 6,7
    const __m256i t2 = _mm256_unpacklo_epi32(x2, x3);  // w2,w3 of blocks 0,1 | 4,5
    const __m256i t3 = _mm256_unpackhi_epi32(x2, x3);  // w2,w3 of blocks 2,3 | 6,7

    const __m256i b04 = _mm256_unpacklo_epi64(t0, t2);  // block 0 | block 4
    const __m256i b15 = _mm256_unpackhi_epi64(t0, t2);  // block 1 | block 5
    const __m256i b26 = _mm256_unpacklo_epi64(t1, t3);  // block 2 | block 6
    const __m256i b37 = _mm256_unpackhi_epi64(t1, t3);  // block 3 | block 7

    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 0),
                        _mm256_permute2x128_si256(b04, b15, 0x20));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 8),
                        _mm256_permute2x128_si256(b26, b37, 0x20));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 16),
                        _mm256_permute2x128_si256(b04, b15, 0x31));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 24),
                        _mm256_permute2x128_si256(b26, b37, 0x31));
}

/// Eight blocks per iteration; `blocks` must be a multiple of 8.
template <unsigned Rounds>
VPHILOX_TARGET("avx2")
void generate_batches(counter4 base, const key2& k, u32* out, std::size_t blocks) noexcept {
    const __m256i m0 = _mm256_set1_epi32(static_cast<int>(philox_M0));
    const __m256i m1 = _mm256_set1_epi32(static_cast<int>(philox_M1));
    const __m256i w0 = _mm256_set1_epi32(static_cast<int>(philox_W0));
    const __m256i w1 = _mm256_set1_epi32(static_cast<int>(philox_W1));

    for (std::size_t block = 0; block < blocks; block += kernel_avx2_width) {
        __m256i x0, x1, x2, x3;
        counter_lanes(base, x0, x1, x2, x3);

        // The key is held broadcast and bumped with vector adds, so no lane
        // reload happens between rounds. Round 0 uses the unbumped key, as in
        // kernel_scalar::philox4x32.
        __m256i kv0 = _mm256_set1_epi32(static_cast<int>(k.v[0]));
        __m256i kv1 = _mm256_set1_epi32(static_cast<int>(k.v[1]));

        for (unsigned round = 0; round < Rounds; ++round) {
            // philox_M0 * word 0 and philox_M1 * word 2, each split across the
            // even and odd 32-bit lanes.
            const __m256i p0e = _mm256_mul_epu32(x0, m0);
            const __m256i p0o = _mm256_mul_epu32(_mm256_srli_epi64(x0, 32), m0);
            const __m256i p1e = _mm256_mul_epu32(x2, m1);
            const __m256i p1o = _mm256_mul_epu32(_mm256_srli_epi64(x2, 32), m1);

            // The round permutation, matching philox_round exactly:
            //   w0' = hi(M1*w2) ^ w1 ^ k0     w1' = lo(M1*w2)
            //   w2' = hi(M0*w0) ^ w3 ^ k1     w3' = lo(M0*w0)
            const __m256i n0 = _mm256_xor_si256(_mm256_xor_si256(pack_hi(p1e, p1o), x1), kv0);
            const __m256i n2 = _mm256_xor_si256(_mm256_xor_si256(pack_hi(p0e, p0o), x3), kv1);

            x1 = pack_lo(p1e, p1o);
            x3 = pack_lo(p0e, p0o);
            x0 = n0;
            x2 = n2;

            kv0 = _mm256_add_epi32(kv0, w0);
            kv1 = _mm256_add_epi32(kv1, w1);
        }

        store_blocks(x0, x1, x2, x3, out + block * block_words);
        counter_add(base, kernel_avx2_width);
    }
}

}  // namespace avx2

#endif  // VPHILOX_HAS_AVX2

struct kernel_avx2 {
    static constexpr const char* name = "avx2";

    /// Eight counters per register: the full 32-bit lane width, and the layout
    /// issue #12 selected.
    static constexpr std::size_t preferred_blocks = kernel_avx2_width;

    /// True once the intrinsic path exists. Dispatch checks this alongside the
    /// runtime CPU probe, so a stubbed kernel is never selected.
    static constexpr bool implemented = VPHILOX_HAS_AVX2 != 0;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
#if VPHILOX_HAS_AVX2
        // Whole vectors first, then the tail. Splitting here rather than
        // inside the batch loop keeps the hot path free of a remainder test,
        // and routing the tail through the scalar kernel is what makes output
        // independent of how the caller chunks the request.
        const std::size_t batched = blocks & ~(kernel_avx2_width - 1);
        avx2::generate_batches<Rounds>(base, k, out, batched);
        kernel_scalar::generate<Rounds>(counter_advanced(base, batched), k,
                                        out + batched * block_words, blocks - batched);
#else
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
#endif
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_AVX2_HPP
