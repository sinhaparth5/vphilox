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
