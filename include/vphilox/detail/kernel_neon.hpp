// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_neon.hpp
//
// NEON Philox4x32-R over four interleaved counters.
//
// LANE LAYOUT (issue #12, docs/benchmarks/simd-lane-layout.md)
//
// The same structure of arrays as the x86 kernels, four lanes wide: four
// uint32x4_t, one per counter word, with 32-bit lane j carrying block base + j.
//
//     x0 = [ c0.w0 c1.w0 c2.w0 c3.w0 ]
//     x1 = [ c0.w1 c1.w1 c2.w1 c3.w1 ]   x2, x3 likewise.
//
// Four blocks per iteration, not the two this file's original plan assumed.
// That plan read vmull_u32 -- which takes a uint32x2_t and widens to
// uint64x2_t -- as capping the layout at two counters. A64 also has
// vmull_high_u32, so the upper half of a full uint32x4_t is reachable in one
// more instruction, and the whole register stays productive through the XOR,
// key and store steps. This is the same trade issue #12 settled for AVX2: two
// multiplies plus a repack beats leaving half the register idle.
//
// The store is where NEON is genuinely better than either x86 kernel.
// vst4q_u32 writes four registers de-interleaved, which *is* the SoA-to-AoS
// transpose -- one instruction against AVX2's eight-step unpack/permute
// sequence and AVX-512's two rounds of shuffle_i32x4.
//
// NEON is baseline on aarch64, so there is no runtime probe and no
// [[gnu::target]]: dispatch selects this unconditionally where it is compiled
// in. VPHILOX_ARCH_ARM64 is aarch64/ARM64 only, never armv7, which is what
// makes the A64-only widening intrinsics above safe to use unguarded.

#ifndef VPHILOX_DETAIL_KERNEL_NEON_HPP
#define VPHILOX_DETAIL_KERNEL_NEON_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_NEON
#include <arm_neon.h>
#endif

