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

#include "vphilox/detail/float_bulk.hpp"
#include "vphilox/vphilox.hpp"

#include "bench_cycles.hpp"

namespace {

constexpr std::size_t kCount = 1u << 16;

using vphilox_bench::cycle_counter;

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

// ---------------------------------------------------------------------------
// Issue #34: the conversion loop on its own, at all three widths.
//
// BM_float_bulk above measures generate-and-convert together, so a change to
// the conversion width is diluted by the kernel it shares the loop with. These
// rows call the variants directly over a buffer that is already full, which is
// the only way to see what the width is worth. Every variant produces
// bit-identical floats -- tests/test_bulk_generate.cpp pins that -- so this is
// purely a speed question.
//
// The variants are called by name rather than through resolve_float_convert(),
// on purpose: dispatch picks one per process, and the point here is to compare
// them on the same CPU in the same run.
// ---------------------------------------------------------------------------

// The working set is the second axis, and it is the one that decides the
// answer. Conversion reads four bytes and writes four for every element, so a
// wider register only helps while the loads and stores are not themselves the
// limit. 2048 words keeps source and destination inside L1; 65536 puts them in
// L2; 4M is past every cache on the hosts measured here.
constexpr std::size_t kL1Words   = 2048;
constexpr std::size_t kL2Words   = 65536;
constexpr std::size_t kDramWords = 4u << 20;

void report_convert(benchmark::State& state, const cycle_counter& counter, std::size_t words) {
    const auto bytes = static_cast<std::int64_t>(state.iterations() * words * 4);
    state.SetBytesProcessed(bytes);
    const double cycles = counter.total();
    if (cycles > 0.0 && bytes > 0) {
        state.counters["cycles_per_byte"] = cycles / static_cast<double>(bytes);
        state.SetLabel(std::string{"cycle_source="} + counter.source());
    }
}

std::vector<vphilox::u32> filled_source(std::size_t words) {
    std::vector<vphilox::u32> src(words);
    vphilox::engine g{0xC0FFEEull};
    g.generate(std::span<vphilox::u32>{src});
    return src;
}

/// The variant arrives as a runtime pointer, and DoNotOptimize keeps it opaque,
/// so none of the three is inlined into this loop.
///
/// That matters more than it looks. Only the baseline lacks a target attribute,
/// and GCC will not inline an attributed function into an unattributed caller —
/// so a template parameter here would inline the baseline into a -O3 benchmark
/// TU while leaving the AVX2 and AVX-512 variants as real calls, and the
/// comparison would be measuring the calling convention. `generate_n` reaches
/// all three through resolve_float_convert()'s function pointer, and this
/// matches that.
void run_convert(benchmark::State& state, vphilox::detail::float_convert_fn convert) {
    const auto words                    = static_cast<std::size_t>(state.range(0));
    const std::vector<vphilox::u32> src = filled_source(words);
    std::vector<float> out(words);
    benchmark::DoNotOptimize(convert);
    cycle_counter cycles;
    for (auto _ : state) {
        cycles.start();
        convert(src.data(), out.data(), words);
        cycles.stop();
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    report_convert(state, cycles, words);
}

void add_sizes(benchmark::internal::Benchmark* b) {
    for (std::size_t w : {kL1Words, kL2Words, kDramWords}) b->Arg(static_cast<std::int64_t>(w));
}

void BM_convert_baseline(benchmark::State& state) {
    run_convert(state, &vphilox::detail::to_float01_n_baseline);
}
BENCHMARK(BM_convert_baseline)->Apply(add_sizes);

#if VPHILOX_HAS_AVX2
void BM_convert_avx2(benchmark::State& state) {
    if (!vphilox::detail::detect_cpu().avx2) {
        state.SkipWithError("no AVX2 on this CPU");
        return;
    }
    run_convert(state, &vphilox::detail::to_float01_n_avx2);
}
BENCHMARK(BM_convert_avx2)->Apply(add_sizes);
#endif

#if VPHILOX_HAS_AVX512
void BM_convert_avx512(benchmark::State& state) {
    if (!vphilox::detail::detect_cpu().avx512) {
        state.SkipWithError("no AVX-512 on this CPU");
        return;
    }
    run_convert(state, &vphilox::detail::to_float01_n_avx512);
}
BENCHMARK(BM_convert_avx512)->Apply(add_sizes);
#endif

}  // namespace

BENCHMARK_MAIN();
