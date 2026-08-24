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
        c.v[i]        = static_cast<u32>(sum);
        carry         = (carry >> 32) + (sum >> 32);
    }
}

/// Convenience: the counter `delta` blocks ahead of `c`.
[[nodiscard]] constexpr counter4 counter_advanced(counter4 c, u64 delta) noexcept {
    counter_add(c, delta);
    return c;
}

/// c -= delta, the exact inverse of counter_add, borrowing the same way it
/// carries and wrapping at 2^128 rather than saturating. Serialization needs
/// it: the engine tracks the counter its *next refill* will start from, which
/// runs ahead of the value the caller will see next by however much is still
/// buffered, and recovering the caller-visible position means stepping back.
constexpr void counter_sub(counter4& c, u64 delta) noexcept {
    u64 borrow = delta;
    for (std::size_t i = 0; i < block_words && borrow != 0; ++i) {
        const u64 lo   = borrow & 0xFFFFFFFFu;
        const u64 diff = static_cast<u64>(c.v[i]) - lo;
        c.v[i]         = static_cast<u32>(diff);
        // diff >> 63 is the sign bit of the wrapped subtraction: 1 exactly
        // when c.v[i] < lo, which is when the next word owes a borrow.
        borrow = (borrow >> 32) + (diff >> 63);
    }
}

/// Convenience: the counter `delta` blocks behind `c`.
[[nodiscard]] constexpr counter4 counter_retreated(counter4 c, u64 delta) noexcept {
    counter_sub(c, delta);
    return c;
}

}  // namespace vphilox

#endif  // VPHILOX_COUNTER_HPP
