// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1 baseline / Phase 4 comparison matrix.
//
// Report cycles-per-byte directly alongside wall-clock throughput. On x86 the
// fallback is a serialized RDTSC measurement. When Google Benchmark supplies a
// CYCLES performance counter, that takes precedence; use that path on ARM.
//
// TODO(phase-4): add xoshiro256++ and PCG64 to complete the matrix. Both are
// small enough to vendor into benchmarks/third_party/ rather than take as
// dependencies.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "vphilox/vphilox.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define VPHILOX_BENCH_HAS_RDTSC 1
#else
#define VPHILOX_BENCH_HAS_RDTSC 0
#endif

namespace {

constexpr std::size_t kWords = 1u << 16;  // 256 KiB per iteration: fits L2,
                                          // so this measures the generator
                                          // rather than memory bandwidth.
constexpr std::size_t kBytesPerIteration = kWords * sizeof(vphilox::u32);

class cycle_counter {
public:
    void start() noexcept {
#if VPHILOX_BENCH_HAS_RDTSC
        _mm_lfence();
        start_ = __rdtsc();
        _mm_lfence();
#endif
    }

    void stop() noexcept {
#if VPHILOX_BENCH_HAS_RDTSC
        _mm_lfence();
        const std::uint64_t end = __rdtsc();
        _mm_lfence();
        total_ += end - start_;
#endif
    }

    [[nodiscard]] double total() const noexcept { return static_cast<double>(total_); }

private:
    std::uint64_t start_{};
    std::uint64_t total_{};
};

void report_metrics(benchmark::State& state, const cycle_counter& rdtsc,
                    std::string label = {}) {
    const auto bytes = static_cast<std::int64_t>(state.iterations() * kBytesPerIteration);
    state.SetBytesProcessed(bytes);

    double cycles = 0.0;
    std::string source;
    const auto perf_cycles = state.counters.find("CYCLES");
    if (perf_cycles != state.counters.end() && perf_cycles->second.value > 0.0) {
        cycles = perf_cycles->second.value;
        source = "perf";
    } else if (rdtsc.total() > 0.0) {
        cycles = rdtsc.total();
        source = "rdtsc";
    }

    if (cycles > 0.0 && bytes > 0) {
        state.counters["cycles_per_byte"] = cycles / static_cast<double>(bytes);
        if (!label.empty()) label += ", ";
        label += "cycle_source=" + source;
    }
    if (!label.empty()) state.SetLabel(label);
}

void BM_vphilox(benchmark::State& state) {
    vphilox::engine g{0xDEADBEEFull};
    std::vector<vphilox::u32> out(kWords);
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        for (std::size_t i = 0; i < kWords; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles, vphilox::backend_name(vphilox::engine::which_backend()));
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
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        vphilox::detail::resolve_dispatch<vphilox::default_rounds>().fn(ctr, k, out.data(), blocks);
        vphilox::counter_add(ctr, blocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
}
BENCHMARK(BM_vphilox_bulk);

/// The baseline vphilox has to beat.
void BM_mt19937(benchmark::State& state) {
    std::mt19937 g{0xDEADBEEFu};
    std::vector<std::uint32_t> out(kWords);
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        for (std::size_t i = 0; i < kWords; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
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
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        vphilox::detail::kernel_scalar::generate<vphilox::default_rounds>(
            ctr, k, out.data(), blocks);
        vphilox::counter_add(ctr, blocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
}
BENCHMARK(BM_philox_scalar);

}  // namespace

BENCHMARK_MAIN();
