# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha

include(FetchContent)

find_package(GTest QUIET)

if(NOT GTest_FOUND AND VPHILOX_FETCH_DEPS)
    message(STATUS "vphilox: GoogleTest not found, fetching")
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    set(GTest_FOUND TRUE)
endif()

if(NOT GTest_FOUND)
    message(WARNING "vphilox: GoogleTest unavailable; skipping tests. "
                    "Install it or configure with -DVPHILOX_FETCH_DEPS=ON.")
    return()
endif()

include(GoogleTest)

set(VPHILOX_TEST_SOURCES
    test_version.cpp          # CalVer plumbing: VERSION file -> version.hpp
    test_reference_vectors.cpp # Phase 1: known-answer tests vs Random123
    test_counter.cpp          # Phase 1: 128-bit counter arithmetic
    test_kernel_parity.cpp    # Phase 2: SIMD kernels == scalar, bit for bit
    test_bulk_generate.cpp    # Phase 3: bulk path == operator(), word for word
    test_cpu_features.cpp     # Phase 3: CPUID/XGETBV probe, shared with MSVC
    test_float_cast.cpp       # Phase 3: mantissa injection range/uniformity
    test_engine.cpp           # Phase 3: C++20 concept, buffer, discard
    test_seeding.cpp          # Phase 3: seed_seq / u64 seeding and overload rank
)

foreach(src IN LISTS VPHILOX_TEST_SOURCES)
    cmake_path(GET src STEM name)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE vphilox::vphilox GTest::gtest_main)
    target_compile_features(${name} PRIVATE cxx_std_20)
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    gtest_discover_tests(${name})
endforeach()
