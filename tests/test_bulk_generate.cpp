// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// The bulk path has one job: be indistinguishable from calling operator().
// Every test here is a form of that -- the interesting failures are ordering
// failures at the seams between buffered words, whole blocks, and the tail.

#include <gtest/gtest.h>

#include <numeric>
#include <span>
#include <vector>

#include "vphilox/vphilox.hpp"

using namespace vphilox;

namespace {

/// The reference stream: `n` words straight out of operator().
std::vector<u32> by_call(u64 seed, std::size_t n) {
    engine g{seed};
    std::vector<u32> out(n);
    for (auto& w : out) w = g();
    return out;
}

}  // namespace

TEST(BulkGenerate, MatchesOperatorCallForEveryLength) {
    // Sweep past the buffer boundary (32 words) and the AVX2 batch width
    // (8 blocks = 32 words) rather than sampling a few round numbers.
    constexpr std::size_t max_words = 200;
    const auto reference            = by_call(0xABCDEF01ull, max_words);

    for (std::size_t n = 0; n <= max_words; ++n) {
        SCOPED_TRACE(testing::Message() << "n=" << n);
        engine g{0xABCDEF01ull};
        std::vector<u32> got(n, 0xDEADBEEFu);
        g.generate_n(got.data(), n);
        EXPECT_TRUE(std::equal(got.begin(), got.end(), reference.begin()));
    }
}

TEST(BulkGenerate, ChunkedCallsMatchOneWholeCall) {
    // A caller that asks in pieces must get the same stream as one that asks
    // once. Chunk sizes straddle the buffer size and the SIMD width.
    constexpr std::size_t total = 1024;
    const auto reference        = by_call(7ull, total);

    for (std::size_t chunk : {1u, 3u, 4u, 7u, 8u, 31u, 32u, 33u, 64u, 127u, 128u, 512u}) {
        SCOPED_TRACE(testing::Message() << "chunk=" << chunk);
        engine g{7ull};
        std::vector<u32> got(total, 0xDEADBEEFu);
        for (std::size_t i = 0; i < total; i += chunk) {
            g.generate_n(got.data() + i, std::min<std::size_t>(chunk, total - i));
        }
        EXPECT_EQ(got, reference);
    }
}

TEST(BulkGenerate, InterleavesWithSingleWordCalls) {
    // Mixing the two access patterns is where an off-by-one in the drain would
    // show up: the bulk call has to consume exactly what was left buffered.
    constexpr std::size_t total = 512;
    const auto reference        = by_call(0x1234ull, total);

    for (std::size_t prefix = 0; prefix < 70; ++prefix) {
        SCOPED_TRACE(testing::Message() << "prefix=" << prefix);
        engine g{0x1234ull};
        std::vector<u32> got(total, 0xDEADBEEFu);

        for (std::size_t i = 0; i < prefix; ++i) got[i] = g();
        const std::size_t middle = (total - prefix) / 2;
        g.generate_n(got.data() + prefix, middle);
        for (std::size_t i = prefix + middle; i < total; ++i) got[i] = g();

        EXPECT_EQ(got, reference);
    }
}

TEST(BulkGenerate, LeavesTheEngineWhereOperatorCallWould) {
    // Same stream is not enough -- the engine has to resume in the same place
    // afterwards, or the next call diverges.
    //
    // Note what is deliberately *not* asserted here: counter() equality. The
    // two paths read ahead by different amounts (the bulk path consumes whole
    // blocks straight into the caller's memory, the stepping path always
    // buffers a full refill), so their kernel counters legitimately differ
    // while the visible stream does not. counter() reports where the kernel
    // will read next, not which output the caller will see next.
    for (std::size_t n : {1u, 31u, 32u, 33u, 76u, 77u, 128u, 129u}) {
        SCOPED_TRACE(testing::Message() << "n=" << n);

        engine bulk{99ull};
        std::vector<u32> sink(n);
        bulk.generate_n(sink.data(), n);

        engine stepped{99ull};
        for (std::size_t i = 0; i < n; ++i) (void)stepped();

        for (int i = 0; i < 64; ++i) EXPECT_EQ(bulk(), stepped()) << "diverged at i=" << i;
    }
}

TEST(BulkGenerate, AgreesWithDiscard) {
    // discard(n) and generating n words then throwing them away must land in
    // the same place.
    constexpr std::size_t n = 301;

    engine generated{0xFEEDull};
    std::vector<u32> sink(n);
    generated.generate_n(sink.data(), n);

    engine skipped{0xFEEDull};
    skipped.discard(n);

    for (int i = 0; i < 32; ++i) EXPECT_EQ(generated(), skipped()) << "diverged at i=" << i;
}

TEST(BulkGenerate, HandlesZeroAndDoesNotTouchTheEngine) {
    engine g{5ull};
    const u32 first = g();

    engine same{5ull};
    same.generate_n(nullptr, 0);
    EXPECT_EQ(same(), first);

    same.generate(std::span<u32>{});
    EXPECT_EQ(same(), g());
}

TEST(BulkGenerate, SpanOverloadMatchesPointerForm) {
    constexpr std::size_t n = 300;
    std::vector<u32> via_span(n);
    std::vector<u32> via_pointer(n);

    engine a{0x5EEDull};
    a.generate(std::span<u32>{via_span});

    engine b{0x5EEDull};
    b.generate_n(via_pointer.data(), n);

    EXPECT_EQ(via_span, via_pointer);
}

TEST(BulkGenerate, WritesToUnalignedDestinations) {
    // The kernel contract says `out` need not be aligned, and the direct path
    // hands the caller's pointer straight to it. A destination deliberately
    // offset off any useful boundary must still be correct.
    constexpr std::size_t n = 400;
    const auto reference    = by_call(0xC0FFEEull, n);

    for (std::size_t offset = 0; offset < 8; ++offset) {
        SCOPED_TRACE(testing::Message() << "offset=" << offset);
        std::vector<u32> padded(n + 8, 0xDEADBEEFu);
        engine g{0xC0FFEEull};
        g.generate_n(padded.data() + offset, n);
        EXPECT_TRUE(std::equal(reference.begin(), reference.end(), padded.begin() + offset));
    }
}

TEST(BulkGenerate, DoesNotWritePastTheRequestedLength) {
    constexpr std::size_t n     = 33;  // deliberately not a block multiple
    constexpr u32 guard         = 0xA5A5A5A5u;
    constexpr std::size_t slack = 16;

    std::vector<u32> buf(n + slack, guard);
    engine g{1ull};
    g.generate_n(buf.data(), n);

    for (std::size_t i = n; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], guard) << "clobbered word " << i;
    }
}

TEST(BulkGenerate, RequestSpanningManyRefillsStaysInOrder) {
    // Large enough that the direct path runs several times with buffered
    // remainders in between.
    constexpr std::size_t n = 10'000;
    const auto reference    = by_call(0xBEEFull, n);

    engine g{0xBEEFull};
    std::vector<u32> got(n);
    g.generate(std::span<u32>{got});
    EXPECT_EQ(got, reference);
}
