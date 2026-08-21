// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/cpu_features.hpp
//
// Runtime CPU probing. Kept separate from dispatch so it can be tested and
// overridden (VPHILOX_BACKEND env var) without touching kernel selection.

#ifndef VPHILOX_DETAIL_CPU_FEATURES_HPP
#define VPHILOX_DETAIL_CPU_FEATURES_HPP

#include "vphilox/config.hpp"

#if defined(VPHILOX_ARCH_X86) && defined(_MSC_VER)
#  include <intrin.h>
#endif

namespace vphilox::detail {

struct cpu_features {
    bool avx2   = false;
    bool avx512 = false;  // avx512f && avx512dq
    bool neon   = false;
};

/// Probe once; the result cannot change during the process lifetime.
inline const cpu_features& detect_cpu() noexcept {
    static const cpu_features features = [] {
        cpu_features f{};
#if defined(VPHILOX_ARCH_X86)
#  if defined(__GNUC__) || defined(__clang__)
        __builtin_cpu_init();
        f.avx2   = __builtin_cpu_supports("avx2");
        f.avx512 = __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512dq");
#  elif defined(_MSC_VER)
        // TODO(phase-3): MSVC path -- __cpuidex leaf 7 (EBX bit 5 = AVX2,
        // bit 16 = AVX512F, bit 17 = AVX512DQ) plus an XGETBV check that the
        // OS actually saves the YMM/ZMM state.
        f.avx2   = false;
        f.avx512 = false;
#  endif
#elif defined(VPHILOX_ARCH_ARM64)
        f.neon = true;  // NEON is mandatory on aarch64
#endif
        return f;
    }();
    return features;
}

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_CPU_FEATURES_HPP
