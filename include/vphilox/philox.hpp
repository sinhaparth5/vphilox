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

#include <algorithm>
#include <concepts>
#include <cstring>
#include <limits>
#include <random>
#include <span>

#include "vphilox/constants.hpp"
#include "vphilox/counter.hpp"
#include "vphilox/detail/dispatch.hpp"
#include "vphilox/detail/float_bulk.hpp"
#include "vphilox/float_cast.hpp"

namespace vphilox {

/// Blocks produced per refill. 8 blocks = 32 words = 128 bytes, which is one
/// AVX-512 iteration and two AVX2 iterations, so no backend ever has to split
/// a refill.
inline constexpr std::size_t refill_blocks = 8;
inline constexpr std::size_t refill_words  = refill_blocks * block_words;

/// Words converted per pass in the bulk float path. 256 words is 1 KiB of
/// stack that stays resident in L1 across the generate/convert pair, and 64
/// blocks -- comfortably past every backend's preferred_blocks, so the
/// generating half always takes the direct path.
inline constexpr std::size_t float_tile_words = 256;

namespace detail {

/// Stand-in for the standard's SeedSequence requirements.
///
/// Its real job is exclusion, and the failure it prevents is quieter than it
/// looks. An unconstrained `basic_engine(Sseq&)` accepts *any* non-const
/// lvalue, so `std::is_constructible_v<engine, Widget&>` answers true and the
/// mismatch only surfaces as a hard error inside the constructor body -- past
/// the point where a caller's SFINAE can see it, with diagnostics pointing
/// into this header rather than at the call. Seeding itself would still come
/// out right (the body's `seed(q)` falls through to `seed(u64)` for any
/// integer lvalue), which is exactly what makes the gap easy to miss.
template <class S>
concept seed_sequence = requires(S& s, u32* first, u32* last) {
    typename S::result_type;
    s.generate(first, last);
};

}  // namespace detail

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
    explicit basic_engine(u64 s) noexcept { seed(s); }

    /// Seed from a standard seed sequence, so `std::mt19937 rng(seq)` call
    /// sites port across unchanged.
    ///
    /// The exclusion of `basic_engine` itself is not currently reachable --
    /// the engine's own `generate` takes one argument, so it fails the
    /// concept -- but a future two-iterator `generate` overload would turn
    /// this into a copy constructor for non-const lvalues. Cheaper to pin
    /// than to debug.
    template <class Sseq>
        requires detail::seed_sequence<Sseq> && (!std::same_as<Sseq, basic_engine>)
    explicit basic_engine(Sseq& q) {
        seed(q);
    }

    /// Full control: explicit key and starting counter.
    basic_engine(key2 k, counter4 start) noexcept
        : cursor_(refill_words),  // force a refill on first use
          key_(k),
          next_(start) {}

    [[nodiscard]] result_type operator()() noexcept {
        if (cursor_ >= refill_words) [[unlikely]] {
            refill();
        }
        return buffer_[cursor_++];
    }

    /// Fill `count` words at `dst`, bypassing the refill buffer for the wide
    /// middle of the request.
    ///
    /// Produces exactly what `count` successive `operator()` calls would
    /// produce and leaves the engine in exactly the state they would leave it,
    /// so bulk and single-word access can be mixed freely on one engine.
    ///
    /// This exists because the buffer is not free: draining it word by word is
    /// a second pass over every byte, and once the kernel is vectorised that
    /// pass costs about as much as generating the data did. Writing straight
    /// into the caller's memory is the only way to skip it. See
    /// `docs/benchmarks/buffer-overhead-2026-08-23.md`.
    void generate_n(result_type* dst, std::size_t count) noexcept {
        const auto& entry = detail::resolve_dispatch<Rounds>();

        while (true) {
            // Whatever is already buffered belongs to the caller first --
            // jumping straight to the kernel would reorder the stream.
            const std::size_t buffered = std::min(count, refill_words - cursor_);
            if (buffered != 0) {
                std::memcpy(dst, buffer_ + cursor_, buffered * sizeof(result_type));
                cursor_ += buffered;
                dst += buffered;
                count -= buffered;
            }
            if (count == 0) return;

            // The buffer is empty at this point. Only go direct when the
            // request is wide enough to fill the backend's tail-free width:
            // a narrower one would spend the whole call in the kernel's scalar
            // tail and come out slower than the buffer it was avoiding.
            const std::size_t blocks = count / block_words;
            if (blocks >= entry.preferred_blocks) {
                entry.fn(next_, key_, dst, blocks);
                counter_add(next_, blocks);
                const std::size_t words = blocks * block_words;
                dst += words;
                count -= words;
                if (count == 0) return;
            }

            // Either the request was too narrow to go direct, or a sub-block
            // remainder is left. Both are served from a fresh buffer.
            refill();
        }
    }

