// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Benchmark entry point that stamps provenance into the result JSON.
//
// Google Benchmark's `context` block records the machine and the clock, but
// nothing about which kernel actually ran -- and on this project that is the
// one fact a reader needs, because every row is the same generator and the
// only variable is the backend dispatch resolved to. Four archived runs had
// to have their backend recovered by hand from the surrounding notes; this
// removes that step and makes `docs/benchmarks/raw/` derivable from the JSON
// alone.
//
// Resolving the backend here rather than reading it off the first benchmark
// is deliberate: dispatch is a one-shot resolve, so calling it before any
// benchmark runs pins the same value every row will use, including when
// VPHILOX_BACKEND forces one.

#ifndef VPHILOX_BENCH_MAIN_HPP
#define VPHILOX_BENCH_MAIN_HPP

#include <benchmark/benchmark.h>

#include "vphilox/vphilox.hpp"

namespace vphilox_bench {

inline void add_provenance_context() {
    benchmark::AddCustomContext("vphilox_backend",
                                vphilox::backend_name(vphilox::active_backend()));
    benchmark::AddCustomContext("vphilox_version", vphilox::version_string);
}

}  // namespace vphilox_bench

// Mirrors BENCHMARK_MAIN() with the context call inserted before the run.
#define VPHILOX_BENCHMARK_MAIN()                                            \
    int main(int argc, char** argv) {                                       \
        ::benchmark::Initialize(&argc, argv);                               \
        if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1; \
        ::vphilox_bench::add_provenance_context();                          \
        ::benchmark::RunSpecifiedBenchmarks();                              \
        ::benchmark::Shutdown();                                            \
        return 0;                                                           \
    }                                                                       \
    int main(int, char**)

#endif  // VPHILOX_BENCH_MAIN_HPP
