# SPDX-License-Identifier: MIT OR Apache-2.0
# Copyright (c) 2026 Parth Sinha

# VphiloxVersion.cmake
#
# vphilox uses CalVer: YYYY.0M.MICRO  (e.g. 2026.08.0)
#   YYYY  - four-digit release year
#   0M    - zero-padded release month
#   MICRO - patch counter, reset to 0 on every new YYYY.0M
#
# The `VERSION` file at the repo root is the single source of truth. Nothing
# else in the tree hardcodes a version number; CMake reads it here and
# propagates it to project(), version.hpp, the install package, and CI.
#
# See VERSIONING.md for the compatibility contract.

function(vphilox_read_version out_full out_year out_month out_micro out_month_n)
    set(_version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

    if(NOT EXISTS "${_version_file}")
        message(FATAL_ERROR "vphilox: missing version file: ${_version_file}")
    endif()

    file(READ "${_version_file}" _raw)
    string(STRIP "${_raw}" _raw)

    if(NOT _raw MATCHES "^([0-9][0-9][0-9][0-9])\\.([0-9][0-9])\\.([0-9]+)$")
        message(FATAL_ERROR
            "vphilox: '${_raw}' in VERSION is not valid CalVer.\n"
            "Expected YYYY.0M.MICRO (zero-padded month), e.g. 2026.08.0")
    endif()

    set(_year  "${CMAKE_MATCH_1}")
    set(_month "${CMAKE_MATCH_2}")
    set(_micro "${CMAKE_MATCH_3}")

    # Reject a month that is syntactically padded but not a real month.
    math(EXPR _month_n "${_month} + 0")
    if(_month_n LESS 1 OR _month_n GREATER 12)
        message(FATAL_ERROR "vphilox: '${_month}' is not a month (01-12) in version '${_raw}'")
    endif()

    set(${out_full}    "${_raw}"     PARENT_SCOPE)
    set(${out_year}    "${_year}"    PARENT_SCOPE)
    set(${out_month}   "${_month}"   PARENT_SCOPE)  # zero-padded, for display
    set(${out_micro}   "${_micro}"   PARENT_SCOPE)
    set(${out_month_n} "${_month_n}" PARENT_SCOPE)  # unpadded; "08" is not a
                                                    # valid C octal literal

    # Re-run CMake when VERSION changes.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_version_file}")
endfunction()
