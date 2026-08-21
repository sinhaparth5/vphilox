// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox - philox.hpp
//
// The user-facing engine: a std::uniform_random_bit_generator over a
// counter-based Philox core, backed by an aligned refill buffer so that
// scalar operator() calls stay O(1) while the kernel keeps producing whole
// SIMD blocks.

#ifndef VPHILOX_PHILOX_HPP
#define VPHILOX_PHILOX_HPP

#include <concepts>
#include <limits>
#include <random>

#include "vphilox/constants.hpp"
#include "vphilox/counter.hpp"
#include "vphilox/detail/dispatch.hpp"
#include "vphilox/float_cast.hpp"

namespace vphilox {

/// Blocks produced per refill. 8 blocks = 32 words = 128 bytes, which is one
/// AVX-512 iteration and two AVX2 iterations, so no backend ever has to split
/// a refill.
inline constexpr std::size_t refill_blocks = 8;
inline constexpr std::size_t refill_words  = refill_blocks * block_words;

/// Counter-based Philox engine.
///
/// Not thread-safe as an object -- but it does not need to be. Give each
/// thread its own engine with a distinct key (or a disjoint counter range) and
/// the streams are independent with zero synchronisation. That is the whole
/// point of a counter-based generator.
template <unsigned Rounds = default_rounds>
class basic_engine {
public:
    using result_type = u32;

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return std::numeric_limits<u32>::max(); }

    static constexpr unsigned rounds = Rounds;

    basic_engine() noexcept { reset(); }

    /// Seed from a 64-bit value. Splits across the two key words.
    explicit basic_engine(u64 seed) noexcept {
        key_.v[0] = static_cast<u32>(seed);
        key_.v[1] = static_cast<u32>(seed >> 32);
        reset();
    }

    /// Full control: explicit key and starting counter.
    basic_engine(key2 k, counter4 start) noexcept
        : cursor_(refill_words),  // force a refill on first use
          key_(k), next_(start) {}

    [[nodiscard]] result_type operator()() noexcept {
        if (cursor_ >= refill_words) [[unlikely]] {
            refill();
        }
        return buffer_[cursor_++];
    }

    /// Uniform float in [0, 1) via mantissa injection.
    [[nodiscard]] float next_float() noexcept { return to_float01((*this)()); }

    /// Uniform double in [0, 1); consumes two 32-bit words.
    [[nodiscard]] double next_double() noexcept {
        const u64 lo = (*this)();
        const u64 hi = (*this)();
        return to_double01((hi << 32) | lo);
    }

    /// Jump to an arbitrary point in the stream in O(1). `n` counts 32-bit
    /// outputs, not blocks -- this is the property a stateful engine cannot
    /// give you without generating everything in between.
    void discard(u64 n) noexcept {
        const u64 remaining = refill_words - cursor_;
        if (n < remaining) {
            cursor_ += static_cast<std::size_t>(n);
            return;
        }
        n -= remaining;

        // Land on the block boundary containing the target, then walk the
        // remainder inside a fresh refill.
        const u64 blocks_to_skip = n / block_words;
        const auto word_offset   = static_cast<std::size_t>(n % block_words);

        counter_add(next_, blocks_to_skip);
        refill();
        cursor_ = word_offset;
    }

    /// Restart the stream at counter 0 with the current key.
    void reset() noexcept {
        next_   = counter4{};
        cursor_ = refill_words;
    }

    [[nodiscard]] key2 key() const noexcept { return key_; }
    /// The next counter the kernel will consume. Not the counter of the value
    /// operator() will return next -- up to `refill_words` outputs may still
    /// be buffered ahead of it.
    [[nodiscard]] counter4 counter() const noexcept { return next_; }

    /// Which kernel this engine's calls actually go through.
    [[nodiscard]] static backend which_backend() noexcept { return active_backend<Rounds>(); }

private:
    void refill() noexcept {
        const auto& entry = detail::resolve_dispatch<Rounds>();
        entry.fn(next_, key_, buffer_, refill_blocks);
        counter_add(next_, refill_blocks);
        cursor_ = 0;
    }

    // Hot scalars first so they share a cache line with each other rather than
    // trailing the 128-byte buffer.
    std::size_t cursor_ = refill_words;
    key2        key_{};
    counter4    next_{};  // counter the next refill will start from
    alignas(cacheline_size) u32 buffer_[refill_words]{};
};

using engine = basic_engine<default_rounds>;

static_assert(std::uniform_random_bit_generator<engine>,
              "vphilox::engine must satisfy std::uniform_random_bit_generator");

}  // namespace vphilox

#endif  // VPHILOX_PHILOX_HPP
