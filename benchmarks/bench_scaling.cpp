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
// Two threading runtimes are measured, not one. The std::thread pool below is
// the library's own harness; the OpenMP variant is what a caller is far more
// likely to actually write, and #52 exists to check that the scaling result is
// a property of the generator rather than of this file's pool. Both are given
// persistent workers -- libgomp keeps its team alive between parallel regions
// exactly as the pool does -- so the comparison is the cost of the parallel
// construct, not of spawning threads.
//
// Workers live in a persistent pool rather than being spawned per iteration.
// With 256 KiB per worker a spawn-and-join costs a large fraction of the fill
// itself, which landed in `bytes_per_second` but not in `cycles_per_byte`
// (the counters wrap only the fill) and made the two columns tell different
// stories -- GB/s appeared to *improve* with a larger working set purely
// because the fixed spawn cost amortised better. With the pool, both columns
// measure the same work.
//
// Instruction supply (#53) is measured on demand, not on every run. Set
// VPHILOX_PERF_COUNTERS=1 and each worker also opens retired-instruction and
// L1 i-cache-miss counters on itself, and the row gains `icache_mpki` and
// `instructions_per_byte`. It is opt-in because the extra ioctl pair per
// worker per iteration lands in wall time and so in `bytes_per_second`, and
// the published scaling rows must not move because a diagnostic was added.
//
// The question it answers is narrow. #51 concluded the hyperthread knee is
// execution-port contention; the standing alternative is that two workers on
// one core thrash a shared instruction cache, since these kernels are large
// unrolled bodies. MPKI flat across the co-location boundary excludes that,
// and MPKI that climbs with it does not.
//
// One caveat remains. On x86 the counter is RDTSC, which counts reference
// cycles rather than core cycles. Adding threads moves the turbo ceiling, so
// on a machine without a pinned clock part of any movement in cycles/byte is
// the frequency changing underneath the measurement rather than contention.
// Run this under the performance governor.
//
// A second caveat, learned on a 16-core x86 host. This benchmark is unpinned
// by design -- pinning every worker to one core would measure a context-switch
// storm -- but that leaves thread *placement* to the scheduler, and above one
// thread per physical core the placement is most of the result. Two workers on
// one core's hyperthread siblings cost 35% each, so a curve that bends past
// eight threads on a 8C/16T socket is reporting where the threads landed as
// much as how the generator scales, which is also why the CV jumps there.
// Attributing a knee needs the placement fixed by hand; see
// docs/benchmarks/scaling-cascade-lake-2026-08-25.md for how that was done.

#include <benchmark/benchmark.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "vphilox/vphilox.hpp"

#include "bench_cycles.hpp"
#include "bench_main.hpp"

#if VPHILOX_HAVE_OPENMP
#include <omp.h>
#endif

namespace {

using vphilox_bench::cycle_counter;
using vphilox_bench::perf_counter;

/// Opt-in instruction-supply counters (#53). Read once: getenv in the worker
/// path would be a shared-state read inside the measured region.
bool perf_counters_wanted() {
    static const bool wanted = [] {
        const char* v = std::getenv("VPHILOX_PERF_COUNTERS");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return wanted;
}

/// A fixed set of workers that outlive the benchmark loop.
///
/// std::barrier would express this in a third of the lines, but libc++ gates
/// <barrier> behind a macOS availability macro and CI builds the benchmarks on
/// macOS arm64, so this uses the primitives that are unconditionally
/// available. The handshake costs two lock acquisitions per iteration, against
/// a fill of at least 256 KiB -- immaterial, and constant across thread counts,
/// which is the property that matters for a scaling curve.
class worker_pool {
public:
    explicit worker_pool(std::size_t threads) : threads_(threads) {
        workers_.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) workers_.emplace_back([this, t] { loop(t); });
    }

    ~worker_pool() {
        {
            const std::lock_guard lock(mutex_);
            stop_ = true;
            ++generation_;
        }
        start_.notify_all();
        for (auto& w : workers_) w.join();
    }

    worker_pool(const worker_pool&)            = delete;
    worker_pool& operator=(const worker_pool&) = delete;

    /// Run `job(t)` on every worker and block until all of them finish.
    void run(const std::function<void(std::size_t)>& job) {
        {
            const std::lock_guard lock(mutex_);
            job_       = &job;
            remaining_ = threads_;
            ++generation_;
        }
        start_.notify_all();

        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return remaining_ == 0; });
    }

