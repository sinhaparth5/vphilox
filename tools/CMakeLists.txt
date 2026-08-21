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
