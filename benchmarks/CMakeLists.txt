# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha

include(FetchContent)

find_package(benchmark QUIET)

if(NOT benchmark_FOUND AND VPHILOX_FETCH_DEPS)
    message(STATUS "vphilox: Google Benchmark not found, fetching")
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.0
        GIT_SHALLOW    TRUE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    set(benchmark_FOUND TRUE)
endif()

if(NOT benchmark_FOUND)
    message(WARNING "vphilox: Google Benchmark unavailable; skipping benchmarks.")
    return()
endif()

# bench_engines  - vphilox vs mt19937 vs scalar Philox (Phase 1 baseline,
#                  Phase 4 comparison matrix)
# bench_float    - mantissa injection vs uniform_real_distribution (Phase 3)
# bench_scaling  - OpenMP thread scaling, 1..32 threads (Phase 4)
set(VPHILOX_BENCH_SOURCES
    bench_engines.cpp
    bench_float.cpp
    bench_scaling.cpp
)

if(VPHILOX_ARCH_X86 AND VPHILOX_ENABLE_AVX2)
    list(APPEND VPHILOX_BENCH_SOURCES bench_lane_layout.cpp)
endif()

foreach(src IN LISTS VPHILOX_BENCH_SOURCES)
    cmake_path(GET src STEM name)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE vphilox::vphilox benchmark::benchmark)
    target_compile_features(${name} PRIVATE cxx_std_20)

    # Benchmarks are the one place -O3 -march=native is appropriate: we are
    # measuring the ceiling, not shipping the binary.
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -O3)
    endif()
endforeach()

if(TARGET bench_lane_layout)
    target_compile_options(bench_lane_layout PRIVATE ${VPHILOX_FLAGS_AVX2})
endif()

# Optional OpenMP for the scaling benchmark.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(bench_scaling PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(bench_scaling PRIVATE VPHILOX_HAVE_OPENMP=1)
else()
    message(STATUS "vphilox: OpenMP not found; bench_scaling falls back to std::thread")
endif()
