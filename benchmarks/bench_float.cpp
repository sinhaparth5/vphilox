// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 3: mantissa injection vs the division-based conversion.
//
// Target from the docs: 3 cycles for vpor + vsubps against 10-14 for vdivps,
// a ~4.3x latency reduction. What this benchmark measures is throughput of
// the whole generate-and-convert loop, so the ratio will be smaller than the
// raw latency ratio -- the generator work is shared.

#include <benchmark/benchmark.h>

#include <random>
#include <span>
#include <vector>

#include "vphilox/vphilox.hpp"

namespace {

constexpr std::size_t kCount = 1u << 16;

void BM_float_mantissa_injection(benchmark::State& state) {
    vphilox::engine g{1};
    std::vector<float> out(kCount);
    for (auto _ : state) {
        for (std::size_t i = 0; i < kCount; ++i) out[i] = g.next_float();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
}
BENCHMARK(BM_float_mantissa_injection);

void BM_float_std_uniform_real(benchmark::State& state) {
    vphilox::engine g{1};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> out(kCount);
    for (auto _ : state) {
        for (std::size_t i = 0; i < kCount; ++i) out[i] = dist(g);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
}
BENCHMARK(BM_float_std_uniform_real);

void BM_float_naive_division(benchmark::State& state) {
    vphilox::engine g{1};
    std::vector<float> out(kCount);
    for (auto _ : state) {
        for (std::size_t i = 0; i < kCount; ++i) {
            out[i] = static_cast<float>(g()) / 4294967296.0f;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
}
BENCHMARK(BM_float_naive_division);

void BM_double_mantissa_injection(benchmark::State& state) {
    vphilox::engine g{1};
    std::vector<double> out(kCount);
    for (auto _ : state) {
        for (std::size_t i = 0; i < kCount; ++i) out[i] = g.next_double();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
}
BENCHMARK(BM_double_mantissa_injection);

// ---------------------------------------------------------------------------
// Issue #34: the bulk float path against the word-at-a-time one, with the raw
// integer bulk path as the ceiling. The gap between the last two is what the
// conversion pass costs -- the part that only fusing conversion into the
// kernels themselves could remove.
// ---------------------------------------------------------------------------

void BM_float_bulk(benchmark::State& state) {
    vphilox::engine g{1};
    std::vector<float> out(kCount);
    for (auto _ : state) {
        g.generate(std::span<float>{out});
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kCount * 4));
}
BENCHMARK(BM_float_bulk);

void BM_u32_bulk_ceiling(benchmark::State& state) {
    vphilox::engine g{1};
    std::vector<vphilox::u32> out(kCount);
    for (auto _ : state) {
        g.generate(std::span<vphilox::u32>{out});
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kCount));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kCount * 4));
}
BENCHMARK(BM_u32_bulk_ceiling);

}  // namespace

BENCHMARK_MAIN();
