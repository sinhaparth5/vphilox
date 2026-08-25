// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// vphilox_curand_parity: does vphilox produce the same stream as NVIDIA cuRAND?
//
// Issue #54's parity claim was discharged across x86-64 (scalar, AVX2,
// AVX-512) and aarch64 (NEON) by `tests/test_cross_platform_parity.cpp`. That
// covers this library's own kernels. This tool answers a different question:
// does vphilox agree with somebody *else's* Philox4x32-10?
//
// `tests/test_reference_vectors.cpp` already pins the core function against the
// published Salmon et al. vectors, so agreement on the algorithm is not really
// in doubt. What is worth checking is the layer above it, which the KAT vectors
// say nothing about: cuRAND seeds with (seed, subsequence, offset) and vphilox
// with (key, counter). Implementations of Philox agree on the round function
// and disagree on exactly this mapping. For a library whose entire reason for
// existing is portable, reproducible state, "you get the same stream moving
// between cuRAND and vphilox" is a claim worth either establishing or
// explicitly disclaiming.
//
// Two independent things are checked:
//
//   1. CORE. cuRAND reports its own (counter, key) after seeding. Feed exactly
//      those into vphilox and the word streams must be identical. This tests
//      the round function and the counter increment order with no guesswork
//      about seeding at all.
//
//   2. MAPPING. The (counter, key) cuRAND derived must match what this file
//      predicts from (seed, subsequence, offset). This is the interop claim,
//      and it is the half that could plausibly fail.
//
// ---------------------------------------------------------------------------
// Why there is no CUDA here, and why that is not a weaker test
//
// cuRAND's Philox header guards its function decoration with
// `#if !defined(QUALIFIERS)`, so defining QUALIFIERS before including it
// compiles NVIDIA's own reference implementation as ordinary host C++. No nvcc,
// no GPU, no container — the tool needs the CUDA *headers* and nothing else,
// which is what makes it runnable anywhere rather than only on a GPU box.
//
// The arithmetic is the same either way by construction. The only target-
// dependent line in the whole generator is `mulhilo32`, which cuRAND writes as
// NV_IF_ELSE_TARGET(NV_IS_HOST, 64-bit multiply, __umulhi). Both branches
// compute the high 32 bits of a 32x32 product; the difference is instruction
// selection, not algorithm. So this checks NVIDIA's reference implementation
// and not NVIDIA's device code generation, and the gap between those two is a
// hypothetical `__umulhi` bug rather than anything about Philox.
//
// Build: only exists when the cuRAND headers are found. See tools/CMakeLists.txt.

// Must precede the cuRAND include: it is what turns __device__ code into
// host code. See the note above.
#define QUALIFIERS static inline

#include <curand_kernel.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "vphilox/vphilox.hpp"

namespace {

struct case_spec {
    const char* what;
    unsigned long long seed;
    unsigned long long subseq;
    unsigned long long offset;
};

// Chosen to exercise the seeding arithmetic rather than just the round
// function: offsets that are not block-aligned land mid-block and shift the
// word index, and values straddling 2^32 force a carry between the counter's
// low and high words, which is where a mapping bug would actually live.
constexpr case_spec cases[] = {
    {"origin", 0, 0, 0},
    {"plain seed", 0xDEADBEEFu, 0, 0},
    {"64-bit seed", 0x0123456789ABCDEFull, 0, 0},
    {"all-ones seed", ~0ull, 0, 0},
    {"offset 1 (mid-block)", 12345, 0, 1},
    {"offset 2 (mid-block)", 12345, 0, 2},
    {"offset 3 (mid-block)", 12345, 0, 3},
    {"offset 4 (next block)", 12345, 0, 4},
    {"offset 7 (mid-block)", 12345, 0, 7},
    {"subsequence 1", 99, 1, 0},
    {"subsequence 2^32", 99, 1ull << 32, 0},
    {"subsequence max", 99, ~0ull, 0},
    {"offset across 2^32", 7, 0, 1ull << 34},
    {"offset carry boundary", 7, 0, (1ull << 34) - 4},
    {"both set", 0xFEEDFACECAFEBEEFull, 0x1234, 4096},
    {"both large", 0xA5A5A5A5A5A5A5A5ull, 0xFFFFFFFFull, (1ull << 40) + 9},
};

constexpr std::size_t words_per_case = 4096;

vphilox::counter4 predicted_counter(unsigned long long subseq, unsigned long long offset) {
    // curand_init zeroes the counter, adds `subsequence` to its high 64 bits,
    // then adds `offset / 4` to the low 64 bits and keeps `offset % 4` as a
    // word index within the block. vphilox's counter4 is little-endian by word,
    // so the low 64 bits are words 0-1 and the high 64 bits are words 2-3.
    const unsigned long long lo = offset / 4;
    return vphilox::counter4{{static_cast<vphilox::u32>(lo), static_cast<vphilox::u32>(lo >> 32),
                              static_cast<vphilox::u32>(subseq),
                              static_cast<vphilox::u32>(subseq >> 32)}};
}

vphilox::key2 predicted_key(unsigned long long seed) {
    return vphilox::key2{{static_cast<vphilox::u32>(seed), static_cast<vphilox::u32>(seed >> 32)}};
}

std::string hex_counter(const vphilox::counter4& c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%08x:%08x:%08x:%08x", c.v[3], c.v[2], c.v[1], c.v[0]);
    return buf;
}

}  // namespace

