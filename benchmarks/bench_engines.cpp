// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Phase 1 baseline / Phase 4 comparison matrix.
//
// Report cycles-per-byte directly alongside wall-clock throughput. On x86 the
// fallback is a serialized RDTSC measurement. On Linux ARM, perf_event_open
// reads the hardware CPU-cycle counter directly.
//
// TODO(phase-4): add xoshiro256++ and PCG64 to complete the matrix. Both are
// small enough to vendor into benchmarks/third_party/ rather than take as
// dependencies.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#if defined(__linux__) && !defined(__x86_64__) && !defined(_M_X64)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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
#if defined(__linux__) && !VPHILOX_BENCH_HAS_RDTSC
    cycle_counter() noexcept {
        perf_event_attr attr{};
        attr.type           = PERF_TYPE_HARDWARE;
        attr.size           = static_cast<decltype(attr.size)>(sizeof(attr));
        attr.config         = PERF_COUNT_HW_CPU_CYCLES;
        attr.disabled       = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        fd_                 = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
    }

    ~cycle_counter() {
        if (fd_ >= 0) close(fd_);
    }

    cycle_counter(const cycle_counter&)            = delete;
    cycle_counter& operator=(const cycle_counter&) = delete;
#endif

    void start() noexcept {
#if VPHILOX_BENCH_HAS_RDTSC
        _mm_lfence();
        start_ = __rdtsc();
        _mm_lfence();
#elif defined(__linux__)
        if (fd_ >= 0) {
            ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
        }
#endif
    }

    void stop() noexcept {
#if VPHILOX_BENCH_HAS_RDTSC
        _mm_lfence();
        const std::uint64_t end = __rdtsc();
        _mm_lfence();
        total_ += end - start_;
#elif defined(__linux__)
        if (fd_ >= 0) {
            ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
            std::uint64_t measured{};
            if (read(fd_, &measured, sizeof(measured)) == static_cast<ssize_t>(sizeof(measured))) {
                total_ += measured;
            }
        }
#endif
    }

    [[nodiscard]] double total() const noexcept { return static_cast<double>(total_); }

    [[nodiscard]] const char* source() const noexcept {
#if VPHILOX_BENCH_HAS_RDTSC
        return "rdtsc";
#elif defined(__linux__)
        return fd_ >= 0 ? "perf_event" : "";
#else
        return "";
#endif
    }

private:
    std::uint64_t start_{};
    std::uint64_t total_{};
#if defined(__linux__) && !VPHILOX_BENCH_HAS_RDTSC
    int fd_{-1};
#endif
};

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

}  // namespace

BENCHMARK_MAIN();
