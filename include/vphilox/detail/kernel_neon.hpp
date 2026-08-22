// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_neon.hpp
//
// STATUS: Phase 2, not implemented. Falls through to the scalar kernel.
//
// Plan: ARM has no direct analogue of _mm256_mul_epu32. The wide multiply
// comes from vmull_u32, which takes a uint32x2_t and produces a uint64x2_t --
// two 32x32->64 multiplies per instruction, so a 128-bit NEON register holds
// 2 counters where AVX2 holds 4+.
//   - hi extraction: vshrn_n_u64(prod, 32) narrows and shifts in one step.
//   - permutation: vtrn / vzip / vext on uint32x4_t.
//   - Targets Apple Silicon and AWS Graviton; NEON is baseline on aarch64, so
//     no runtime probe is needed -- dispatch selects it unconditionally there.
//   - SVE/SVE2 is a possible later backend; not in scope for Phase 2.
//
// Exit criterion: bit-for-bit equality with kernel_scalar, verified on an
// actual aarch64 runner (CI matrix, not just cross-compilation).

#ifndef VPHILOX_DETAIL_KERNEL_NEON_HPP
#define VPHILOX_DETAIL_KERNEL_NEON_HPP

#include "vphilox/config.hpp"
#include "vphilox/detail/kernel_scalar.hpp"

#if VPHILOX_HAS_NEON
#include <arm_neon.h>
#endif

namespace vphilox::detail {

struct kernel_neon {
    static constexpr const char* name             = "neon";
    static constexpr std::size_t preferred_blocks = 2;
    static constexpr bool implemented             = false;

    template <unsigned Rounds = default_rounds>
    static void generate(const counter4& base, const key2& k, u32* out,
                         std::size_t blocks) noexcept {
        // TODO(phase-2): replace with the vmull_u32-based NEON kernel.
        kernel_scalar::generate<Rounds>(base, k, out, blocks);
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_NEON_HPP
