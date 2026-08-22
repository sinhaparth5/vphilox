# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha

cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VphiloxVersion)

# CalVer, read from the VERSION file (the single source of truth).
vphilox_read_version(VPHILOX_VERSION VPHILOX_VERSION_YEAR VPHILOX_VERSION_MONTH
                     VPHILOX_VERSION_MICRO VPHILOX_VERSION_MONTH_N)

project(vphilox
    VERSION     ${VPHILOX_VERSION}
    DESCRIPTION "SIMD-accelerated counter-based PRNG (Philox4x32-10) for parallel CPU systems"
    HOMEPAGE_URL "https://github.com/sinhaparth5/vphilox"
    LANGUAGES   CXX)

# Define the standard install directories before they are referenced by the
# vphilox target's BUILD_INTERFACE/INSTALL_INTERFACE include paths. In a fresh
# configure these variables do not exist until GNUInstallDirs is loaded.
include(GNUInstallDirs)

message(STATUS "vphilox ${VPHILOX_VERSION} (CalVer ${VPHILOX_VERSION_YEAR}.${VPHILOX_VERSION_MONTH}.${VPHILOX_VERSION_MICRO})")

# Are we the top-level project, or vendored into someone else's build?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VPHILOX_IS_TOP_LEVEL ON)
else()
    set(VPHILOX_IS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
include(CMakeDependentOption)

option(VPHILOX_BUILD_TESTS      "Build the vphilox test suite"        ${VPHILOX_IS_TOP_LEVEL})
option(VPHILOX_BUILD_BENCHMARKS "Build the vphilox benchmarks"        ${VPHILOX_IS_TOP_LEVEL})
option(VPHILOX_BUILD_TOOLS      "Build CLI tools (PractRand streamer)" ${VPHILOX_IS_TOP_LEVEL})
option(VPHILOX_INSTALL          "Generate the vphilox install target"  ${VPHILOX_IS_TOP_LEVEL})
option(VPHILOX_FETCH_DEPS       "Fetch GoogleTest/Benchmark if not found" ON)

# Backend gates. ON means "compile the kernel in and let runtime dispatch
# decide"; it never forces the kernel onto a CPU that cannot run it.
option(VPHILOX_ENABLE_AVX2   "Compile the AVX2 kernel"    ON)
option(VPHILOX_ENABLE_AVX512 "Compile the AVX-512 kernel" ON)
option(VPHILOX_ENABLE_NEON   "Compile the ARM NEON kernel" ON)
option(VPHILOX_FORCE_SCALAR  "Ignore every SIMD kernel and always run scalar" OFF)

# ---------------------------------------------------------------------------
# Target architecture
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(VPHILOX_ARCH_X86 ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(VPHILOX_ARCH_ARM64 ON)
endif()

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vphilox/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/vphilox/version.hpp"
    @ONLY)

# ---------------------------------------------------------------------------
# The library: header-only INTERFACE target
# ---------------------------------------------------------------------------
add_library(vphilox INTERFACE)
add_library(vphilox::vphilox ALIAS vphilox)

target_compile_features(vphilox INTERFACE cxx_std_20)

target_include_directories(vphilox INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_definitions(vphilox INTERFACE
    $<$<NOT:$<BOOL:${VPHILOX_ENABLE_AVX2}>>:VPHILOX_NO_AVX2>
    $<$<NOT:$<BOOL:${VPHILOX_ENABLE_AVX512}>>:VPHILOX_NO_AVX512>
    $<$<NOT:$<BOOL:${VPHILOX_ENABLE_NEON}>>:VPHILOX_NO_NEON>
    $<$<BOOL:${VPHILOX_FORCE_SCALAR}>:VPHILOX_FORCE_SCALAR>)

# Per-kernel ISA flags. These are attached to the translation units that
# contain the kernels, NOT to the whole build -- the binary must still start
# on a CPU without AVX-512 and fall back at runtime.
#
# Phase 2 will use these via target_compile_options() on the kernel TUs, or
# via [[gnu::target("avx2")]] function attributes in the headers.
set(VPHILOX_FLAGS_AVX2   "" CACHE INTERNAL "")
set(VPHILOX_FLAGS_AVX512 "" CACHE INTERNAL "")
if(VPHILOX_ARCH_X86 AND NOT MSVC)
    set(VPHILOX_FLAGS_AVX2   "-mavx2" CACHE INTERNAL "")
    set(VPHILOX_FLAGS_AVX512 "-mavx512f;-mavx512dq;-mavx512vl" CACHE INTERNAL "")
elseif(VPHILOX_ARCH_X86 AND MSVC)
    set(VPHILOX_FLAGS_AVX2   "/arch:AVX2"    CACHE INTERNAL "")
    set(VPHILOX_FLAGS_AVX512 "/arch:AVX512"  CACHE INTERNAL "")
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
if(VPHILOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VPHILOX_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(VPHILOX_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
if(VPHILOX_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS vphilox EXPORT vphiloxTargets)

    # Both licenses ship: the offer is "either one", so a consumer who only
    # received the tarball still has the text of whichever they picked.
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-APACHE"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-MIT"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/vphilox")

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/vphilox"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            FILES_MATCHING PATTERN "*.hpp")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/vphilox/version.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vphilox")

    install(EXPORT vphiloxTargets
            FILE      vphiloxTargets.cmake
            NAMESPACE vphilox::
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vphilox")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vphilox-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vphilox-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vphilox")

    # CalVer is not semver: a newer YYYY.0M is not assumed API-compatible with
    # an older one, so consumers get exact-or-newer-micro matching only.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/vphilox-config-version.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
        ARCH_INDEPENDENT)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/vphilox-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/vphilox-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vphilox")
endif()
