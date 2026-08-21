// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - counter.hpp
//
// 128-bit arithmetic on Philox counters. This is what makes O(1) seeking
// work: advancing a stream by N is `counter += N`, not N state transitions.

#ifndef VPHILOX_COUNTER_HPP
#define VPHILOX_COUNTER_HPP

#include "vphilox/constants.hpp"

namespace vphilox {

/// c += delta, treating c as a little-endian 128-bit integer. Wraps silently
/// at 2^128, which is the algorithm's period -- unreachable in practice.
constexpr void counter_add(counter4& c, u64 delta) noexcept {
    u64 carry = delta;
    for (std::size_t i = 0; i < block_words && carry != 0; ++i) {
        const u64 sum = static_cast<u64>(c.v[i]) + (carry & 0xFFFFFFFFu);
        c.v[i] = static_cast<u32>(sum);
        carry = (carry >> 32) + (sum >> 32);
    }
}

/// Convenience: the counter `delta` blocks ahead of `c`.
[[nodiscard]] constexpr counter4 counter_advanced(counter4 c, u64 delta) noexcept {
    counter_add(c, delta);
    return c;
}

}  // namespace vphilox

#endif  // VPHILOX_COUNTER_HPP
