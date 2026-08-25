// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_neon.hpp
//
// NEON Philox4x32-R over four interleaved counters, two groups in flight.
//
// LANE LAYOUT (issue #12)
//
// The same structure of arrays as the x86 kernels, four lanes wide: four
// uint32x4_t, one per counter word, with 32-bit lane j carrying block base + j.
//
//     x0 = [ c0.w0 c1.w0 c2.w0 c3.w0 ]
//     x1 = [ c0.w1 c1.w1 c2.w1 c3.w1 ]   x2, x3 likewise.
//
// Four blocks per register, not the two this file's original plan assumed.
// That plan read vmull_u32 -- which takes a uint32x2_t and widens to
// uint64x2_t -- as capping the layout at two counters. A64 also has
// vmull_high_u32, so the upper half of a full uint32x4_t is reachable in one
// more instruction, and the whole register stays productive through the XOR,
// key and store steps. This is the same trade issue #12 settled for AVX2: two
// multiplies plus a repack beats leaving half the register idle.
//
// TWO GROUPS IN FLIGHT (issue #89)
//
// One four-lane group is not enough work to keep a Cortex-A76 busy. Measured
// at 2.20 cycles/byte the kernel ran roughly 1.3 NEON ops per cycle against a
// 2/cycle peak, because Philox's rounds are a serial chain: each round's
// multiplies depend on the previous round's output, and four lanes issue those
// multiplies far faster than their ~4-cycle latency retires them. The register
// file was not the limit -- per-lane efficiency was in fact higher here than on
// either x86 kernel -- the instruction window simply had nothing else to run.
//
// So the loop carries two independent groups, eight blocks per iteration, and
// alternates their rounds. Group B's multiplies fill the latency shadow of
// group A's and vice versa. The cost is register pressure: two groups hold
// twelve live vectors plus four broadcast constants, and each round needs eight
// uint64x2_t temporaries, which fits aarch64's 32 vector registers with room to
// spare. Neither group's arithmetic changes, so the stream does not move --
// tests/test_cross_platform_parity.cpp is what proves that.
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

/// Blocks held in one group: four 32-bit lanes, one counter each.
/// A power of two, which the tail split below relies on.
inline constexpr std::size_t kernel_neon_width = 4;

/// Independent groups interleaved per iteration, to cover multiply latency.
inline constexpr std::size_t kernel_neon_groups = 2;

/// Blocks per full iteration, and the width dispatch advertises.
inline constexpr std::size_t kernel_neon_batch = kernel_neon_width * kernel_neon_groups;

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

/// Four counters mid-flight: the SoA state words plus this group's running key.
/// Holding the key per group rather than per loop is what lets two groups share
/// the round code without either reloading it.
struct lane_group {
    uint32x4_t x0, x1, x2, x3;
    uint32x4_t kv0, kv1;
};

/// Round 0 uses the unbumped key, as in kernel_scalar::philox4x32.
inline lane_group load_group(const counter4& base, const key2& k) noexcept {
    lane_group g;
    counter_lanes(base, g.x0, g.x1, g.x2, g.x3);
    g.kv0 = vdupq_n_u32(k.v[0]);
    g.kv1 = vdupq_n_u32(k.v[1]);
    return g;
}

