// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Decision benchmark for issue #12. This executable intentionally requires
// AVX2: it compares two implementation layouts, rather than runtime dispatch.

#include <benchmark/benchmark.h>

#include <immintrin.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "vphilox/detail/kernel_scalar.hpp"

#include "bench_main.hpp"

namespace {

using vphilox::counter4;
using vphilox::key2;
using vphilox::u32;
using vphilox::u64;

constexpr std::size_t kBlocks = 1u << 14;
constexpr std::size_t kBytes  = kBlocks * vphilox::block_words * sizeof(u32);
constexpr u64 kLow32          = 0x00000000FFFFFFFFull;

template <std::size_t Width>
std::array<counter4, Width> counters_from(const counter4& base) {
    std::array<counter4, Width> counters{};
    for (std::size_t i = 0; i < Width; ++i) counters[i] = vphilox::counter_advanced(base, i);
    return counters;
}

template <unsigned Rounds>
void generate4(counter4 base, key2 key, u32* out, std::size_t blocks) noexcept {
    constexpr auto M0  = static_cast<long long>(vphilox::philox_M0);
    constexpr auto M1  = static_cast<long long>(vphilox::philox_M1);
    const __m256i mask = _mm256_set1_epi64x(static_cast<long long>(kLow32));

    std::size_t block = 0;
    for (; block + 4 <= blocks; block += 4) {
        const auto c = counters_from<4>(base);
        __m256i x0   = _mm256_set_epi64x(c[3].v[0], c[2].v[0], c[1].v[0], c[0].v[0]);
        __m256i x1   = _mm256_set_epi64x(c[3].v[1], c[2].v[1], c[1].v[1], c[0].v[1]);
        __m256i x2   = _mm256_set_epi64x(c[3].v[2], c[2].v[2], c[1].v[2], c[0].v[2]);
        __m256i x3   = _mm256_set_epi64x(c[3].v[3], c[2].v[3], c[1].v[3], c[0].v[3]);
        u32 k0 = key.v[0], k1 = key.v[1];

        for (unsigned round = 0; round < Rounds; ++round) {
            const __m256i p0 = _mm256_mul_epu32(x0, _mm256_set1_epi64x(M0));
            const __m256i p1 = _mm256_mul_epu32(x2, _mm256_set1_epi64x(M1));
            const __m256i n0 = _mm256_xor_si256(_mm256_xor_si256(_mm256_srli_epi64(p1, 32), x1),
                                                _mm256_set1_epi64x(k0));
            const __m256i n2 = _mm256_xor_si256(_mm256_xor_si256(_mm256_srli_epi64(p0, 32), x3),
                                                _mm256_set1_epi64x(k1));
            x0               = n0;
            x1               = _mm256_and_si256(p1, mask);
            x2               = n2;
            x3               = _mm256_and_si256(p0, mask);
            k0 += vphilox::philox_W0;
            k1 += vphilox::philox_W1;
        }

        alignas(32) std::array<u64, 4> words[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[0].data()), x0);
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[1].data()), x1);
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[2].data()), x2);
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[3].data()), x3);
        for (std::size_t lane = 0; lane < 4; ++lane)
            for (std::size_t word = 0; word < 4; ++word)
                out[(block + lane) * 4 + word] = static_cast<u32>(words[word][lane]);
        vphilox::counter_add(base, 4);
    }
    vphilox::detail::kernel_scalar::generate<Rounds>(base, key, out + block * 4, blocks - block);
}

inline __m256i pack_lo(__m256i even, __m256i odd) noexcept {
    const __m256i low = _mm256_set1_epi64x(static_cast<long long>(kLow32));
    return _mm256_or_si256(_mm256_and_si256(even, low), _mm256_slli_epi64(odd, 32));
}

inline __m256i pack_hi(__m256i even, __m256i odd) noexcept {
    const __m256i high = _mm256_set1_epi64x(static_cast<long long>(~kLow32));
    return _mm256_or_si256(_mm256_srli_epi64(even, 32), _mm256_and_si256(odd, high));
}

template <unsigned Rounds>
void generate8(counter4 base, key2 key, u32* out, std::size_t blocks) noexcept {
    const __m256i m0 = _mm256_set1_epi32(static_cast<int>(vphilox::philox_M0));
    const __m256i m1 = _mm256_set1_epi32(static_cast<int>(vphilox::philox_M1));

    std::size_t block = 0;
    for (; block + 8 <= blocks; block += 8) {
        const auto c = counters_from<8>(base);
        alignas(32) u32 lanes[4][8];
        for (std::size_t lane = 0; lane < 8; ++lane)
            for (std::size_t word = 0; word < 4; ++word) lanes[word][lane] = c[lane].v[word];
        __m256i x0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(lanes[0]));
        __m256i x1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(lanes[1]));
        __m256i x2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(lanes[2]));
        __m256i x3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(lanes[3]));
        u32 k0 = key.v[0], k1 = key.v[1];

        for (unsigned round = 0; round < Rounds; ++round) {
            const __m256i p0e = _mm256_mul_epu32(x0, m0);
            const __m256i p0o = _mm256_mul_epu32(_mm256_srli_epi64(x0, 32), m0);
            const __m256i p1e = _mm256_mul_epu32(x2, m1);
            const __m256i p1o = _mm256_mul_epu32(_mm256_srli_epi64(x2, 32), m1);
            const __m256i n0 =
                _mm256_xor_si256(_mm256_xor_si256(pack_hi(p1e, p1o), x1), _mm256_set1_epi32(k0));
            const __m256i n2 =
                _mm256_xor_si256(_mm256_xor_si256(pack_hi(p0e, p0o), x3), _mm256_set1_epi32(k1));
            x0 = n0;
            x1 = pack_lo(p1e, p1o);
            x2 = n2;
            x3 = pack_lo(p0e, p0o);
            k0 += vphilox::philox_W0;
            k1 += vphilox::philox_W1;
        }

        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes[0]), x0);
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes[1]), x1);
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes[2]), x2);
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes[3]), x3);
        for (std::size_t lane = 0; lane < 8; ++lane)
            for (std::size_t word = 0; word < 4; ++word)
                out[(block + lane) * 4 + word] = lanes[word][lane];
        vphilox::counter_add(base, 8);
    }
    vphilox::detail::kernel_scalar::generate<Rounds>(base, key, out + block * 4, blocks - block);
}

template <auto Generate>
void verify() {
    constexpr counter4 base{{0xFFFFFFFFu, 0x12345678u, 0x9ABCDEF0u, 7u}};
    constexpr key2 key{{0xDEADBEEFu, 0xCAFEBABEu}};
    std::array<u32, 17 * 4> expected{}, actual{};
    vphilox::detail::kernel_scalar::generate(base, key, expected.data(), 17);
    Generate(base, key, actual.data(), 17);
    if (actual != expected) std::abort();
}

template <auto Generate>
void run(benchmark::State& state) {
    verify<Generate>();
    const key2 key{{0xDEADBEEFu, 0xCAFEBABEu}};
    counter4 base{};
    std::vector<u32> out(kBlocks * 4);
    for (auto _ : state) {
        Generate(base, key, out.data(), kBlocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        vphilox::counter_add(base, kBlocks);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kBytes));
}

void BM_layout4(benchmark::State& state) {
    run<generate4<vphilox::default_rounds>>(state);
}
void BM_layout8(benchmark::State& state) {
    run<generate8<vphilox::default_rounds>>(state);
}

BENCHMARK(BM_layout4);
BENCHMARK(BM_layout8);

}  // namespace

VPHILOX_BENCHMARK_MAIN();
