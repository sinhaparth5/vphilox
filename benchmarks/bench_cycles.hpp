// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Shared cycle counting for the benchmarks. On x86 this is a serialized
// RDTSC; on Linux ARM it is perf_event_open reading hardware CPU cycles.
//
// One instance measures one thread. perf_event_open with pid=0 attaches to the
// *calling* thread, so a multi-threaded benchmark must construct a counter
// inside each worker rather than sharing one -- see bench_scaling.cpp, where
// the per-thread totals are summed to give aggregate cycles per byte.
//
// `perf_counter` below applies the same rule to retired instructions and L1
// instruction-cache misses (#53), which is what the instruction-supply half of
// the scaling study needs.

#ifndef VPHILOX_BENCH_CYCLES_HPP
#define VPHILOX_BENCH_CYCLES_HPP

#include <cstdint>

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

// perf_event_open backs the cycle counter on ARM and the instruction/i-cache
// counters everywhere, so the headers are needed on any Linux, not just the
// non-RDTSC targets.
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#define VPHILOX_BENCH_HAS_PERF 1
#else
#define VPHILOX_BENCH_HAS_PERF 0
#endif

namespace vphilox_bench {

class cycle_counter {
public:
#if VPHILOX_BENCH_HAS_PERF && !VPHILOX_BENCH_HAS_RDTSC
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

    /// Unrounded total, for summing across threads before dividing.
    [[nodiscard]] std::uint64_t raw() const noexcept { return total_; }

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
#if VPHILOX_BENCH_HAS_PERF && !VPHILOX_BENCH_HAS_RDTSC
    int fd_{-1};
#endif
};

/// One hardware perf event, opened on the calling thread.
///
/// This exists rather than Google Benchmark's `--benchmark_perf_counters` for
/// a specific reason. Those counters start and stop on the thread running the
/// benchmark loop (`benchmark.cc`, in `State::ResumeTiming`), and in a
/// pool-driven benchmark that thread is the dispatcher: on the std::thread
/// path it blocks on a condition variable while the workers do every
/// instruction, so the counters would report a near-empty thread; on the
/// OpenMP path the caller is the team master, so they would report one
/// worker's share and read like an aggregate. Per-worker counters, summed the
/// way `cycle_counter` already is, are the only shape that answers the
/// question being asked.
///
/// libpfm is deliberately not involved. The two events wanted here --
/// retired instructions and L1 instruction-cache read misses -- are both
/// generic `perf_event_open` events, so this needs no dependency beyond the
/// kernel headers already used for the ARM cycle counter.
class perf_counter {
public:
    enum class kind {
        instructions,   ///< PERF_COUNT_HW_INSTRUCTIONS, the MPKI denominator
        icache_misses,  ///< L1 instruction cache, read, miss
    };

    perf_counter()                               = default;
    perf_counter(const perf_counter&)            = delete;
    perf_counter& operator=(const perf_counter&) = delete;

#if VPHILOX_BENCH_HAS_PERF
    ~perf_counter() {
        if (fd_ >= 0) close(fd_);
    }

    /// Returns false if the kernel refused the event, which is normal rather
    /// than exceptional: a VM may expose no PMU at all, and
    /// perf_event_paranoid can forbid it. The caller reports nothing in that
    /// case instead of reporting a zero.
    bool open(kind k) noexcept {
        perf_event_attr attr{};
        attr.size           = static_cast<decltype(attr.size)>(sizeof(attr));
        attr.disabled       = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        if (k == kind::instructions) {
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_INSTRUCTIONS;
        } else {
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        }
        fd_ = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
        return fd_ >= 0;
    }

    void start() noexcept {
        if (fd_ < 0) return;
        ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
    }

    void stop() noexcept {
        if (fd_ < 0) return;
        ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
        std::uint64_t measured{};
        if (read(fd_, &measured, sizeof(measured)) == static_cast<ssize_t>(sizeof(measured))) {
            total_ += measured;
        }
    }

    [[nodiscard]] bool available() const noexcept { return fd_ >= 0; }
#else
    bool open(kind) noexcept { return false; }
    void start() noexcept {}
    void stop() noexcept {}
    [[nodiscard]] bool available() const noexcept { return false; }
#endif

    /// Unrounded total, for summing across threads before dividing.
    [[nodiscard]] std::uint64_t raw() const noexcept { return total_; }

private:
    std::uint64_t total_{};
#if VPHILOX_BENCH_HAS_PERF
    int fd_{-1};
#endif
};

}  // namespace vphilox_bench

#endif  // VPHILOX_BENCH_CYCLES_HPP
