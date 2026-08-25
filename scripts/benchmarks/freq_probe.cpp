// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha
//
// Not part of the build, and deliberately so: it is x86-only inline asm plus
// Linux thread affinity, and wiring that into CMake would put a target in CI
// that cannot compile on two of the four platforms this project supports.
// Build it by hand next to a configured tree, which is where version.hpp is
// generated:
//
//   cmake --preset bench
//   g++ -O2 -std=c++20 -I include -I build/bench/generated -pthread \
//       scripts/benchmarks/freq_probe.cpp -o freq_probe
//   ./freq_probe                      # AVX-512 load, if the CPU has it
//   VPHILOX_BACKEND=avx2 ./freq_probe
//
// Results are archived in results/<tag>-frequency-probe.txt and written up in
// docs/benchmarks/avx512-cascade-lake-2026-08-25.md.
//
// Written for issue #51 and reused for #27.
//
// bench_scaling reports aggregate cycles/byte from RDTSC, which counts
// *reference* cycles at the invariant nominal rate. If the core clock drops as
// threads are added, that lands in the column and looks exactly like
// contention. This separates the two: a dependent add chain of known length
// executes one add per core cycle, so timing it with a wall clock recovers the
// real core frequency, and timing it with RDTSC recovers the reference
// frequency. The ratio is the turbo state.
//
// The probe runs while N background threads hammer the AVX-512 kernel, which
// is what puts the socket into whatever frequency licence it ends up in.
//
// Every thread gets its own logical CPU. Without that the probe time-shares a
// core with a load worker and reports 1/N of the clock -- which looks exactly
// like a catastrophic downclock and is nothing of the kind.

#include <pthread.h>
#include <sched.h>
#include <x86intrin.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "vphilox/vphilox.hpp"

// Eight dependent adds per iteration. Each has one-cycle latency and each
// depends on the last, so the chain cannot be reordered or widened: the loop
// costs 8 core cycles per iteration regardless of how many ports are free.
// The sub/jnz issue alongside it and do not extend the chain.
static uint64_t dep_chain(uint64_t n) {
    uint64_t a = 0;
    asm volatile(
        "1:\n\t"
        "addq $1, %[a]\n\t addq $1, %[a]\n\t addq $1, %[a]\n\t addq $1, %[a]\n\t"
        "addq $1, %[a]\n\t addq $1, %[a]\n\t addq $1, %[a]\n\t addq $1, %[a]\n\t"
        "subq $1, %[n]\n\t"
        "jnz 1b\n\t"
        : [a] "+r"(a), [n] "+r"(n)
        :
        : "cc");
    return a;
}

static std::atomic<bool> g_stop{false};
static std::atomic<uint64_t> g_sink{0};

static void pin_to(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void load_worker(uint64_t seed, int cpu) {
    pin_to(cpu);
    vphilox::engine g(seed);
    std::vector<uint32_t> buf(65536);  // 256 KiB, L2-resident
    uint64_t acc = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        g.generate_n(buf.data(), buf.size());
        acc += buf[0];
    }
    g_sink.fetch_add(acc, std::memory_order_relaxed);
}

// CPU 2 is the probe's. Load workers take the rest, filling node 0's physical
// cores first, then node 1's, then the hyperthread siblings -- the same order
// the scheduler would use, but deterministic.
static const int kProbeCpu = 2;

static std::vector<int> load_cpus(int n) {
    std::vector<int> order;
    for (int c = 0; c < 32; ++c)
        if (c != kProbeCpu) order.push_back(c);
    order.resize(n);
    return order;
}

static void measure(int load_threads) {
    g_stop.store(false);
    std::vector<std::thread> pool;
    std::vector<int> cpus = load_cpus(load_threads);
    for (int i = 0; i < load_threads; ++i) pool.emplace_back(load_worker, 1000 + i, cpus[i]);

    // Let the socket settle into its licence before sampling.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    constexpr uint64_t kIters = 40'000'000;  // 320M core cycles, ~0.1 s
    constexpr double kCycles  = 8.0 * kIters;

    double best_core = 0, best_ref = 0;
    for (int rep = 0; rep < 5; ++rep) {
        auto t0     = std::chrono::steady_clock::now();
        uint64_t c0 = __rdtsc();
        uint64_t r  = dep_chain(kIters);
        uint64_t c1 = __rdtsc();
        auto t1     = std::chrono::steady_clock::now();
        g_sink.fetch_add(r, std::memory_order_relaxed);

        double secs = std::chrono::duration<double>(t1 - t0).count();
        double core = kCycles / secs / 1e9;
        double ref  = double(c1 - c0) / secs / 1e9;
        // Take the fastest sample: interference can only slow the chain down.
        if (core > best_core) {
            best_core = core;
            best_ref  = ref;
        }
    }

    g_stop.store(true);
    for (auto& t : pool) t.join();

    printf("%8d %14.3f %14.3f %12.3f\n", load_threads + 1, best_core, best_ref,
           best_core / best_ref);
    fflush(stdout);
}

int main() {
    pin_to(kProbeCpu);
    printf("backend: %s\n\n", vphilox::backend_name(vphilox::active_backend()));
    printf("%8s %14s %14s %12s\n", "active", "core GHz", "ref GHz", "core/ref");
    for (int n : {0, 3, 7, 15, 31}) measure(n);
    return 0;
}
