// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1 baseline / Phase 4 comparison matrix.
//
// The number that matters is cycles-per-byte, not wall time -- it is the only
// figure comparable across machines and the one the docs quote (0.42 cpb
// target for AVX-512, ~4.5 cpb for scalar Philox, ~1.8 cpb for mt19937).
// Google Benchmark's bytes_per_second plus the reported CPU frequency gets
// you there; Phase 4 should add an rdtsc-based counter for a direct read.
//
// TODO(phase-4): add xoshiro256++ and PCG64 to complete the matrix. Both are
// small enough to vendor into benchmarks/third_party/ rather than take as
// dependencies.

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include "vphilox/vphilox.hpp"

namespace {

constexpr std::size_t kWords = 1u << 16;  // 256 KiB per iteration: fits L2,
                                          // so this measures the generator
                                          // rather than memory bandwidth.

void BM_vphilox(benchmark::State& state) {
    vphilox::engine g{0xDEADBEEFull};
    std::vector<vphilox::u32> out(kWords);

    for (auto _ : state) {
        for (std::size_t i = 0; i < kWords; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kWords * sizeof(vphilox::u32)));
    state.SetLabel(vphilox::backend_name(vphilox::engine::which_backend()));
}
BENCHMARK(BM_vphilox);

/// Raw kernel throughput, no buffering layer. The gap between this and
/// BM_vphilox is the cost of the ring buffer abstraction; Phase 3's
/// "zero-cost abstraction" claim is that the gap is negligible.
void BM_vphilox_bulk(benchmark::State& state) {
    const vphilox::key2 k{{0xDEADBEEFu, 0u}};
    vphilox::counter4 ctr{};
    std::vector<vphilox::u32> out(kWords);
    constexpr std::size_t blocks = kWords / vphilox::block_words;

    for (auto _ : state) {
        vphilox::detail::resolve_dispatch<vphilox::default_rounds>().fn(ctr, k, out.data(), blocks);
        vphilox::counter_add(ctr, blocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kWords * sizeof(vphilox::u32)));
}
BENCHMARK(BM_vphilox_bulk);

/// The baseline vphilox has to beat.
void BM_mt19937(benchmark::State& state) {
    std::mt19937 g{0xDEADBEEFu};
    std::vector<std::uint32_t> out(kWords);

    for (auto _ : state) {
        for (std::size_t i = 0; i < kWords; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kWords * sizeof(std::uint32_t)));
}
BENCHMARK(BM_mt19937);

/// Scalar Philox, forced. This is the ~10x-slower-than-mt19937 case the
/// project exists to fix; keeping it in the matrix is what makes the speedup
/// claim checkable rather than asserted.
void BM_philox_scalar(benchmark::State& state) {
    const vphilox::key2 k{{0xDEADBEEFu, 0u}};
    vphilox::counter4 ctr{};
    std::vector<vphilox::u32> out(kWords);
    constexpr std::size_t blocks = kWords / vphilox::block_words;

    for (auto _ : state) {
        vphilox::detail::kernel_scalar::generate<vphilox::default_rounds>(ctr, k, out.data(), blocks);
        vphilox::counter_add(ctr, blocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * kWords * sizeof(vphilox::u32)));
}
BENCHMARK(BM_philox_scalar);

}  // namespace

BENCHMARK_MAIN();