    /// Span form of `generate_n`.
    void generate(std::span<result_type> dst) noexcept { generate_n(dst.data(), dst.size()); }

    /// Fill `count` floats in [0, 1), equivalent to `count` next_float()
    /// calls in both output and resulting engine state.
    ///
    /// Generation and conversion are separate passes because nothing else is
    /// available: the kernels write integers, so the bytes have to be read
    /// back to be converted. The pass is kept off the caller's array and onto
    /// a 1 KiB tile instead -- converting in place over `dst` would touch it
    /// three times (write, read, write) where the tile touches it once, and
    /// the tile itself never leaves L1.
    ///
    /// Fusing the conversion into the kernels would remove the pass outright,
    /// and `bench_float` measures the prize: the gap between this and
    /// `generate_n(u32*)`. It has not been measured on a machine that can
    /// hold a clock -- see docs/benchmarks/float-conversion-2026-08-24.md.
    void generate_n(float* dst, std::size_t count) noexcept {
        const detail::float_convert_fn convert = detail::resolve_float_convert();

        u32 tile[float_tile_words];
        while (count != 0) {
            const std::size_t n = std::min(count, float_tile_words);
            generate_n(tile, n);
            convert(tile, dst, n);
            dst += n;
            count -= n;
        }
    }

    /// Span form of the bulk float path.
    void generate(std::span<float> dst) noexcept { generate_n(dst.data(), dst.size()); }

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

    /// Re-seed from a 64-bit value and restart the stream at counter 0.
    void seed(u64 s) noexcept {
        key_.v[0] = static_cast<u32>(s);
        key_.v[1] = static_cast<u32>(s >> 32);
        reset();
    }

    /// Re-seed from a seed sequence and restart the stream at counter 0.
    ///
    /// Only the key is drawn: two words, the whole of it. A Philox key
    /// selects a stream rather than accumulating state, so there is nothing
    /// wider to fill, and the counter deliberately starts at zero so the full
    /// 2^128 stream stays ahead of every freshly seeded engine. Nor is any
    /// key degenerate -- all-zeros included, unlike an LCG or a Mersenne
    /// Twister -- so the words are used exactly as the sequence hands them
    /// over.
    ///
    /// That mapping from sequence to key is part of the frozen bit stream:
    /// a given seed sequence must keep producing the key it produces today.
    template <detail::seed_sequence Sseq>
    void seed(Sseq& q) {
        u32 words[key_words]{};
        q.generate(words, words + key_words);
        key_.v[0] = words[0];
        key_.v[1] = words[1];
        reset();
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
    key2 key_{};
    counter4 next_{};  // counter the next refill will start from
    alignas(cacheline_size) u32 buffer_[refill_words]{};
};

using engine = basic_engine<default_rounds>;

static_assert(std::uniform_random_bit_generator<engine>,
              "vphilox::engine must satisfy std::uniform_random_bit_generator");

}  // namespace vphilox

#endif  // VPHILOX_PHILOX_HPP