private:
    void loop(std::size_t t) {
        std::uint64_t seen = 0;
        for (;;) {
            std::unique_lock lock(mutex_);
            start_.wait(lock, [this, &seen] { return generation_ != seen; });
            seen = generation_;
            if (stop_) return;

            const auto* job = job_;
            lock.unlock();

            (*job)(t);

            lock.lock();
            const bool last = (--remaining_ == 0);
            lock.unlock();
            if (last) done_.notify_one();
        }
    }

    std::size_t threads_;
    std::vector<std::thread> workers_;

    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;
    const std::function<void(std::size_t)>* job_{nullptr};
    std::uint64_t generation_{0};
    std::size_t remaining_{0};
    bool stop_{false};
};

enum class fill_mode { per_call, bulk };

/// Which threading runtime drives the workers. The job they run is identical,
/// so a difference between the two columns is the runtime's, not the kernel's.
enum class runtime_kind { std_thread, openmp };

template <fill_mode Mode, runtime_kind Runtime>
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

    // One counter per worker, opened on the worker itself: perf_event_open
    // attaches to the calling thread, so a counter built here would measure
    // the main thread and report nothing. Each accumulates across iterations.
    std::vector<std::unique_ptr<cycle_counter>> counters(threads);

    // ...which also means slot t only stays meaningful while slot t keeps the
    // same OS thread. The pool guarantees that by construction; OpenMP does
    // not promise a stable thread-number-to-thread mapping across parallel
    // regions, and if libgomp ever stopped providing one the counters would
    // silently measure the wrong threads rather than fail. Record the owner
    // and check it instead of trusting the runtime.
    std::vector<std::thread::id> owners(threads);
    std::atomic<bool> migrated{false};

    // Instruction supply, same per-worker ownership rule, same lifetime.
    const bool want_perf = perf_counters_wanted();
    std::vector<std::unique_ptr<perf_counter>> instrs(threads);
    std::vector<std::unique_ptr<perf_counter>> imisses(threads);

    const std::function<void(std::size_t)> job = [&](std::size_t t) {
        if (!counters[t]) {
            counters[t] = std::make_unique<cycle_counter>();
            owners[t]   = std::this_thread::get_id();
        } else if (owners[t] != std::this_thread::get_id()) {
            migrated.store(true, std::memory_order_relaxed);
        }
        auto& cycles = *counters[t];

        if (want_perf && !instrs[t]) {
            instrs[t]  = std::make_unique<perf_counter>();
            imisses[t] = std::make_unique<perf_counter>();
            instrs[t]->open(perf_counter::kind::instructions);
            imisses[t]->open(perf_counter::kind::icache_misses);
        }

        const auto start = vphilox::counter_advanced(
            vphilox::counter4{}, static_cast<vphilox::u64>(t) * (words / vphilox::block_words));

        vphilox::engine g{key, start};
        auto& buf = *buffers[t];

        if (want_perf) {
            instrs[t]->start();
            imisses[t]->start();
        }
        cycles.start();
        if constexpr (Mode == fill_mode::bulk) {
            g.generate_n(buf.data(), words);
        } else {
            for (std::size_t i = 0; i < words; ++i) buf[i] = g();
        }
        cycles.stop();
        if (want_perf) {
            imisses[t]->stop();
            instrs[t]->stop();
        }

        benchmark::DoNotOptimize(buf.data());
    };

    if constexpr (Runtime == runtime_kind::openmp) {
#if VPHILOX_HAVE_OPENMP
        // Dynamic adjustment lets the runtime hand back a smaller team than
        // asked for, which would leave `bytes` below counting work nobody did.
        // Turn it off, then confirm -- a short team is a skipped row, not a
        // quietly optimistic number.
        omp_set_dynamic(0);
        int team = 0;
#pragma omp parallel num_threads(static_cast<int>(threads))
        {
#pragma omp single
            team = omp_get_num_threads();
        }
        if (static_cast<std::size_t>(team) != threads) {
            state.SkipWithError("OpenMP gave a smaller team than requested");
            return;
        }

        for (auto _ : state) {
#pragma omp parallel num_threads(static_cast<int>(threads))
            {
                job(static_cast<std::size_t>(omp_get_thread_num()));
            }
            benchmark::ClobberMemory();
        }
#endif
    } else {
        worker_pool pool(threads);
        for (auto _ : state) {
            pool.run(job);
            benchmark::ClobberMemory();
        }
    }

    if (migrated.load(std::memory_order_relaxed)) {
        state.SkipWithError("worker threads migrated between slots; cycle counts unreliable");
        return;
    }

    const auto bytes =
        static_cast<std::int64_t>(state.iterations() * threads * words * sizeof(vphilox::u32));
    state.SetBytesProcessed(bytes);

    std::uint64_t total_cycles = 0;
    for (const auto& c : counters) {
        if (c) total_cycles += c->raw();
    }

    std::string label = vphilox::backend_name(vphilox::engine::which_backend());
    label += Runtime == runtime_kind::openmp ? ", runtime=openmp" : ", runtime=std::thread";
    if (total_cycles > 0 && bytes > 0) {
        // Aggregate: all threads' cycles over all threads' bytes. Flat across
        // the thread axis means linear scaling.
        state.counters["cycles_per_byte"] =
            static_cast<double>(total_cycles) / static_cast<double>(bytes);
        label += std::string{", cycle_source="} + counters.front()->source();
    }
    state.counters["kib_per_thread"] = static_cast<double>(words * sizeof(vphilox::u32)) / 1024.0;

    if (want_perf) {
        std::uint64_t total_instr = 0;
        std::uint64_t total_imiss = 0;
        bool all_available        = true;
        for (std::size_t t = 0; t < threads; ++t) {
            if (!instrs[t] || !instrs[t]->available() || !imisses[t]->available()) {
                all_available = false;
                break;
            }
            total_instr += instrs[t]->raw();
            total_imiss += imisses[t]->raw();
        }
        // A denied event reports nothing rather than a zero: an MPKI of 0.00
        // is the answer this study is looking for, so it must never be what a
        // missing PMU produces. `perf_event_paranoid` and VMs without a vPMU
        // both land here.
        if (!all_available || total_instr == 0) {
            state.SetLabel(label + ", perf_counters=unavailable");
            return;
        }
        state.counters["icache_mpki"] =
            static_cast<double>(total_imiss) * 1000.0 / static_cast<double>(total_instr);
        state.counters["instructions_per_byte"] =
            static_cast<double>(total_instr) / static_cast<double>(bytes);
        label += ", perf_counters=on";
    }

    state.SetLabel(label);
}

