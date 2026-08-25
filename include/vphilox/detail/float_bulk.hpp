// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/float_bulk.hpp
//
// Bulk u32 -> float conversion, vectorised by the compiler rather than by hand.
//
// Issue #34 asked for _mm256_or_si256 + _mm256_sub_ps spelled out. Measuring
// first showed there is nothing to spell: GCC turns the plain loop below into
//
//     vmovdqu / vpsrld $9 / vpor / vaddps -1.0f / vmovups
//
// which is that sequence exactly, vaddps of -1.0f being the canonical form of
// "- 1.0f". Intrinsics here would be transcription, not optimisation, and they
// would freeze the width at 8 where the loop widens on its own.
//
// What the plain loop cannot do on its own is choose an ISA. vphilox is
// header-only, so this loop compiles with whatever flags the *consumer's*
// translation unit carries: someone building at -O2 with no -mavx2 gets SSE2
// conversion while the AVX2 kernel still runs, because the kernel carries its
// target attribute with it. So the loop is duplicated under VPHILOX_TARGET and
// picked at runtime, the same way the kernels are. The attribute is what makes
// it AVX2 -- the vectorising is the compiler's job either way.
//
// The conversion is elementwise and branch-free, so every width produces
// bit-identical output. Which variant runs is a speed decision, never a stream
// decision.
//
// There is no NEON clone, and that is not an omission. NEON is baseline on
// aarch64, so the consumer's translation unit already compiles the plain loop
// to ushr/orr/fsub over uint32x4_t -- the ISA gap this file exists to close
// does not exist there. A VPHILOX_TARGET("neon") attribute would be inert.

#ifndef VPHILOX_DETAIL_FLOAT_BULK_HPP
#define VPHILOX_DETAIL_FLOAT_BULK_HPP

#include <cstddef>

#include "vphilox/config.hpp"
#include "vphilox/detail/dispatch.hpp"
#include "vphilox/float_cast.hpp"

namespace vphilox::detail {

using float_convert_fn = void (*)(const u32*, float*, std::size_t) noexcept;

/// Baseline conversion: whatever the consumer's translation unit targets.
inline void to_float01_n_baseline(const u32* src, float* dst, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = to_float01(src[i]);
}

#if VPHILOX_HAS_AVX2
/// The identical loop, compiled for AVX2 regardless of the consumer's flags.
VPHILOX_TARGET("avx2")
inline void to_float01_n_avx2(const u32* src, float* dst, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = to_float01(src[i]);
}
#endif

#if VPHILOX_HAS_AVX512
/// The identical loop again at sixteen lanes.
///
/// The shift, the or and the subtract are all avx512f; the dq subset the
/// AVX-512 *kernel* needs is not required here. The gate below is still the
/// kernel's f+dq probe, because the two paths resolving to different backends
/// on the same CPU would be worse than converting eight lanes on the vanishing
/// set of machines that have f without dq.
///
/// Checked rather than assumed: GCC 15 at -O2 compiles this to
///
///     vpsrld $9, (%rdi,%rax), %zmm0 / vpord / vaddps / vmovups %zmm0
///
/// which is the sixteen-lane form of the AVX2 sequence above. It is worth
/// checking because vector width is a tuning decision, not a target one -- GCC
/// tunes some Skylake-derived -mtune values to prefer-vector-width=256, and a
/// compiler that does so here emits the ymm sequence instead. That degrades to
/// a second copy of the AVX2 variant: still bit-identical, just not faster. No
/// test can catch it, because every width produces the same floats by
/// construction.
VPHILOX_TARGET("avx512f")
inline void to_float01_n_avx512(const u32* src, float* dst, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = to_float01(src[i]);
}
#endif

/// Resolve the conversion once per process, from the backend the kernels
/// already resolved to.
///
/// Deriving it rather than repeating the probe is what guarantees the two can
/// never disagree: pinning VPHILOX_BACKEND has to pin the whole path, or a run
/// that says "scalar" would still be converting sixteen lanes at a time. It
/// also inherits dispatch's handling of an override naming a backend this CPU
/// does not have, which the previous hand-rolled check got subtly wrong --
/// VPHILOX_BACKEND=neon on x86 gave the AVX2 kernel and the baseline
/// converter.
inline float_convert_fn resolve_float_convert() noexcept {
    static const float_convert_fn fn = []() -> float_convert_fn {
        switch (active_backend()) {
#if VPHILOX_HAS_AVX512
            case backend::avx512:
                return &to_float01_n_avx512;
#endif
#if VPHILOX_HAS_AVX2
            case backend::avx2:
                return &to_float01_n_avx2;
#endif
            // scalar, and neon -- where the baseline loop already is the NEON
            // loop, because NEON is not an optional ISA on aarch64.
            default:
                return &to_float01_n_baseline;
        }
    }();
    return fn;
}

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_FLOAT_BULK_HPP
