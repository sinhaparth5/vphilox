// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - detail/kernel_scalar.hpp
//
// Reference scalar Philox4x32-R. This is the ground truth: every SIMD kernel
// must reproduce it bit for bit, and the known-answer tests check it against
// the Random123 published vectors.
//
// It is also the slow path the whole project exists to fix -- the dependency
// chain through the 32x32->64 multiply is exactly the ~10x-vs-mt19937 stall
// documented in docs/.

#ifndef VPHILOX_DETAIL_KERNEL_SCALAR_HPP
#define VPHILOX_DETAIL_KERNEL_SCALAR_HPP

#include "vphilox/constants.hpp"
#include "vphilox/counter.hpp"

namespace vphilox::detail {

/// 32x32 -> 64 wide multiply, split into halves.
constexpr void mulhilo32(u32 a, u32 b, u32& hi, u32& lo) noexcept {
    const u64 product = static_cast<u64>(a) * static_cast<u64>(b);
    hi = static_cast<u32>(product >> 32);
    lo = static_cast<u32>(product);
}

/// One Philox4x32 round: two wide multiplies, key XOR, word permutation.
constexpr counter4 philox_round(const counter4& c, const key2& k) noexcept {
    u32 hi0 = 0, lo0 = 0, hi1 = 0, lo1 = 0;
    mulhilo32(philox_M0, c.v[0], hi0, lo0);
    mulhilo32(philox_M1, c.v[2], hi1, lo1);

    return counter4{{
        static_cast<u32>(hi1 ^ c.v[1] ^ k.v[0]),
        lo1,
        static_cast<u32>(hi0 ^ c.v[3] ^ k.v[1]),
        lo0,
    }};
}

/// Weyl key update, applied between rounds.
constexpr key2 bump_key(const key2& k) noexcept {
    return key2{{
        static_cast<u32>(k.v[0] + philox_W0),
        static_cast<u32>(k.v[1] + philox_W1),
    }};
}

/// Full R-round Philox4x32 bijection. The first round uses the unbumped key.
template <unsigned Rounds = default_rounds>
[[nodiscard]] constexpr counter4 philox4x32(counter4 c, key2 k) noexcept {
    static_assert(Rounds > 0 && Rounds <= 16, "Philox4x32 supports 1..16 rounds");

    c = philox_round(c, k);
    for (unsigned r = 1; r < Rounds; ++r) {
        k = bump_key(k);
        c = philox_round(c, k);
    }
    return c;
}

/// The scalar backend, in the shape every kernel implements.
///
/// Kernel contract (shared by scalar/AVX2/AVX-512/NEON):
///   - `generate` writes `blocks * block_words` u32 to `out`.
///   - Block i is philox4x32(base_counter + i, key), so output is independent
///     of how the caller chunks the request.
///   - `blocks` may be any value; the kernel handles its own tail.
///   - `out` need not be aligned; `preferred_blocks` is the SIMD-width
///     multiple that runs without a tail.
struct kernel_scalar {
    static constexpr const char* name = "scalar";
    static constexpr std::size_t preferred_blocks = 1;

    template <unsigned Rounds = default_rounds>
    static constexpr void generate(const counter4& base, const key2& k,
                                   u32* out, std::size_t blocks) noexcept {
        for (std::size_t i = 0; i < blocks; ++i) {
            const counter4 r = philox4x32<Rounds>(counter_advanced(base, i), k);
            for (std::size_t w = 0; w < block_words; ++w) {
                out[i * block_words + w] = r.v[w];
            }
        }
    }
};

}  // namespace vphilox::detail

#endif  // VPHILOX_DETAIL_KERNEL_SCALAR_HPP
