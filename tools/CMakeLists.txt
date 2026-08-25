# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha

# vphilox_stream: raw random bytes on stdout, for piping into PractRand or
# TestU01. Kept out of the test suite because a meaningful run takes hours.
add_executable(vphilox_stream vphilox_stream.cpp)
target_link_libraries(vphilox_stream PRIVATE vphilox::vphilox)
target_compile_features(vphilox_stream PRIVATE cxx_std_20)
if(NOT MSVC)
    target_compile_options(vphilox_stream PRIVATE -O3)
endif()

# vphilox_testu01: the TestU01 batteries. Optional -- TestU01 ships under its
# own non-permissive licence, so it is never vendored and the target simply
# does not exist when the library is absent. scripts/statistical/build_testu01.sh
# installs it into ~/.local by default.
find_path(TESTU01_INCLUDE_DIR bbattery.h
    HINTS ENV TESTU01_HOME PATH_SUFFIXES include
    PATHS "$ENV{HOME}/.local/include" /usr/local/include /usr/include)
find_library(TESTU01_LIBRARY   testu01
    HINTS ENV TESTU01_HOME PATH_SUFFIXES lib
    PATHS "$ENV{HOME}/.local/lib" /usr/local/lib /usr/lib)
find_library(TESTU01_PROBDIST  probdist
    HINTS ENV TESTU01_HOME PATH_SUFFIXES lib
    PATHS "$ENV{HOME}/.local/lib" /usr/local/lib /usr/lib)
find_library(TESTU01_MYLIB     mylib
    HINTS ENV TESTU01_HOME PATH_SUFFIXES lib
    PATHS "$ENV{HOME}/.local/lib" /usr/local/lib /usr/lib)

if(TESTU01_INCLUDE_DIR AND TESTU01_LIBRARY AND TESTU01_PROBDIST AND TESTU01_MYLIB)
    add_executable(vphilox_testu01 vphilox_testu01.cpp)
    target_link_libraries(vphilox_testu01 PRIVATE
        vphilox::vphilox ${TESTU01_LIBRARY} ${TESTU01_PROBDIST} ${TESTU01_MYLIB} m)
    target_include_directories(vphilox_testu01 PRIVATE ${TESTU01_INCLUDE_DIR})
    target_compile_features(vphilox_testu01 PRIVATE cxx_std_20)
    if(NOT MSVC)
        target_compile_options(vphilox_testu01 PRIVATE -O3)
    endif()
    message(STATUS "vphilox: TestU01 found, building vphilox_testu01")
else()
    message(STATUS "vphilox: TestU01 not found, skipping vphilox_testu01 "
                   "(run scripts/statistical/build_testu01.sh)")
endif()

# vphilox_curand_parity: cross-check the stream against NVIDIA cuRAND (#54).
# Optional in exactly the way vphilox_testu01 is -- the target does not exist
# unless the cuRAND headers are installed, and CUDA is never a dependency of
# the library itself.
#
# Headers only: no nvcc, no CUDA runtime, no GPU. cuRAND guards its function
# decoration with `#if !defined(QUALIFIERS)`, so the tool defines QUALIFIERS
# and compiles NVIDIA's reference implementation as host C++ with the same
# compiler as everything else. That is what lets this run on any machine with
# the CUDA toolkit unpacked, rather than only on a GPU box -- see the note at
# the top of vphilox_curand_parity.cpp for why it is not a weaker check.
find_path(CURAND_INCLUDE_DIR curand_kernel.h
    HINTS ENV CUDA_HOME ENV CUDA_PATH PATH_SUFFIXES include
    PATHS /usr/local/cuda/include /usr/local/cuda/targets/x86_64-linux/include
          /opt/cuda/include /usr/include)

if(CURAND_INCLUDE_DIR)
    add_executable(vphilox_curand_parity vphilox_curand_parity.cpp)
    target_link_libraries(vphilox_curand_parity PRIVATE vphilox::vphilox)
    target_include_directories(vphilox_curand_parity PRIVATE ${CURAND_INCLUDE_DIR})
    target_compile_features(vphilox_curand_parity PRIVATE cxx_std_20)
    if(NOT MSVC)
        target_compile_options(vphilox_curand_parity PRIVATE -O3)
    endif()
    message(STATUS "vphilox: cuRAND headers found, building vphilox_curand_parity")
else()
    message(STATUS "vphilox: cuRAND headers not found, skipping vphilox_curand_parity "
                   "(install the CUDA toolkit headers to enable the cross-check)")
endif()
