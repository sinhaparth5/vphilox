// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_avx512.hpp
//
// AVX-512 Philox4x32-R over sixteen interleaved counters.
//
// LANE LAYOUT (issue #12)
//
// The same structure of arrays the AVX2 kernel uses, at twice the width: four
// __m512i, one per counter word, with 32-bit lane j carrying block base + j.
//
//     x0 = [ c0.w0 c1.w0 ... c15.w0 ]
//     x1 = [ c0.w1 c1.w1 ... c15.w1 ]   x2, x3 likewise.
//
// Sixteen blocks per iteration, not the eight the stub advertised. Issue #12
// settled that the full 32-bit lane width beats a half-idle 64-bit layout on
// AVX2, and nothing about a wider register changes that argument. The refill
// buffer moved from 8 blocks to 16 to match, so this kernel still fills one
// refill in exactly one iteration -- at 8 it would have run every engine
// refill entirely through the scalar tail.
//
// Against AVX2 the wins are the native unsigned compare (no 2^31 bias trick to
// get a carry mask) and mask registers for the blend. The multiply is
// unchanged in character: _mm512_mul_epu32 still only takes the low half of
// each 64-bit lane, so each Philox multiply is still two multiplies plus a
// repack.
//
// Gated on AVX512F **and** AVX512DQ to match detail/cpu_features.hpp, which
// probes both (issue #26). Only F instructions are actually used here; DQ is
// carried because the probe requires it and every part shipping F since
// Skylake-SP also has DQ.
//
// Header-only, so the entry points carry [[gnu::target(...)]] via
// VPHILOX_TARGET rather than a translation-unit flag: a binary built without
// -mavx512f still contains this kernel and still starts on a CPU that cannot
// run it, and dispatch keeps it unreachable there.

#ifndef VPHILOX_DETAIL_KERNEL_AVX512_HPP
#define VPHILOX_DETAIL_KERNEL_AVX512_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_AVX512
#include <immintrin.h>
#endif

