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
    // Typed rather than a bare nullptr: generate_n is overloaded on u32* and
    // float*, so an untyped null is ambiguous by construction.
    same.generate_n(static_cast<u32*>(nullptr), 0);
    EXPECT_EQ(same(), first);

    same.generate(std::span<u32>{});
    EXPECT_EQ(same(), g());

    same.generate_n(static_cast<float*>(nullptr), 0);
    same.generate(std::span<float>{});
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

// ---------------------------------------------------------------------------
// The float bulk path. Same contract, one conversion further on: `n` floats
// must equal `n` next_float() calls and leave the engine where they would.
// The seams that matter here are the 256-word conversion tile boundaries,
// which do not line up with the 32-word refill boundaries.
// ---------------------------------------------------------------------------

namespace {

/// The reference stream: `n` floats straight out of next_float().
std::vector<float> floats_by_call(u64 seed, std::size_t n) {
    engine g{seed};
    std::vector<float> out(n);
    for (auto& f : out) f = g.next_float();
    return out;
}

}  // namespace

TEST(BulkGenerateFloat, MatchesNextFloatAcrossTileBoundaries) {
    // Either side of the 256-word tile and the 32-word refill, plus a size
    // that is a multiple of neither.
    constexpr std::size_t sizes[] = {0, 1, 31, 32, 33, 255, 256, 257, 512, 700, 4097};
    for (std::size_t n : sizes) {
        const auto reference = floats_by_call(0xF10Aull, n);

        engine g{0xF10Aull};
        std::vector<float> got(n);
        g.generate_n(got.data(), n);

        ASSERT_EQ(got.size(), reference.size());
        for (std::size_t i = 0; i < n; ++i) {
            // Bit-exact, not approximate: this is the same arithmetic twice.
            EXPECT_EQ(got[i], reference[i]) << "n=" << n << " word " << i;
        }
    }
}

TEST(BulkGenerateFloat, LeavesTheEngineWhereNextFloatWould) {
    constexpr std::size_t n = 1000;

    engine bulk{7ull};
    std::vector<float> sink(n);
    bulk.generate_n(sink.data(), n);

    engine stepped{7ull};
    for (std::size_t i = 0; i < n; ++i) static_cast<void>(stepped.next_float());

    // Not counter(): that is the kernel's next counter, and the two engines
    // legitimately disagree on it. The bulk path consumed 250 blocks and left
    // nothing buffered; the stepped one consumed 256 and is holding 24 words
    // in hand. What has to match is the stream from here on -- long enough to
    // cross several refills, so a stale buffer would show up.
    for (int i = 0; i < 300; ++i) EXPECT_EQ(bulk(), stepped()) << "word " << i;
}

TEST(BulkGenerateFloat, MixesWithSingleValueCalls) {
    engine mixed{99ull};
    engine reference{99ull};

    std::vector<float> chunk(300);
    for (int i = 0; i < 5; ++i) static_cast<void>(mixed.next_float());
    mixed.generate(std::span<float>{chunk});
    for (int i = 0; i < 5; ++i) static_cast<void>(mixed.next_float());

    for (int i = 0; i < 5; ++i) static_cast<void>(reference.next_float());
    for (auto& f : chunk) EXPECT_EQ(f, reference.next_float());
    for (int i = 0; i < 5; ++i) static_cast<void>(reference.next_float());

    EXPECT_EQ(mixed(), reference());
}

TEST(BulkGenerateFloat, StaysInTheUnitInterval) {
    constexpr std::size_t n = 100'000;
    engine g{3ull};
    std::vector<float> out(n);
    g.generate(std::span<float>{out});

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_GE(out[i], 0.0f) << "index " << i;
        ASSERT_LT(out[i], 1.0f) << "index " << i;
    }
}

TEST(BulkGenerateFloat, DoesNotWriteBeyondTheRequest) {
    constexpr std::size_t n     = 257;  // one past a tile
    constexpr std::size_t slack = 16;
    const float guard           = -1.0f;

    std::vector<float> buf(n + slack, guard);
    engine g{1ull};
    g.generate_n(buf.data(), n);

    for (std::size_t i = n; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], guard) << "clobbered float " << i;
    }
}

// The two conversion variants are the same loop at two widths. Since the
// conversion is elementwise, they must agree bit for bit -- the ISA choice is
// a speed decision, never a stream decision.
TEST(BulkGenerateFloat, ConversionWidthsAgreeBitForBit) {
#if VPHILOX_HAS_AVX2
    if (!detail::detect_cpu().avx2) GTEST_SKIP() << "no AVX2 on this CPU";

    constexpr std::size_t n = 4096 + 5;  // deliberately not a vector multiple
    std::vector<u32> src(n);
    engine g{0xABCDull};
    g.generate(std::span<u32>{src});

    std::vector<float> baseline(n), wide(n);
    detail::to_float01_n_baseline(src.data(), baseline.data(), n);
    detail::to_float01_n_avx2(src.data(), wide.data(), n);

    EXPECT_EQ(baseline, wide);
#else
    GTEST_SKIP() << "AVX2 not compiled in";
#endif
}

// The sixteen-lane variant has to clear the same bar. It is gated on the
// kernel's f+dq probe rather than on avx512f alone, so this skips exactly where
// the AVX-512 kernel would not run.
TEST(BulkGenerateFloat, Avx512ConversionAgreesBitForBit) {
#if VPHILOX_HAS_AVX512
    if (!detail::detect_cpu().avx512) GTEST_SKIP() << "no AVX-512 on this CPU";

    constexpr std::size_t n = 4096 + 5;  // deliberately not a vector multiple
    std::vector<u32> src(n);
    engine g{0xABCDull};
    g.generate(std::span<u32>{src});

    std::vector<float> baseline(n), wide(n);
    detail::to_float01_n_baseline(src.data(), baseline.data(), n);
    detail::to_float01_n_avx512(src.data(), wide.data(), n);

    EXPECT_EQ(baseline, wide);
#else
    GTEST_SKIP() << "AVX-512 not compiled in";
#endif
}

// The conversion is now derived from the resolved backend rather than from a
// second CPU probe, so the two cannot disagree. This is the test that says so:
// whatever backend the kernels picked, the converter must be that backend's
// variant -- a scalar-pinned run converting sixteen lanes at a time would be
// measuring something nobody asked for.
TEST(BulkGenerateFloat, ConversionFollowsTheResolvedBackend) {
    const detail::float_convert_fn fn = detail::resolve_float_convert();

    switch (active_backend()) {
#if VPHILOX_HAS_AVX512
        case backend::avx512:
            EXPECT_EQ(fn, &detail::to_float01_n_avx512);
            break;
#endif
#if VPHILOX_HAS_AVX2
        case backend::avx2:
            EXPECT_EQ(fn, &detail::to_float01_n_avx2);
            break;
#endif
        // NEON lands here on purpose: it is baseline on aarch64, so the
        // baseline loop already vectorises and there is no clone to select.
        default:
            EXPECT_EQ(fn, &detail::to_float01_n_baseline);
            break;
    }
}
