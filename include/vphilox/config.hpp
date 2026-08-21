// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - config.hpp
//
// Compile-time feature detection and portability shims. Included by every
// other vphilox header; includes nothing from vphilox itself.

#ifndef VPHILOX_CONFIG_HPP
#define VPHILOX_CONFIG_HPP

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Architecture
// ---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
#define VPHILOX_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define VPHILOX_ARCH_ARM64 1
#endif

// ---------------------------------------------------------------------------
// Which kernels get compiled in.
//
// A kernel being compiled in does NOT mean it will run: runtime dispatch
// (detail/dispatch.hpp) queries the CPU and picks. VPHILOX_NO_* are set by
// CMake from the VPHILOX_ENABLE_* options; VPHILOX_FORCE_SCALAR overrides all.
// ---------------------------------------------------------------------------
#if defined(VPHILOX_FORCE_SCALAR)
#define VPHILOX_HAS_AVX2 0
#define VPHILOX_HAS_AVX512 0
#define VPHILOX_HAS_NEON 0
#else
#if defined(VPHILOX_ARCH_X86) && !defined(VPHILOX_NO_AVX2)
#define VPHILOX_HAS_AVX2 1
#else
#define VPHILOX_HAS_AVX2 0
#endif
#if defined(VPHILOX_ARCH_X86) && !defined(VPHILOX_NO_AVX512)
#define VPHILOX_HAS_AVX512 1
#else
#define VPHILOX_HAS_AVX512 0
#endif
#if defined(VPHILOX_ARCH_ARM64) && !defined(VPHILOX_NO_NEON)
#define VPHILOX_HAS_NEON 1
#else
#define VPHILOX_HAS_NEON 0
#endif
#endif

// ---------------------------------------------------------------------------
// Inlining and target attributes
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#define VPHILOX_FORCE_INLINE __forceinline
#define VPHILOX_TARGET(isa)
#else
#define VPHILOX_FORCE_INLINE inline __attribute__((always_inline))
#define VPHILOX_TARGET(isa) __attribute__((target(isa)))
#endif

namespace vphilox {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

/// Cache-line size assumed for buffer alignment. 64 B on every x86-64 part and
/// on Graviton; Apple Silicon uses 128 B, so this is a lower bound, not a
/// promise. Only used for alignas() -- correctness never depends on it.
inline constexpr std::size_t cacheline_size = 64;

}  // namespace vphilox

#endif  // VPHILOX_CONFIG_HPP