namespace vphilox::detail {

/// Blocks processed per vector iteration: sixteen 32-bit lanes, one counter
/// each. A power of two, which the tail split below relies on.
inline constexpr std::size_t kernel_avx512_width = 16;

#if VPHILOX_HAS_AVX512

namespace avx512 {

#define VPHILOX_AVX512_TARGET VPHILOX_TARGET("avx512f,avx512dq")

/// Expand `base` into the sixteen-lane SoA counter registers, lane j holding
/// base + j as a 128-bit little-endian value.
///
/// Only word 0 gains the lane offset; words 1..3 move only when a carry
/// reaches them. AVX-512 compares unsigned natively and yields a mask
/// register, so the carry is a masked add of one -- no bias, no -1 subtract.
/// The carry out of word 3 is dropped, matching counter_add's silent wrap at
/// 2^128.
VPHILOX_AVX512_TARGET
inline void counter_lanes(const counter4& base, __m512i& x0, __m512i& x1, __m512i& x2,
                          __m512i& x3) noexcept {
    const __m512i offsets = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i one     = _mm512_set1_epi32(1);

    const __m512i b0 = _mm512_set1_epi32(static_cast<int>(base.v[0]));
    x0               = _mm512_add_epi32(b0, offsets);
    __mmask16 carry  = _mm512_cmplt_epu32_mask(x0, b0);

    const __m512i b1 = _mm512_set1_epi32(static_cast<int>(base.v[1]));
    x1               = _mm512_mask_add_epi32(b1, carry, b1, one);
    carry            = _mm512_cmplt_epu32_mask(x1, b1);

    const __m512i b2 = _mm512_set1_epi32(static_cast<int>(base.v[2]));
    x2               = _mm512_mask_add_epi32(b2, carry, b2, one);
    carry            = _mm512_cmplt_epu32_mask(x2, b2);

    const __m512i b3 = _mm512_set1_epi32(static_cast<int>(base.v[3]));
    x3               = _mm512_mask_add_epi32(b3, carry, b3, one);
}

/// Recombine a split wide multiply into the low words of all sixteen products.
///
/// `even` holds the 64-bit products of the even 32-bit lanes and `odd` those
/// of the odd lanes shifted down. Shifting `odd` back up by 32 puts its low
/// halves in the odd lane positions, so a mask blend on the odd lanes finishes
/// the interleave.
VPHILOX_AVX512_TARGET
inline __m512i pack_lo(__m512i even, __m512i odd) noexcept {
    return _mm512_mask_blend_epi32(0xAAAA, even, _mm512_slli_epi64(odd, 32));
}

/// The same recombination for the high words: `even` shifted down leaves its
/// high halves in the even lanes, and `odd` already carries its high halves in
/// the odd lanes.
VPHILOX_AVX512_TARGET
inline __m512i pack_hi(__m512i even, __m512i odd) noexcept {
    return _mm512_mask_blend_epi32(0xAAAA, _mm512_srli_epi64(even, 32), odd);
}

/// Transpose the four SoA word registers back to array-of-structures block
/// order and write 64 words: b0w0..b0w3, b1w0..b1w3, ... b15w3.
///
/// unpack works inside each 128-bit lane, so after the 32- and 64-bit unpacks
/// each register holds four blocks that are four apart -- r0 carries blocks
/// 0, 4, 8, 12 in its four lanes, r1 carries 1, 5, 9, 13, and so on. Two
/// rounds of shuffle_i32x4 then gather one lane from each of the four, which
/// is what puts consecutive blocks together. Stores are unaligned: the kernel
/// contract does not promise an aligned out.
VPHILOX_AVX512_TARGET
inline void store_blocks(__m512i x0, __m512i x1, __m512i x2, __m512i x3, u32* out) noexcept {
    const __m512i t0 = _mm512_unpacklo_epi32(x0, x1);
    const __m512i t1 = _mm512_unpackhi_epi32(x0, x1);
    const __m512i t2 = _mm512_unpacklo_epi32(x2, x3);
    const __m512i t3 = _mm512_unpackhi_epi32(x2, x3);

    // Whole blocks now, but strided: lane i of r_n is block n + 4*i.
    const __m512i r0 = _mm512_unpacklo_epi64(t0, t2);  // blocks  0,  4,  8, 12
    const __m512i r1 = _mm512_unpackhi_epi64(t0, t2);  // blocks  1,  5,  9, 13
    const __m512i r2 = _mm512_unpacklo_epi64(t1, t3);  // blocks  2,  6, 10, 14
    const __m512i r3 = _mm512_unpackhi_epi64(t1, t3);  // blocks  3,  7, 11, 15

    // Pair the registers up, then interleave their 128-bit lanes.
    const __m512i s0 = _mm512_shuffle_i32x4(r0, r1, 0x44);  // r0.l0 r0.l1 r1.l0 r1.l1
    const __m512i s1 = _mm512_shuffle_i32x4(r2, r3, 0x44);  // r2.l0 r2.l1 r3.l0 r3.l1
    const __m512i s2 = _mm512_shuffle_i32x4(r0, r1, 0xEE);  // r0.l2 r0.l3 r1.l2 r1.l3
    const __m512i s3 = _mm512_shuffle_i32x4(r2, r3, 0xEE);  // r2.l2 r2.l3 r3.l2 r3.l3

    _mm512_storeu_si512(out + 0, _mm512_shuffle_i32x4(s0, s1, 0x88));   // blocks  0- 3
    _mm512_storeu_si512(out + 16, _mm512_shuffle_i32x4(s0, s1, 0xDD));  // blocks  4- 7
    _mm512_storeu_si512(out + 32, _mm512_shuffle_i32x4(s2, s3, 0x88));  // blocks  8-11
    _mm512_storeu_si512(out + 48, _mm512_shuffle_i32x4(s2, s3, 0xDD));  // blocks 12-15
}

/// Sixteen blocks per iteration; `blocks` must be a multiple of 16.
template <unsigned Rounds>
VPHILOX_AVX512_TARGET void generate_batches(counter4 base, const key2& k, u32* out,
                                            std::size_t blocks) noexcept {
    const __m512i m0 = _mm512_set1_epi32(static_cast<int>(philox_M0));
    const __m512i m1 = _mm512_set1_epi32(static_cast<int>(philox_M1));
    const __m512i w0 = _mm512_set1_epi32(static_cast<int>(philox_W0));
    const __m512i w1 = _mm512_set1_epi32(static_cast<int>(philox_W1));

    for (std::size_t block = 0; block < blocks; block += kernel_avx512_width) {
        __m512i x0, x1, x2, x3;
        counter_lanes(base, x0, x1, x2, x3);

        // The key is held broadcast and bumped with vector adds, so no lane
        // reload happens between rounds. Round 0 uses the unbumped key, as in
        // kernel_scalar::philox4x32.
        __m512i kv0 = _mm512_set1_epi32(static_cast<int>(k.v[0]));
        __m512i kv1 = _mm512_set1_epi32(static_cast<int>(k.v[1]));

        for (unsigned round = 0; round < Rounds; ++round) {
            // philox_M0 * word 0 and philox_M1 * word 2, each split across the
            // even and odd 32-bit lanes.
            const __m512i p0e = _mm512_mul_epu32(x0, m0);
            const __m512i p0o = _mm512_mul_epu32(_mm512_srli_epi64(x0, 32), m0);
            const __m512i p1e = _mm512_mul_epu32(x2, m1);
            const __m512i p1o = _mm512_mul_epu32(_mm512_srli_epi64(x2, 32), m1);

            // The round permutation, matching philox_round exactly:
            //   w0' = hi(M1*w2) ^ w1 ^ k0     w1' = lo(M1*w2)
            //   w2' = hi(M0*w0) ^ w3 ^ k1     w3' = lo(M0*w0)
            const __m512i n0 = _mm512_xor_si512(_mm512_xor_si512(pack_hi(p1e, p1o), x1), kv0);
            const __m512i n2 = _mm512_xor_si512(_mm512_xor_si512(pack_hi(p0e, p0o), x3), kv1);

            x1 = pack_lo(p1e, p1o);
            x3 = pack_lo(p0e, p0o);
            x0 = n0;
            x2 = n2;

            kv0 = _mm512_add_epi32(kv0, w0);
            kv1 = _mm512_add_epi32(kv1, w1);
        }

        store_blocks(x0, x1, x2, x3, out + block * block_words);
        counter_add(base, kernel_avx512_width);
    }
}

#undef VPHILOX_AVX512_TARGET

}  // namespace avx512

#endif  // VPHILOX_HAS_AVX512

struct kernel_avx512 {
    static constexpr const char* name = "avx512";

    /// Sixteen counters per register: the full 32-bit lane width.
    static constexpr std::size_t preferred_blocks = kernel_avx512_width;

    /// True once the intrinsic path exists. Dispatch checks this alongside the
    /// runtime CPU probe, so a stubbed kernel is never selected.
    static constexpr bool implemented = VPHILOX_HAS_AVX512 != 0;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
#if VPHILOX_HAS_AVX512
        // Whole vectors first, then the tail. Splitting here rather than
        // inside the batch loop keeps the hot path free of a remainder test,
        // and routing the tail through the scalar kernel is what makes output
        // independent of how the caller chunks the request.
        const std::size_t batched = blocks & ~(kernel_avx512_width - 1);
        avx512::generate_batches<Rounds>(base, k, out, batched);
        kernel_scalar::generate<Rounds>(counter_advanced(base, batched), k,
                                        out + batched * block_words, blocks - batched);
#else
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
#endif
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_AVX512_HPP
