// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1 baseline / Phase 4 comparison matrix.
//
// Report cycles-per-byte directly alongside wall-clock throughput. On x86 the
// fallback is a serialized RDTSC measurement. On Linux ARM, perf_event_open
// reads the hardware CPU-cycle counter directly.
//
// Every row generates the same number of *bytes*, not the same number of
// calls. xoshiro256++ and PCG64 return 64 bits per call and the rest return
// 32, so a per-call loop would hand the 64-bit generators twice the output
// for the same iteration count and flatter their cycles/byte by 2x. Equal
// bytes is what makes the column comparable at all.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "vphilox/vphilox.hpp"

#include "bench_cycles.hpp"
#include "third_party/pcg-cpp/pcg_random.hpp"
#include "third_party/xoshiro256plusplus.hpp"

namespace {

constexpr std::size_t kWords = 1u << 16;  // 256 KiB per iteration: fits L2,
                                          // so this measures the generator
                                          // rather than memory bandwidth.
constexpr std::size_t kBytesPerIteration = kWords * sizeof(vphilox::u32);

// The 64-bit generators fill the same 256 KiB in half as many calls.
static_assert(kWords % 2 == 0, "kWords must halve exactly for the 64-bit rows");
constexpr std::size_t kWords64 = kWords / 2;
static_assert(kWords64 * sizeof(std::uint64_t) == kBytesPerIteration,
              "every row in the matrix must generate the same number of bytes");

using vphilox_bench::cycle_counter;

void report_metrics(benchmark::State& state, const cycle_counter& rdtsc, std::string label = {}) {
    const auto bytes = static_cast<std::int64_t>(state.iterations() * kBytesPerIteration);
    state.SetBytesProcessed(bytes);

    const double cycles = rdtsc.total();

    if (cycles > 0.0 && bytes > 0) {
        state.counters["cycles_per_byte"] = cycles / static_cast<double>(bytes);
        if (!label.empty()) label += ", ";
        label += std::string{"cycle_source="} + rdtsc.source();
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

/// The bulk API (issue #37). Generates straight into the caller's buffer, so
/// the drain pass that BM_vphilox pays for disappears. The argument is the
/// chunk size in words: small chunks are the interesting case, because that is
/// where a naive implementation would hand the kernel a block count below its
/// SIMD width and end up in the scalar tail path.
void BM_vphilox_generate_n(benchmark::State& state) {
    const auto chunk = static_cast<std::size_t>(state.range(0));
    vphilox::engine g{0xDEADBEEFull};
    std::vector<vphilox::u32> out(kWords);
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        for (std::size_t i = 0; i < kWords; i += chunk) {
            g.generate_n(out.data() + i, std::min(chunk, kWords - i));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles, vphilox::backend_name(vphilox::engine::which_backend()));
}
BENCHMARK(BM_vphilox_generate_n)->Arg(8)->Arg(32)->Arg(256)->Arg(1024)->Arg(kWords);

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
        vphilox::detail::kernel_scalar::generate<vphilox::default_rounds>(ctr, k, out.data(),
                                                                          blocks);
        vphilox::counter_add(ctr, blocks);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
}
BENCHMARK(BM_philox_scalar);

/// xoshiro256++ 1.0 -- the speed benchmark people cite when they argue a
/// counter-based generator cannot compete. It is not counter-based: no
/// O(1) seek, no reproducible stream from an arbitrary (key, counter), and
/// no way to hand thread N its own substream without jump(). Losing to it on
/// cycles/byte would be unsurprising; the point of the row is to quantify
/// what those properties actually cost.
void BM_xoshiro256pp(benchmark::State& state) {
    vphilox_bench::xoshiro256plusplus g{0xDEADBEEFull};
    std::vector<std::uint64_t> out(kWords64);
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        for (std::size_t i = 0; i < kWords64; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
}
BENCHMARK(BM_xoshiro256pp);

/// PCG64 (setseq_xsl_rr_128_64). The closest thing in the matrix to vphilox's
/// own design goals -- statistically strong, seekable, multiple streams -- so
/// this is the row that measures the SIMD win rather than a difference in
/// what the generator promises.
void BM_pcg64(benchmark::State& state) {
    pcg64 g{0xDEADBEEFull};
    std::vector<std::uint64_t> out(kWords64);
    cycle_counter cycles;

    for (auto _ : state) {
        cycles.start();
        for (std::size_t i = 0; i < kWords64; ++i) out[i] = g();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
        cycles.stop();
    }
    report_metrics(state, cycles);
}
BENCHMARK(BM_pcg64);

}  // namespace

BENCHMARK_MAIN();