namespace vphilox::detail {

/// Blocks processed per vector iteration: four 32-bit lanes, one counter each.
/// A power of two, which the tail split below relies on.
inline constexpr std::size_t kernel_neon_width = 4;

#if VPHILOX_HAS_NEON

namespace neon {

/// Expand `base` into the four-lane SoA counter registers, lane j holding
/// base + j as a 128-bit little-endian value.
///
/// Only word 0 gains the lane offset; words 1..3 move only when a carry
/// reaches them. vcltq_u32 compares unsigned natively and sets all ones where
/// true, and subtracting all-ones is adding one modulo 2^32, so each word is a
/// compare and a subtract with no branches. The carry out of word 3 is
/// dropped, matching counter_add's silent wrap at 2^128.
inline void counter_lanes(const counter4& base, uint32x4_t& x0, uint32x4_t& x1, uint32x4_t& x2,
                          uint32x4_t& x3) noexcept {
    const uint32_t off[4]    = {0, 1, 2, 3};
    const uint32x4_t offsets = vld1q_u32(off);

    const uint32x4_t b0 = vdupq_n_u32(base.v[0]);
    x0                  = vaddq_u32(b0, offsets);
    uint32x4_t carry    = vcltq_u32(x0, b0);

    const uint32x4_t b1 = vdupq_n_u32(base.v[1]);
    x1                  = vsubq_u32(b1, carry);
    carry               = vcltq_u32(x1, b1);

    const uint32x4_t b2 = vdupq_n_u32(base.v[2]);
    x2                  = vsubq_u32(b2, carry);
    carry               = vcltq_u32(x2, b2);

    const uint32x4_t b3 = vdupq_n_u32(base.v[3]);
    x3                  = vsubq_u32(b3, carry);
}

/// The low 32 bits of all four 64-bit products, packed back into one register.
/// vmovn takes the low half of each lane; the _high form appends the second
/// pair without a separate combine.
inline uint32x4_t pack_lo(uint64x2_t lo, uint64x2_t hi) noexcept {
    return vmovn_high_u64(vmovn_u64(lo), hi);
}

/// The high 32 bits of the same products. vshrn narrows and shifts in one
/// step, which is the instruction that makes the hi extraction cheaper here
/// than the shift-then-blend the x86 kernels need.
inline uint32x4_t pack_hi(uint64x2_t lo, uint64x2_t hi) noexcept {
    return vshrn_high_n_u64(vshrn_n_u64(lo, 32), hi, 32);
}

/// Four blocks per iteration; `blocks` must be a multiple of 4.
template <unsigned Rounds>
void generate_batches(counter4 base, const key2& k, u32* out, std::size_t blocks) noexcept {
    const uint32x4_t m0 = vdupq_n_u32(philox_M0);
    const uint32x4_t m1 = vdupq_n_u32(philox_M1);
    const uint32x4_t w0 = vdupq_n_u32(philox_W0);
    const uint32x4_t w1 = vdupq_n_u32(philox_W1);

    for (std::size_t block = 0; block < blocks; block += kernel_neon_width) {
        uint32x4_t x0, x1, x2, x3;
        counter_lanes(base, x0, x1, x2, x3);

        // The key is held broadcast and bumped with vector adds, so no lane
        // reload happens between rounds. Round 0 uses the unbumped key, as in
        // kernel_scalar::philox4x32.
        uint32x4_t kv0 = vdupq_n_u32(k.v[0]);
        uint32x4_t kv1 = vdupq_n_u32(k.v[1]);

        for (unsigned round = 0; round < Rounds; ++round) {
            // philox_M0 * word 0 and philox_M1 * word 2, each widened in two
            // halves: the low pair of lanes, then the high pair.
            const uint64x2_t p0l = vmull_u32(vget_low_u32(x0), vget_low_u32(m0));
            const uint64x2_t p0h = vmull_high_u32(x0, m0);
            const uint64x2_t p1l = vmull_u32(vget_low_u32(x2), vget_low_u32(m1));
            const uint64x2_t p1h = vmull_high_u32(x2, m1);

            // The round permutation, matching philox_round exactly:
            //   w0' = hi(M1*w2) ^ w1 ^ k0     w1' = lo(M1*w2)
            //   w2' = hi(M0*w0) ^ w3 ^ k1     w3' = lo(M0*w0)
            const uint32x4_t n0 = veorq_u32(veorq_u32(pack_hi(p1l, p1h), x1), kv0);
            const uint32x4_t n2 = veorq_u32(veorq_u32(pack_hi(p0l, p0h), x3), kv1);

            x1 = pack_lo(p1l, p1h);
            x3 = pack_lo(p0l, p0h);
            x0 = n0;
            x2 = n2;

            kv0 = vaddq_u32(kv0, w0);
            kv1 = vaddq_u32(kv1, w1);
        }

        // vst4q de-interleaves on the way out, which is exactly the SoA-to-AoS
        // transpose: lane j of the four registers lands as block j's four
        // consecutive words. Unaligned is fine; the kernel contract does not
        // promise an aligned out.
        uint32x4x4_t blockset;
        blockset.val[0] = x0;
        blockset.val[1] = x1;
        blockset.val[2] = x2;
        blockset.val[3] = x3;
        vst4q_u32(out + block * block_words, blockset);

        counter_add(base, kernel_neon_width);
    }
}

}  // namespace neon

#endif  // VPHILOX_HAS_NEON

struct kernel_neon {
    static constexpr const char* name = "neon";

    /// Four counters per register: the full 32-bit lane width of a NEON
    /// register.
    static constexpr std::size_t preferred_blocks = kernel_neon_width;

    /// True once the intrinsic path exists. Dispatch checks this alongside the
    /// architecture gate, so a stubbed kernel is never selected.
    static constexpr bool implemented = VPHILOX_HAS_NEON != 0;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
#if VPHILOX_HAS_NEON
        // Whole vectors first, then the tail. Splitting here rather than
        // inside the batch loop keeps the hot path free of a remainder test,
        // and routing the tail through the scalar kernel is what makes output
        // independent of how the caller chunks the request.
        const std::size_t batched = blocks & ~(kernel_neon_width - 1);
        neon::generate_batches<Rounds>(base, k, out, batched);
        kernel_scalar::generate<Rounds>(counter_advanced(base, batched), k,
                                        out + batched * block_words, blocks - batched);
#else
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
#endif
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_NEON_HPP
