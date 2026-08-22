# Repository Guidelines

## Project Structure & Module Organization

vphilox is a header-only C++20 library. Public headers live in `include/vphilox/`; implementation details and architecture-specific kernels are under `include/vphilox/detail/`. Keep tests in `tests/`, with one `test_<concern>.cpp` file per behavior. Performance programs belong in `benchmarks/`, the PractRand byte-stream utility lives in `tools/`, and design notes belong in `docs/`. CMake package helpers are maintained in `cmake/`.

## Build, Test, and Development Commands

- `cmake --preset default && cmake --build --preset default` configures a RelWithDebInfo Ninja build and builds tests, benchmarks, and tools.
- `ctest --preset default` runs the GoogleTest suite and prints failing output.
- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` enables strict warnings, including `-Werror`.
- `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` checks AddressSanitizer and UndefinedBehaviorSanitizer findings.
- `cmake --preset bench && cmake --build --preset bench` creates an optimized benchmark build under `build/bench/`.

CMake may fetch GoogleTest and Google Benchmark. Set `VPHILOX_FETCH_DEPS=OFF` when an offline build must use installed dependencies only.

## Coding Style & Naming Conventions

Format C++ and CMake changes with the repository `.clang-format`: Google-based C++20 style, four-space indentation, 100-column limit, and left-aligned pointers. Use `snake_case` for files and functions, and descriptive PascalCase GoogleTest case names such as `TEST(Counter, CarriesAcrossEveryWord)`. Comments should explain design intent or SIMD lane layout, not restate code. Every new source file must include the existing SPDX and copyright header.

## Testing Guidelines

New behavior requires a focused GoogleTest. All kernels must match `kernel_scalar.hpp` bit-for-bit, including partial-vector tails; extend the shared parity matrix instead of creating architecture-only expectations. The published Random123 vectors are permanent compatibility tests. Run default, debug, and ASan presets before submitting.

## Commit & Pull Request Guidelines

Recent commits use concise, imperative, sentence-case subjects without type prefixes (for example, `Add project scaffolding ...`). Keep each commit scoped to one logical change. Pull requests should explain motivation and compatibility impact, list commands run, link relevant issues, and include before/after benchmark results for performance work. Report both cycles per byte and GB/s. Never change the generated stream for an existing key/counter pair; such a change is a different generator, not a compatible fix.
