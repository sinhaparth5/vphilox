// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - constants.hpp
//
// Philox4x32 algorithm constants and the counter/key value types.
//
// Reference: J. K. Salmon, M. A. Moraes, R. O. Dror, D. E. Shaw,
// "Parallel Random Numbers: As Easy as 1, 2, 3", SC'11.

#ifndef VPHILOX_CONSTANTS_HPP
#define VPHILOX_CONSTANTS_HPP

#include <array>

#include "vphilox/config.hpp"

namespace vphilox {

/// Words in one Philox4x32 counter / one output block.
inline constexpr std::size_t block_words = 4;

/// Words in a Philox4x32 key.
inline constexpr std::size_t key_words = 2;

/// Default round count. 10 is the standard-strength variant (Philox4x32-10);
/// 7 rounds already pass BigCrush, so 10 carries a safety margin.
inline constexpr unsigned default_rounds = 10;

/// Wide-multiply multipliers, in Random123's naming. Each round multiplies
/// counter word 0 by M0 and counter word 2 by M1, 32x32 -> 64.
inline constexpr u32 philox_M0 = 0xD2511F53u;
inline constexpr u32 philox_M1 = 0xCD9E8D57u;

/// Weyl increments applied to the key between rounds, breaking the round
/// symmetry that would otherwise make every round identical.
///   W0 = floor((sqrt(5) - 1) / 2 * 2^32)  -- golden ratio
///   W1 = floor((sqrt(3) - 1) / 2 * 2^32)
inline constexpr u32 philox_W0 = 0x9E3779B9u;
inline constexpr u32 philox_W1 = 0xBB67AE85u;

/// A 128-bit Philox counter, little-endian by word index.
struct counter4 {
    std::array<u32, block_words> v{};

    friend constexpr bool operator==(const counter4&, const counter4&) = default;
};

/// A 64-bit Philox key (the "seed" / stream selector).
struct key2 {
    std::array<u32, key_words> v{};

    friend constexpr bool operator==(const key2&, const key2&) = default;
};

}  // namespace vphilox

#endif  // VPHILOX_CONSTANTS_HPP