/// The buffered `operator()` path -- one word at a time, through the refill
/// buffer.
void BM_thread_scaling(benchmark::State& state) {
    run_scaling<fill_mode::per_call, runtime_kind::std_thread>(state);
}

/// The bulk path. On a single thread this is worth 25% on ARM and 42% on AVX2
/// (#37); the open question is whether that advantage survives contention or
/// whether the memory system takes it back.
void BM_thread_scaling_bulk(benchmark::State& state) {
    run_scaling<fill_mode::bulk, runtime_kind::std_thread>(state);
}

#if VPHILOX_HAVE_OPENMP
/// The same two paths under OpenMP (#52). Same buffers, same disjoint counter
/// ranges, same aggregate metric -- only the construct that starts the workers
/// differs, so these rows are directly comparable to the two above.
void BM_thread_scaling_omp(benchmark::State& state) {
    run_scaling<fill_mode::per_call, runtime_kind::openmp>(state);
}

void BM_thread_scaling_omp_bulk(benchmark::State& state) {
    run_scaling<fill_mode::bulk, runtime_kind::openmp>(state);
}
#endif

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

#if VPHILOX_HAVE_OPENMP
BENCHMARK(BM_thread_scaling_omp)
    ->ArgsProduct({{1, 2, 4, 8, 16, 32}, {kL2Words, kDramWords}})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_thread_scaling_omp_bulk)
    ->ArgsProduct({{1, 2, 4, 8, 16, 32}, {kL2Words, kDramWords}})
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);
#endif

// TODO(phase-4): instruction-cache miss rates via libpfm (#53), which Google
// Benchmark can collect with --benchmark_perf_counters.

}  // namespace

VPHILOX_BENCHMARK_MAIN();
