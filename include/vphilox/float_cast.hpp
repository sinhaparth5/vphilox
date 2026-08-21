// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - float_cast.hpp
//
// Division-free uint -> float conversion by IEEE-754 mantissa injection.
//
// The obvious conversion, (float)u / 2^32, costs a vcvtusi2ss plus a vdivps
// (~10-14 cycles latency). Instead, build the float's bit pattern directly:
// force sign=0 and exponent=127, drop the random bits into the mantissa, and
// the result is a uniform value in [1.0, 2.0). Subtract 1.0 to land in
// [0.0, 1.0). Two instructions (vpor + vsubps) on the SIMD path.
//
// The cost is resolution, not correctness: a float has 24 bits of mantissa, so
// only 24 of the 32 random bits survive. Values are uniform on a 2^-24 grid.
// Same story for doubles at 2^-53. If you need the full 32 bits of entropy,
// take the integers.

#ifndef VPHILOX_FLOAT_CAST_HPP
#define VPHILOX_FLOAT_CAST_HPP

#include <bit>

#include "vphilox/config.hpp"

namespace vphilox {

/// u32 -> float uniform in [0, 1). Uses the top 23 bits of `u`.
[[nodiscard]] VPHILOX_FORCE_INLINE constexpr float to_float01(u32 u) noexcept {
    // 0x3F800000 is +1.0f: sign 0, exponent 127, mantissa 0.
    return std::bit_cast<float>(0x3F800000u | (u >> 9)) - 1.0f;
}

/// u64 -> double uniform in [0, 1). Uses the top 52 bits of `u`.
[[nodiscard]] VPHILOX_FORCE_INLINE constexpr double to_double01(u64 u) noexcept {
    // 0x3FF0000000000000 is +1.0: sign 0, exponent 1023, mantissa 0.
    return std::bit_cast<double>(0x3FF0000000000000ull | (u >> 12)) - 1.0;
}

// TODO(phase-3): SIMD forms of the above -- _mm256_or_si256 + _mm256_sub_ps
// and the AVX-512 / NEON equivalents, operating on a whole kernel output block
// so the conversion never leaves vector registers.

}  // namespace vphilox

#endif  // VPHILOX_FLOAT_CAST_HPP
