// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 4: multi-core scaling.
//
// The claim under test is linear scaling with zero synchronisation. Each
// worker gets its own engine over a disjoint counter range, so there is no
// shared state to contend on -- if this does not scale linearly, the problem
// is memory bandwidth or false sharing, not the algorithm.
//
// False sharing is the one real hazard: engines must not straddle a cache
// line, which is why the buffer is alignas(64) and why each worker's engine
// lives in its own heap allocation here rather than in a packed array.

#include <benchmark/benchmark.h>

#include <memory>
#include <thread>
#include <vector>

#include "vphilox/vphilox.hpp"

#if VPHILOX_HAVE_OPENMP
#include <omp.h>
#endif

namespace {

constexpr std::size_t kWordsPerThread = 1u << 20;  // 4 MiB per worker

void BM_thread_scaling(benchmark::State& state) {
    const auto threads = static_cast<std::size_t>(state.range(0));

    // Disjoint counter ranges, one key. Each worker starts where the previous
    // worker's range ends, so the streams provably never overlap.
    const vphilox::key2 key{{0xDEADBEEFu, 0u}};

    std::vector<std::unique_ptr<std::vector<vphilox::u32>>> buffers;
    buffers.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) {
        buffers.push_back(std::make_unique<std::vector<vphilox::u32>>(kWordsPerThread));
    }

    for (auto _ : state) {
        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (std::size_t t = 0; t < threads; ++t) {
            workers.emplace_back([&, t] {
                const auto start = vphilox::counter_advanced(
                    vphilox::counter4{},
                    static_cast<vphilox::u64>(t) * (kWordsPerThread / vphilox::block_words));

                vphilox::engine g{key, start};
                auto& buf = *buffers[t];
                for (std::size_t i = 0; i < kWordsPerThread; ++i) buf[i] = g();
                benchmark::DoNotOptimize(buf.data());
            });
        }
        for (auto& w : workers) w.join();
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * threads *
                                                      kWordsPerThread * sizeof(vphilox::u32)));
    state.SetLabel(vphilox::backend_name(vphilox::engine::which_backend()));
}
BENCHMARK(BM_thread_scaling)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

// TODO(phase-4): add an OpenMP variant to compare against std::thread, and
// record instruction-cache miss rates via libpfm (Google Benchmark's
// --benchmark_perf_counters).

}  // namespace

BENCHMARK_MAIN();
