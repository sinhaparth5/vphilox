# Versioning

vphilox uses **CalVer** in the form `YYYY.0M.MICRO`.

```
2026.08.0
│    │  └── MICRO: patch counter, resets to 0 on every new YYYY.0M
│    └───── 0M:    zero-padded release month (01-12)
└────────── YYYY:  four-digit release year
```

## Why CalVer and not SemVer

SemVer communicates API breakage. That is useful when breakage is the main
thing a consumer needs to plan around. For vphilox the more important question
is *how old is this*, because the interesting axes are hardware support and
statistical validation, not interface churn — a build from two years ago may
predate the AVX-512 kernel or a PractRand result regardless of whether the API
moved. CalVer answers that at a glance.

The API stability that SemVer would have encoded is stated explicitly below
instead.

## The single source of truth

The `VERSION` file at the repository root. Nothing else in the tree hardcodes a
version.

```
VERSION
  └─> cmake/VphiloxVersion.cmake   (parses and validates)
       └─> project(vphilox VERSION ...)
            ├─> include/vphilox/version.hpp   (generated)
            ├─> the installed CMake package version file
            └─> CI artifacts and release tags
```

`vphilox_read_version()` rejects anything that is not `YYYY.0M.MICRO` with a
real month, so a malformed bump fails at configure time rather than shipping.

Note that the month reaches C++ twice, in two forms: `VPHILOX_VERSION_STRING`
keeps the zero-padding for display, while `VPHILOX_VERSION_MONTH` is unpadded
because `08` is not a valid C integer literal (it parses as octal, and `8` is
not a legal octal digit).

## Cutting a release

1. Edit `VERSION`.
2. `cmake --build build && ctest --test-dir build` — `test_version.cpp` checks
   the plumbing.
3. Add a `CHANGELOG.md` entry.
4. Commit, then tag as `v<VERSION>` (e.g. `v2026.08.0`).

Same year and month as the last release? Bump MICRO. New month? New
`YYYY.0M.0`. Months with no release are simply skipped — there is no
obligation to ship monthly.

## Compatibility contract

Since the version number does not encode it, here it is:

| Surface | Stability |
|---|---|
| `vphilox::engine`, `basic_engine<R>` public members | Stable. Breaking changes get a `CHANGELOG.md` entry marked **BREAKING** and a deprecation cycle where practical. |
| **Generated bit stream for a given (key, counter)** | **Frozen forever.** This is the strongest guarantee in the library. Reproducibility is the product; a stream change would be a new algorithm, not a new version. |
| `vphilox::to_float01` / `to_double01` results | Frozen, for the same reason. |
| `vphilox::detail::*` | No guarantee. Kernels, dispatch, and CPU probing are free to change shape. |
| Compiled-in kernel set and dispatch preference | May change in any release — they are performance decisions, and the output is identical either way. |
| Minimum toolchain (C++20, CMake 3.24) | May rise in a new `YYYY.0M`, never in a MICRO bump. |

## Consuming from CMake

```cmake
find_package(vphilox 2026.08 REQUIRED)
target_link_libraries(app PRIVATE vphilox::vphilox)
```

The installed package version file uses `SameMinorVersion`, so a request for
`2026.08` is satisfied by any `2026.08.x` but not by `2026.09.0`. CalVer months
are not assumed compatible with each other; ask for what you tested against.

To pin exactly, request the full `2026.08.0` and add `EXACT`.

## Checking the version in C++

```cpp
#include <vphilox/version.hpp>

static_assert(VPHILOX_VERSION >= VPHILOX_VERSION_NUMBER(2026, 8, 0),
              "vphilox 2026.08.0 or newer required");

std::cout << vphilox::version_string;  // "2026.08.0"
```

`VPHILOX_VERSION` is `YYYY*10000 + MM*100 + MICRO`, so it compares
monotonically in the preprocessor.