/// One Philox round on one group, matching philox_round exactly:
///   w0' = hi(M1*w2) ^ w1 ^ k0     w1' = lo(M1*w2)
///   w2' = hi(M0*w0) ^ w3 ^ k1     w3' = lo(M0*w0)
/// then the key advances by the Weyl increments.
inline void round_group(lane_group& g, uint32x4_t m0, uint32x4_t m1, uint32x4_t w0,
                        uint32x4_t w1) noexcept {
    // philox_M0 * word 0 and philox_M1 * word 2, each widened in two halves:
    // the low pair of lanes, then the high pair.
    const uint64x2_t p0l = vmull_u32(vget_low_u32(g.x0), vget_low_u32(m0));
    const uint64x2_t p0h = vmull_high_u32(g.x0, m0);
    const uint64x2_t p1l = vmull_u32(vget_low_u32(g.x2), vget_low_u32(m1));
    const uint64x2_t p1h = vmull_high_u32(g.x2, m1);

    const uint32x4_t n0 = veorq_u32(veorq_u32(pack_hi(p1l, p1h), g.x1), g.kv0);
    const uint32x4_t n2 = veorq_u32(veorq_u32(pack_hi(p0l, p0h), g.x3), g.kv1);

    g.x1 = pack_lo(p1l, p1h);
    g.x3 = pack_lo(p0l, p0h);
    g.x0 = n0;
    g.x2 = n2;

    g.kv0 = vaddq_u32(g.kv0, w0);
    g.kv1 = vaddq_u32(g.kv1, w1);
}

/// vst4q de-interleaves on the way out, which is exactly the SoA-to-AoS
/// transpose: lane j of the four registers lands as block j's four consecutive
/// words. Unaligned is fine; the kernel contract does not promise an aligned
/// out.
inline void store_group(const lane_group& g, u32* out) noexcept {
    uint32x4x4_t blockset;
    blockset.val[0] = g.x0;
    blockset.val[1] = g.x1;
    blockset.val[2] = g.x2;
    blockset.val[3] = g.x3;
    vst4q_u32(out, blockset);
}

/// `blocks` must be a multiple of kernel_neon_width. Pairs of groups run first;
/// a single leftover group -- there can be at most one -- runs after.
template <unsigned Rounds>
void generate_batches(counter4 base, const key2& k, u32* out, std::size_t blocks) noexcept {
    const uint32x4_t m0 = vdupq_n_u32(philox_M0);
    const uint32x4_t m1 = vdupq_n_u32(philox_M1);
    const uint32x4_t w0 = vdupq_n_u32(philox_W0);
    const uint32x4_t w1 = vdupq_n_u32(philox_W1);

    std::size_t block = 0;

    for (; block + kernel_neon_batch <= blocks; block += kernel_neon_batch) {
        lane_group a = load_group(base, k);
        lane_group b = load_group(counter_advanced(base, kernel_neon_width), k);

        // Alternating the two chains in source order is the whole point: the
        // groups share no register, so the scheduler is free to issue B's
        // multiplies while A's are still in flight.
        for (unsigned round = 0; round < Rounds; ++round) {
            round_group(a, m0, m1, w0, w1);
            round_group(b, m0, m1, w0, w1);
        }

        store_group(a, out + block * block_words);
        store_group(b, out + (block + kernel_neon_width) * block_words);

        counter_add(base, kernel_neon_batch);
    }

    for (; block + kernel_neon_width <= blocks; block += kernel_neon_width) {
        lane_group a = load_group(base, k);
        for (unsigned round = 0; round < Rounds; ++round) {
            round_group(a, m0, m1, w0, w1);
        }
        store_group(a, out + block * block_words);
        counter_add(base, kernel_neon_width);
    }
}

}  // namespace neon

#endif  // VPHILOX_HAS_NEON

struct kernel_neon {
    static constexpr const char* name = "neon";

    /// Eight blocks: two independent four-lane groups, which is the width that
    /// runs with no leftover group and no scalar tail.
    static constexpr std::size_t preferred_blocks = kernel_neon_batch;

    /// True once the intrinsic path exists. Dispatch checks this alongside the
    /// architecture gate, so a stubbed kernel is never selected.
    static constexpr bool implemented = VPHILOX_HAS_NEON != 0;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
#if VPHILOX_HAS_NEON
        // Whole groups first, then the tail. Splitting here rather than inside
        // the batch loop keeps the hot path free of a remainder test, and
        // routing the tail through the scalar kernel is what makes output
        // independent of how the caller chunks the request. The split is on the
        // group width, not the batch width, so a request of 4..7 blocks still
        // gets a vector group instead of falling entirely to scalar.
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
