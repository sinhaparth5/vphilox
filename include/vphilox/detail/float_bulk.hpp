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

/// Resolve the conversion once per process.
///
/// Gated on the same CPU probe and the same VPHILOX_BACKEND override as the
/// kernels: pinning a backend has to pin the whole path, or a benchmark that
/// says "scalar" would still be converting eight lanes at a time.
inline float_convert_fn resolve_float_convert() noexcept {
    static const float_convert_fn fn = []() -> float_convert_fn {
#if VPHILOX_HAS_AVX2
        bool has_override    = false;
        const backend forced = backend_override(has_override);
        if (has_override && forced != backend::avx2 && forced != backend::avx512) {
            return &to_float01_n_baseline;
        }
        if (detect_cpu().avx2) return &to_float01_n_avx2;
#endif
        return &to_float01_n_baseline;
    }();
    return fn;
}

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_FLOAT_BULK_HPP
