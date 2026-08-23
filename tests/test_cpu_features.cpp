// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 3 gate: the x86 CPUID probe must agree with the compiler's own view of
// the machine.
//
// This exists because MSVC has no __builtin_cpu_supports, so the CPUID path is
// the only detection MSVC has -- and Windows CI is the one place we cannot
// easily debug it. Running the identical code on Linux and macOS and checking
// it against __builtin_cpu_supports means a mistake in the leaf numbers, the
// bit positions, or the XGETBV mask fails on a runner we can inspect, rather
// than silently leaving Windows users on the scalar kernel.

#include <gtest/gtest.h>

#include "vphilox/detail/cpu_features.hpp"
#include "vphilox/detail/dispatch.hpp"

using namespace vphilox;

TEST(CpuFeatures, ProbeIsCached) {
    // detect_cpu() hands out a reference to one probe result; the CPU cannot
    // gain features mid-process.
    const detail::cpu_features& first  = detail::detect_cpu();
    const detail::cpu_features& second = detail::detect_cpu();
    EXPECT_EQ(&first, &second);
}

#if defined(VPHILOX_ARCH_X86)

TEST(CpuFeatures, CpuidAgreesWithCompilerBuiltin) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();

    const detail::cpu_features probed = detail::x86::probe();

    // __builtin_cpu_supports covers the OS-state check too, so this compares
    // the whole answer, not just the CPUID bits.
    EXPECT_EQ(probed.avx2, __builtin_cpu_supports("avx2") != 0);
    EXPECT_EQ(probed.avx512,
              __builtin_cpu_supports("avx512f") != 0 && __builtin_cpu_supports("avx512dq") != 0);
#else
    GTEST_SKIP() << "no __builtin_cpu_supports to compare against (MSVC)";
#endif
}

TEST(CpuFeatures, Avx512ImpliesXsaveEnabled) {
    // AVX-512 requires the ZMM state bits, which are a superset of the YMM
    // ones. A host reporting avx512 without a usable XSAVE configuration means
    // the mask is wrong.
    if (!detail::detect_cpu().avx512) GTEST_SKIP() << "no AVX-512 on this host";

    const u64 xcr = detail::x86::xcr0();
    EXPECT_EQ(xcr & detail::x86::xcr0_zmm_state, detail::x86::xcr0_zmm_state);
    EXPECT_TRUE(detail::detect_cpu().avx2) << "AVX-512 host must also report AVX2 state";
}

TEST(CpuFeatures, NeonIsNeverReportedOnX86) {
    EXPECT_FALSE(detail::detect_cpu().neon);
}

#elif defined(VPHILOX_ARCH_ARM64)

TEST(CpuFeatures, NeonIsAlwaysPresentOnAarch64) {
    EXPECT_TRUE(detail::detect_cpu().neon);
}

TEST(CpuFeatures, X86FeaturesAreNeverReportedOnArm) {
    const detail::cpu_features& cpu = detail::detect_cpu();
    EXPECT_FALSE(cpu.avx2);
    EXPECT_FALSE(cpu.avx512);
}

#endif

TEST(CpuFeatures, DispatchNeverOutrunsTheProbe) {
    // The whole point of the probe: dispatch must not pick a backend the CPU
    // cannot run. Restates the dispatch-side check from the probe's side, so a
    // detection regression fails here even if dispatch is refactored.
    const detail::cpu_features& cpu = detail::detect_cpu();
    switch (active_backend<default_rounds>()) {
        case backend::avx2:
            EXPECT_TRUE(cpu.avx2);
            break;
        case backend::avx512:
            EXPECT_TRUE(cpu.avx512);
            break;
        case backend::neon:
            EXPECT_TRUE(cpu.neon);
            break;
        case backend::scalar:
            SUCCEED();
            break;
    }
}
