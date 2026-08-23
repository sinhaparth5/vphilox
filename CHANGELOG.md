# Changelog

Versions are CalVer (`YYYY.0M.MICRO`); see [VERSIONING.md](VERSIONING.md).

The **generated bit stream for a given (key, counter) is frozen permanently**.
Any entry that would change it would be a new algorithm, not a release.

## [Unreleased]

### Changed
- x86 CPU detection now uses a raw CPUID + `XGETBV` probe on every compiler
  instead of `__builtin_cpu_supports`. MSVC previously had no detection at all
  and reported no features, so MSVC builds ran scalar even on AVX2 hardware;
  they now resolve the same backend as GCC and Clang. `tests/test_cpu_features.cpp`
  checks the probe against `__builtin_cpu_supports` wherever that builtin
  exists, so the path MSVC depends on is exercised on every Linux and macOS run.

## [2026.08.0] — 2026-08-21

Initial scaffold. Phase 0 complete, Phase 1 substantially complete.

### Added
- CMake 3.24+ build with a header-only `vphilox::vphilox` INTERFACE target
- CalVer plumbing: `VERSION` → `cmake/VphiloxVersion.cmake` → generated `vphilox/version.hpp`
- Scalar reference Philox4x32-10 (`detail/kernel_scalar.hpp`), fully `constexpr`
- 128-bit counter arithmetic (`counter.hpp`) backing O(1) seeking
- IEEE-754 mantissa injection for `float` and `double` (`float_cast.hpp`)
- `vphilox::engine` satisfying `std::uniform_random_bit_generator`, with a
  64-byte-aligned 128-byte refill buffer
- Runtime CPU detection and kernel dispatch, with a `VPHILOX_BACKEND` override
- Kernel stubs for AVX2, AVX-512, and NEON — present in the tree, excluded from
  dispatch until implemented
- Test suite: Random123 known-answer vectors, counter arithmetic, kernel parity
  harness, float conversion, engine semantics, version plumbing
- Benchmarks: engine comparison, float conversion, thread scaling
- `vphilox_stream` for piping into PractRand / TestU01
- Install/export rules; `find_package(vphilox)` verified downstream

### Licensing
- Dual-licensed MIT OR Apache-2.0. `LICENSE` became `LICENSE-APACHE`, and
  `LICENSE-MIT` was added alongside it; every source file carries an
  `SPDX-License-Identifier`. Consumers may take either license.

### Known limitations
- SIMD kernels fall back to scalar; there is no speedup yet
- MSVC CPU feature detection is stubbed to `false`, so MSVC builds run scalar
- No statistical test results recorded yet
