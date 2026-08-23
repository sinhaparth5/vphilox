// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/cpu_features.hpp
//
// Runtime CPU probing. Kept separate from dispatch so it can be tested and
// overridden (VPHILOX_BACKEND env var) without touching kernel selection.
//
// The x86 probe is raw CPUID plus XGETBV rather than __builtin_cpu_supports,
// and it is the same code on every x86 compiler. MSVC has no equivalent
// builtin, so a compiler-split implementation would leave the MSVC path
// untested everywhere except Windows CI. Sharing one path means every Linux
// and macOS run exercises the code MSVC depends on, and
// tests/test_cpu_features.cpp checks it against __builtin_cpu_supports
// wherever that builtin exists.

#ifndef VPHILOX_DETAIL_CPU_FEATURES_HPP
#define VPHILOX_DETAIL_CPU_FEATURES_HPP

#include "vphilox/config.hpp"

#if defined(VPHILOX_ARCH_X86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace vphilox::detail {

struct cpu_features {
    bool avx2   = false;
    bool avx512 = false;  // avx512f && avx512dq
    bool neon   = false;
};

#if defined(VPHILOX_ARCH_X86)

namespace x86 {

/// CPUID leaf/subleaf bits this probe reads.
inline constexpr u32 leaf1_ecx_osxsave  = 1u << 27;
inline constexpr u32 leaf7_ebx_avx2     = 1u << 5;
inline constexpr u32 leaf7_ebx_avx512f  = 1u << 16;
inline constexpr u32 leaf7_ebx_avx512dq = 1u << 17;

/// XCR0 bits. A CPU can report an instruction set the OS has not agreed to
/// save on context switch; using it then corrupts the vector registers of
/// whatever runs next. Checking XCR0 is what makes the CPUID answer usable.
inline constexpr u64 xcr0_sse       = 1ull << 1;
inline constexpr u64 xcr0_avx       = 1ull << 2;
inline constexpr u64 xcr0_opmask    = 1ull << 5;
inline constexpr u64 xcr0_zmm_hi256 = 1ull << 6;
inline constexpr u64 xcr0_hi16_zmm  = 1ull << 7;

inline constexpr u64 xcr0_ymm_state = xcr0_sse | xcr0_avx;
inline constexpr u64 xcr0_zmm_state = xcr0_ymm_state | xcr0_opmask | xcr0_zmm_hi256 | xcr0_hi16_zmm;

struct cpuid_regs {
    u32 eax = 0;
    u32 ebx = 0;
    u32 ecx = 0;
    u32 edx = 0;
};

inline cpuid_regs cpuid(u32 leaf, u32 subleaf) noexcept {
    cpuid_regs r{};
#if defined(_MSC_VER)
    int regs[4] = {0, 0, 0, 0};
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    r.eax = static_cast<u32>(regs[0]);
    r.ebx = static_cast<u32>(regs[1]);
    r.ecx = static_cast<u32>(regs[2]);
    r.edx = static_cast<u32>(regs[3]);
#else
    __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
#endif
    return r;
}

/// Read XCR0. Only valid once OSXSAVE is known to be set -- XGETBV faults
/// otherwise. Raw asm on GCC/Clang rather than the _xgetbv intrinsic, which
/// would require putting -mxsave on the whole build.
inline u64 xcr0() noexcept {
#if defined(_MSC_VER)
    return static_cast<u64>(_xgetbv(0));
#else
    u32 lo = 0;
    u32 hi = 0;
    __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (static_cast<u64>(hi) << 32) | lo;
#endif
}

inline cpu_features probe() noexcept {
    cpu_features f{};

    // Leaf 7 carries the AVX2 and AVX-512 bits; nothing to ask if the CPU
    // does not implement it.
    if (cpuid(0, 0).eax < 7) return f;

    // Without OSXSAVE the OS has not enabled XSAVE at all, so no extended
    // vector state is preserved and XGETBV must not be executed.
    if ((cpuid(1, 0).ecx & leaf1_ecx_osxsave) == 0) return f;

    const u64 xcr        = xcr0();
    const bool ymm_saved = (xcr & xcr0_ymm_state) == xcr0_ymm_state;
    const bool zmm_saved = (xcr & xcr0_zmm_state) == xcr0_zmm_state;

    const u32 leaf7_ebx = cpuid(7, 0).ebx;

    f.avx2 = ymm_saved && (leaf7_ebx & leaf7_ebx_avx2) != 0;
    f.avx512 =
        zmm_saved && (leaf7_ebx & leaf7_ebx_avx512f) != 0 && (leaf7_ebx & leaf7_ebx_avx512dq) != 0;
    return f;
}

}  // namespace x86

#endif  // VPHILOX_ARCH_X86

/// Probe once; the result cannot change during the process lifetime.
inline const cpu_features& detect_cpu() noexcept {
    static const cpu_features features = [] {
#if defined(VPHILOX_ARCH_X86)
        return x86::probe();
#elif defined(VPHILOX_ARCH_ARM64)
        cpu_features f{};
        f.neon = true;  // NEON is mandatory on aarch64
        return f;
#else
        return cpu_features{};
#endif
    }();
    return features;
}

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_CPU_FEATURES_HPP
