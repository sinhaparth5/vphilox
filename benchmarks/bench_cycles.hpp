// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Shared cycle counting for the benchmarks. On x86 this is a serialized
// RDTSC; on Linux ARM it is perf_event_open reading hardware CPU cycles.
//
// One instance measures one thread. perf_event_open with pid=0 attaches to the
// *calling* thread, so a multi-threaded benchmark must construct a counter
// inside each worker rather than sharing one -- see bench_scaling.cpp, where
// the per-thread totals are summed to give aggregate cycles per byte.

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

#if defined(__linux__) && !VPHILOX_BENCH_HAS_RDTSC
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace vphilox_bench {

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
#if defined(__linux__) && !VPHILOX_BENCH_HAS_RDTSC
    int fd_{-1};
#endif
};

}  // namespace vphilox_bench

#endif  // VPHILOX_BENCH_CYCLES_HPP
