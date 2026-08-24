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
//
// Two axes, because with one the result is not interpretable. range(0) is the
// thread count; range(1) is the words each worker fills. A 256 KiB working set
// stays L2-resident and measures the generator; 4 MiB per worker times eight
// threads is 32 MiB against an 8 MiB L3, which measures the memory system. Run
// both and the gap between the curves *is* the bandwidth story, rather than
// one curve that silently conflates the two.
//
// The headline metric is aggregate cycles per byte: every thread's own cycles
// summed, divided by all the bytes produced. Under perfect scaling that stays
// flat as threads are added, because twice the threads spend twice the cycles
// producing twice the output. A rise is the knee, and it is visible without
// having to compare rows by eye.
//
// Two things to know before reading a run of this:
//
// Workers are spawned and joined every iteration, and the cycle counters wrap
// only the fill, so `cycles_per_byte` excludes thread creation while
// `bytes_per_second` includes it. At 256 KiB per worker the spawn is a large
// fraction of the iteration, which is why the GB/s column can *rise* with the
// working set. Compare thread counts on cycles/byte; treat GB/s here as
// indicative only. A persistent pool would fix this and is worth doing before
// any of these numbers are published.
//
// On x86 the counter is RDTSC, which counts reference cycles rather than core
// cycles. Adding threads moves the turbo ceiling, so on a machine without a
// pinned clock some of the movement in cycles/byte is the frequency changing
// underneath rather than contention. Run this under the performance governor.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "vphilox/vphilox.hpp"

#include "bench_cycles.hpp"

#if VPHILOX_HAVE_OPENMP
#include <omp.h>
#endif

namespace {

using vphilox_bench::cycle_counter;

/// One counter per worker, each on its own cache line. The write happens once
/// per worker per iteration rather than in the fill loop, but padding it costs
/// nothing and keeps the accounting clear of the false sharing this benchmark
/// exists to rule out.
struct alignas(64) padded_cycles {
    std::uint64_t value{};
};

enum class fill_mode { per_call, bulk };

template <fill_mode Mode>
void run_scaling(benchmark::State& state) {
    const auto threads = static_cast<std::size_t>(state.range(0));
    const auto words   = static_cast<std::size_t>(state.range(1));

    // Disjoint counter ranges, one key. Each worker starts where the previous
    // worker's range ends, so the streams provably never overlap.
    const vphilox::key2 key{{0xDEADBEEFu, 0u}};

    std::vector<std::unique_ptr<std::vector<vphilox::u32>>> buffers;
    buffers.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) {
        buffers.push_back(std::make_unique<std::vector<vphilox::u32>>(words));
    }

    std::vector<padded_cycles> thread_cycles(threads);
    const char* cycle_source = "";

    for (auto _ : state) {
        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (std::size_t t = 0; t < threads; ++t) {
            workers.emplace_back([&, t] {
                const auto start = vphilox::counter_advanced(
                    vphilox::counter4{},
                    static_cast<vphilox::u64>(t) * (words / vphilox::block_words));

                vphilox::engine g{key, start};
                auto& buf = *buffers[t];

                // perf_event_open attaches to the calling thread, so the
                // counter has to be constructed here rather than shared.
                cycle_counter cycles;
                cycles.start();
                if constexpr (Mode == fill_mode::bulk) {
                    g.generate_n(buf.data(), words);
                } else {
                    for (std::size_t i = 0; i < words; ++i) buf[i] = g();
                }
                cycles.stop();

                benchmark::DoNotOptimize(buf.data());
                thread_cycles[t].value += cycles.raw();
                if (t == 0) cycle_source = cycles.source();
            });
        }
        for (auto& w : workers) w.join();
        benchmark::ClobberMemory();
    }

    const auto bytes =
        static_cast<std::int64_t>(state.iterations() * threads * words * sizeof(vphilox::u32));
    state.SetBytesProcessed(bytes);

    std::uint64_t total_cycles = 0;
    for (const auto& c : thread_cycles) total_cycles += c.value;

    std::string label = vphilox::backend_name(vphilox::engine::which_backend());
    if (total_cycles > 0 && bytes > 0) {
        // Aggregate: all threads' cycles over all threads' bytes. Flat across
        // the thread axis means linear scaling.
        state.counters["cycles_per_byte"] =
            static_cast<double>(total_cycles) / static_cast<double>(bytes);
        label += std::string{", cycle_source="} + cycle_source;
    }
    state.counters["kib_per_thread"] = static_cast<double>(words * sizeof(vphilox::u32)) / 1024.0;
    state.SetLabel(label);
}

/// The buffered `operator()` path -- one word at a time, through the refill
/// buffer.
void BM_thread_scaling(benchmark::State& state) {
    run_scaling<fill_mode::per_call>(state);
}

/// The bulk path. On a single thread this is worth 25% on ARM and 42% on AVX2
/// (#37); the open question is whether that advantage survives contention or
/// whether the memory system takes it back.
void BM_thread_scaling_bulk(benchmark::State& state) {
    run_scaling<fill_mode::bulk>(state);
}

// 256 KiB per worker stays L2-resident; 4 MiB per worker does not.
constexpr std::int64_t kL2Words   = 1 << 16;
constexpr std::int64_t kDramWords = 1 << 20;

BENCHMARK(BM_thread_scaling)
    ->ArgsProduct({{1, 2, 4, 8, 16, 32}, {kL2Words, kDramWords}})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_thread_scaling_bulk)
    ->ArgsProduct({{1, 2, 4, 8, 16, 32}, {kL2Words, kDramWords}})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

// TODO(phase-4): OpenMP variant to compare against std::thread (#52), and
// instruction-cache miss rates via libpfm (#53), which Google Benchmark can
// collect with --benchmark_perf_counters.

}  // namespace

BENCHMARK_MAIN();
