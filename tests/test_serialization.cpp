// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Portable serialization of engine position.
//
// The library exists because std::mt19937's serialized state does not survive
// a change of standard library. These tests pin the three properties that
// failure taught: the round trip is exact, the text does not depend on the
// locale, and unrecognised input is refused rather than reinterpreted.

#include <gtest/gtest.h>

#include <cstdint>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "vphilox/serialize.hpp"
#include "vphilox/vphilox.hpp"

namespace {

using vphilox::engine;
using vphilox::engine_state;

/// A locale that groups digits, to prove the writer never consults one. If
/// anything here went through num_put, output would gain separators.
struct grouping_punct : std::numpunct<char> {
protected:
    char do_thousands_sep() const override { return '\''; }
    std::string do_grouping() const override { return "\3"; }
};

TEST(Serialization, RoundTripIsExactAtEveryOffset) {
    // Spanning several refills, so the cursor is mid-buffer, on a block
    // boundary, and at the "needs a refill" edge.
    for (std::size_t n = 0; n <= vphilox::refill_words * 3 + 7; ++n) {
        engine a{0xDEADBEEFull};
        for (std::size_t i = 0; i < n; ++i) (void)a();

        engine b{};
        b.set_state(a.state());

        for (int k = 0; k < 64; ++k) {
            ASSERT_EQ(a(), b()) << "after " << n << " draws, output " << k;
        }
    }
}

/// The bug this guards: the engine's own counter runs ahead of the caller's
/// position by whatever is still buffered, so the obvious reconstruction
/// loses outputs. state() must not have that flaw.
TEST(Serialization, KeyAndCounterAloneAreNotEnough) {
    engine a{1};
    for (int i = 0; i < 5; ++i) (void)a();

    engine naive{a.key(), a.counter()};
    engine restored{};
    restored.set_state(a.state());

    const auto expected = a();
    EXPECT_EQ(restored(), expected);
    EXPECT_NE(naive(), expected) << "if this ever matches, counter() changed meaning";
}

TEST(Serialization, TextRoundTripsThroughStringForm) {
    engine a{0xABCDEF};
    a.discard(12345);
    for (int i = 0; i < 3; ++i) (void)a();

    engine_state parsed{};
    ASSERT_TRUE(vphilox::from_string(vphilox::to_string(a.state()), parsed));

    engine b{};
    b.set_state(parsed);
    for (int k = 0; k < 32; ++k) ASSERT_EQ(a(), b());
}

/// The text describes a position, so two engines that reached the same point
/// by different routes serialize identically. That is what makes the format
/// independent of refill_blocks, which has already changed once.
TEST(Serialization, TextDependsOnPositionNotOnHowItWasReached) {
    engine stepped{7};
    for (int i = 0; i < 37; ++i) (void)stepped();

    engine jumped{7};
    jumped.discard(37);

    EXPECT_EQ(vphilox::to_string(stepped.state()), vphilox::to_string(jumped.state()));
}

TEST(Serialization, WrittenTextIsPlainDecimalFields) {
    engine a{1};
    const auto text = vphilox::to_string(a.state());
    EXPECT_EQ(text, "vphilox1 1 0 0 0 0 0 0");
}

TEST(Serialization, OutputIgnoresStreamLocale) {
    engine a{0xFFFFFFFFull};
    a.discard(1000000);

    std::ostringstream plain;
    plain << a;

    std::ostringstream grouped;
    grouped.imbue(std::locale(grouped.getloc(), new grouping_punct));
    grouped << a;

    EXPECT_EQ(plain.str(), grouped.str());
    EXPECT_EQ(plain.str().find('\''), std::string::npos) << "a locale reached the digits";
}

TEST(Serialization, OutputIgnoresStreamFormatFlags) {
    engine a{255};
    std::ostringstream dec, hex;
    hex << std::hex << std::uppercase;
    dec << a;
    hex << a;
    EXPECT_EQ(dec.str(), hex.str()) << "format flags changed the encoding, as they do for mt19937";
}

TEST(Serialization, StreamRoundTrip) {
    engine a{0x1234};
    for (int i = 0; i < 100; ++i) (void)a();

    std::stringstream ss;
    ss << a;

    engine b{};
    ss >> b;
    ASSERT_FALSE(ss.fail());

    for (int k = 0; k < 32; ++k) ASSERT_EQ(a(), b());
}

TEST(Serialization, MalformedInputIsRefused) {
    const char* bad[] = {
        "",
        "vphilox1",
        "vphilox1 1 2 3",                    // too few fields
        "vphilox1 1 2 3 4 5 6 0 9",          // too many
        "vphilox0 1 2 3 4 5 6 0",            // wrong version tag
        "mt19937 1 2 3 4 5 6 0",             // another format entirely
        "vphilox1 1 2 3 4 5 6 4",            // offset outside a block
        "vphilox1 4294967296 0 0 0 0 0 0",   // one past 2^32-1
        "vphilox1 99999999999 0 0 0 0 0 0",  // far past
        "vphilox1 1 2 3 4 5 6 x",            // not a number
        "vphilox1 -1 0 0 0 0 0 0",           // sign
        "vphilox1 1 2 3 4 5 6 0 ",           // trailing space
        "vphilox1  1 2 3 4 5 6 0",           // doubled separator
        "vphilox1 1 2 3 4 5 60",             // separator missing
        "vphilox1 1 2 3 4 5 6 0\n",          // trailing space
    };
    for (const char* text : bad) {
        engine_state st{};
        EXPECT_FALSE(vphilox::from_string(text, st)) << "accepted: '" << text << "'";
    }
}

TEST(Serialization, BadStreamInputSetsFailbitAndLeavesEngineAlone) {
    engine a{99};
    const auto before = vphilox::to_string(a.state());

    std::istringstream ss{"vphilox1 1 2 3 4 5 6 4"};  // offset outside a block
    ss >> a;

    EXPECT_TRUE(ss.fail());
    EXPECT_EQ(vphilox::to_string(a.state()), before) << "engine mutated on a failed read";
}

/// A 2^128 counter must survive the trip, since O(1) discard is what makes
/// deep positions reachable in the first place.
TEST(Serialization, SurvivesADeepPosition) {
    engine a{0x5EED};
    a.discard(vphilox::u64{1} << 40);

    std::stringstream ss;
    ss << a;
    engine b{};
    ss >> b;
    ASSERT_FALSE(ss.fail());

    for (int k = 0; k < 16; ++k) ASSERT_EQ(a(), b());
}

/// The portability claim, as a fixture rather than an argument.
///
/// The state text below was written on x86-64 and is checked into the
/// repository. Any machine that loads it must produce the same outputs --
/// which is precisely what a std::mt19937 checkpoint cannot promise, and the
/// reason this library exists. CI runs this on Linux, macOS arm64 and Windows
/// MSVC, so the claim is re-proved on three platforms and two instruction sets
/// on every commit rather than asserted in a README.
///
/// The position is deliberately awkward: past 2^33 outputs, mid-block (word 3
/// of 4), with a key using both words. A restore that rounded to a block
/// boundary or dropped the high counter word would fail here and pass on a
/// tidier fixture.
TEST(Serialization, StateWrittenOnAnotherMachineRestoresIdentically) {
    constexpr const char* kStateFromX86     = "vphilox1 608135816 2242054355 2147483649 0 0 0 3";
    constexpr std::uint64_t kExpectedDigest = 0xC85E29A172610C9Eull;

    engine_state st{};
    ASSERT_TRUE(vphilox::from_string(kStateFromX86, st));

    engine g{};
    g.set_state(st);

    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 4096; ++i) {
        const vphilox::u32 v = g();
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<unsigned char>((v >> (8 * b)) & 0xFFu);
            h *= 0x100000001b3ull;
        }
    }
    EXPECT_EQ(h, kExpectedDigest)
        << "a state written on one machine no longer restores to the same stream here";
}

/// And the text this machine writes for that position is byte-identical, so
/// the format round-trips in both directions rather than merely being
/// readable.
TEST(Serialization, ThisMachineWritesTheSameTextForThatPosition) {
    engine g{vphilox::key2{{0x243F6A88u, 0x85A308D3u}}, vphilox::counter4{}};
    g.discard((vphilox::u64{1} << 33) + 7);
    EXPECT_EQ(vphilox::to_string(g.state()), "vphilox1 608135816 2242054355 2147483649 0 0 0 3");
}

}  // namespace