int main() {
    std::printf("# vphilox <-> cuRAND Philox4_32_10 parity\n");
    std::printf("# vphilox version : %s\n", VPHILOX_VERSION_STRING);
    std::printf("# vphilox backend : %s\n", vphilox::backend_name(vphilox::active_backend()));
#ifdef CURAND_VERSION
    std::printf("# curand headers  : %d\n", CURAND_VERSION);
#endif
    std::printf("# curand build    : host (QUALIFIERS overridden; no device code)\n");
    std::printf("# words per case  : %zu\n", words_per_case);
    std::printf("#\n");

    std::vector<unsigned int> ref(words_per_case);
    std::vector<vphilox::u32> ours;

    std::size_t core_failures    = 0;
    std::size_t mapping_failures = 0;

    for (const auto& c : cases) {
        curandStatePhilox4_32_10_t st;
        curand_init(c.seed, c.subseq, c.offset, &st);

        // Captured before a single word is drawn, so it describes the state
        // that produces ref[0].
        const vphilox::counter4 curand_ctr{{st.ctr.x, st.ctr.y, st.ctr.z, st.ctr.w}};
        const vphilox::key2 curand_key{{st.key.x, st.key.y}};
        const unsigned int word_index = st.STATE;

        // Word at a time rather than curand4(). curand4() re-splices its return
        // value when the generator sits mid-block, so drawing single words is
        // the form that behaves identically for aligned and unaligned offsets.
        for (std::size_t i = 0; i < words_per_case; ++i) {
            ref[i] = curand(&st);
        }

        // --- check 1: the core, using cuRAND's own reported state ---
        //
        // vphilox starts at word 0 of the block; cuRAND starts at `word_index`
        // of the same block, so generate the extra words and skip them rather
        // than adjusting the counter, which would be the easy way to write a
        // test that agrees with itself.
        ours.assign(words_per_case + word_index, 0);
        vphilox::engine eng(curand_key, curand_ctr);
        eng.generate_n(ours.data(), ours.size());

        std::size_t mismatch = words_per_case;
        for (std::size_t i = 0; i < words_per_case; ++i) {
            if (ours[i + word_index] != ref[i]) {
                mismatch = i;
                break;
            }
        }

        // --- check 2: the seeding mapping this file documents ---
        const bool mapping_ok = predicted_counter(c.subseq, c.offset) == curand_ctr &&
                                predicted_key(c.seed) == curand_key &&
                                word_index == (c.offset & 3u);

        const bool core_ok = mismatch == words_per_case;
        core_failures += core_ok ? 0 : 1;
        mapping_failures += mapping_ok ? 0 : 1;

        std::printf("%-24s core %-4s  mapping %-4s  ctr %s  key %08x:%08x  w%u\n", c.what,
                    core_ok ? "OK" : "FAIL", mapping_ok ? "OK" : "FAIL",
                    hex_counter(curand_ctr).c_str(), curand_key.v[1], curand_key.v[0], word_index);

        if (!core_ok) {
            std::printf("    first mismatch at word %zu: vphilox %08x, cuRAND %08x\n", mismatch,
                        ours[mismatch + word_index], ref[mismatch]);
        }
        if (!mapping_ok) {
            std::printf("    predicted ctr %s, key %08x:%08x, w%llu\n",
                        hex_counter(predicted_counter(c.subseq, c.offset)).c_str(),
                        predicted_key(c.seed).v[1], predicted_key(c.seed).v[0], c.offset & 3ull);
        }
    }

    const std::size_t n = sizeof(cases) / sizeof(cases[0]);
    std::printf("#\n");
    std::printf("# core    : %zu/%zu cases identical over %zu words each\n", n - core_failures, n,
                words_per_case);
    std::printf("# mapping : %zu/%zu cases match the documented derivation\n", n - mapping_failures,
                n);

    if (core_failures == 0 && mapping_failures == 0) {
        std::printf("# VERDICT : IDENTICAL\n");
        return 0;
    }
    std::printf("# VERDICT : MISMATCH\n");
    return 1;
}
