# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

vphilox is a header-only C++20 implementation of the Philox4x32-10 counter-based
PRNG, being built toward SIMD-accelerated kernels. `CONTRIBUTING.md` has the
full contributor detail.

## Build, test, benchmark

Requires CMake 3.24+, Ninja, and a C++20 compiler. Presets are the supported
entry point — `default` builds into `build/`, every other preset into
`build/<preset>/`.

```bash
cmake --preset default && cmake --build --preset default && ctest --preset default
cmake --preset debug   && cmake --build --preset debug   && ctest --preset debug   # -Werror
cmake --preset asan    && cmake --build --preset asan    && ctest --preset asan    # ASan + UBSan
cmake --preset scalar  && cmake --build --preset scalar  && ctest --preset scalar  # VPHILOX_FORCE_SCALAR
cmake --preset bench   && cmake --build --preset bench                             # -> build/bench/benchmarks/
```

Run default, debug, and asan before submitting; CI runs all four plus macOS
arm64, Windows MSVC, an install/`find_package` consumer check, and a
`clang-format --dry-run --Werror` sweep over `include tests benchmarks tools`.

Single tests (GoogleTest cases are auto-discovered into CTest):

```bash
ctest --preset default -R KernelParity --output-on-failure
ctest --preset default -R Engine.ReportsItsBackend -V     # prints the resolved backend
build/tests/test_engine --gtest_filter='Engine.Discard*'  # run the binary directly
```

Pin a backend at runtime without rebuilding: `VPHILOX_BACKEND=scalar|avx2|avx512|neon`.

Benchmarks want a quiet, frequency-pinned machine and report **cycles per byte**
first, GB/s second (`bench_engines.cpp` reads RDTSC on x86 and
`perf_event_open` on Linux ARM). Results are written up under
`docs/benchmarks/`, raw JSON in `docs/benchmarks/raw/` and `results/`.

`VPHILOX_FETCH_DEPS=OFF` forces an offline build using only installed
GoogleTest/Benchmark.

## Statistical validation

`tools/vphilox_stream` writes raw bytes on stdout for piping into PractRand or
TestU01; `tools/vphilox_testu01` drives `vphilox::engine` through TestU01's
callback interface so the refill buffer and runtime dispatch are under test,
not just a byte stream. TestU01 ships under a non-permissive licence, so it is
never vendored — the `vphilox_testu01` target simply does not exist unless the
library is installed, and configure logs which case you are in.

```bash
scripts/statistical/build_practrand.sh    # PractRand pre0.95 -> ~/.local/src
scripts/statistical/build_testu01.sh      # TestU01 -> ~/.local
scripts/statistical/run_practrand.sh --length 1TB --backend avx2
```

These batteries take hours, which is why they are scripts rather than CTest
cases. Every archived log under `results/practrand/` and `results/testu01/`
carries a provenance header (git SHA, CPU, resolved backend, verbatim command);
`docs/statistical-validation.md` is the write-up. `run_practrand.sh` runs under
`set -o pipefail`, so `vphilox_stream` ignores `SIGPIPE` — PractRand closes the
pipe at `-tlmax` and the default disposition made completed runs report 141.

## Architecture

Everything is headers under `include/vphilox/`; there is no compiled library.
The target `vphilox::vphilox` is an INTERFACE target, and tests/benchmarks/tools
only build when vphilox is the top-level project.

The layering, bottom up:

- `config.hpp` — architecture macros and the `VPHILOX_HAS_AVX2/AVX512/NEON`
  gates. Included by everything, includes nothing of ours. CMake's
  `VPHILOX_ENABLE_*` options arrive here as `VPHILOX_NO_*` definitions;
  `VPHILOX_FORCE_SCALAR` zeroes all of them.
- `constants.hpp` — Philox multipliers/Weyl increments and the `counter4` /
  `key2` value types.
- `counter.hpp` — 128-bit little-endian counter arithmetic. This is what makes
  `discard()` O(1): seeking N blocks is `counter += N`.
- `detail/kernel_scalar.hpp` — the reference implementation and ground truth,
  fully `constexpr`.
- `detail/kernel_avx2.hpp` — eight interleaved counters per `__m256i`,
  `[[gnu::target("avx2")]]` rather than a TU flag because the kernel lives in a
  header. `detail/kernel_{avx512,neon}.hpp` are still Phase 2 stubs that
  forward to the scalar kernel.
- `detail/cpu_features.hpp` — one-shot runtime CPU probe. The x86 path is a raw
  `CPUID` + `XGETBV` probe written once for every compiler (`__cpuidex` on MSVC,
  `__cpuid_count` elsewhere) rather than `__builtin_cpu_supports`, so the Linux
  and macOS runs exercise the same detection logic MSVC depends on.
- `detail/dispatch.hpp` — resolves one `kernel_fn` per `Rounds` instantiation on
  first use.
- `philox.hpp` — `basic_engine<Rounds>` / `engine`, a
  `std::uniform_random_bit_generator` over a 32-word (128-byte) aligned refill
  buffer sized so no backend ever splits a refill. `generate_n(u32*, count)` and
  `generate(std::span<u32>)` are the bulk path: they run the kernel straight
  into the caller's buffer, skipping the refill copy (100% of raw kernel
  throughput, 1.72x the buffered engine). Bulk and `operator()` must stay
  interchangeable — N bulk words produce exactly what N `operator()` calls
  produce and leave the engine in the same state.
- `float_cast.hpp` — division-free `u32/u64 -> float/double` by IEEE-754
  mantissa injection.

### The kernel contract

Every backend implements `generate(base, key, out, blocks)`, writing
`blocks * 4` words where block *i* is `philox4x32(base + i, key)`. Output must
not depend on how the caller chunks the request — that independence is what
makes parity testing meaningful and lets the engine refill in any size. Kernels
handle their own tails and must not assume `out` is aligned.
`preferred_blocks` advertises the tail-free width (scalar 1, AVX2 8, AVX-512 8,
NEON 2 — settled in `docs/benchmarks/simd-lane-layout.md`).

### Dispatch and the `implemented` flag

A kernel is selected only if it is compiled in (`VPHILOX_HAS_*`), has
`implemented = true`, and the CPU supports it. The remaining stubs set
`implemented = false`, which keeps them out of dispatch entirely so
`active_backend()` never reports a backend that did not actually run. Landing a
SIMD kernel means: implement `generate()`, flip `implemented` to `true`,
confirm `preferred_blocks` matches the real interleaving width — dispatch and
`tests/test_kernel_parity.cpp` pick it up with no further wiring. AVX2 is the
worked example of all of this.

Never add ISA flags to the whole build. `VPHILOX_FLAGS_AVX2` /
`VPHILOX_FLAGS_AVX512` are staged in the root `CMakeLists.txt` for attaching to
individual translation units, or use `[[gnu::target]]` / `VPHILOX_TARGET`. One
binary must start and run on a CPU lacking the instructions and fall back at
runtime.

## The invariant

**The generated bit stream never changes.** For any (key, counter), vphilox
produces exactly what Random123's Philox4x32-10 produces — across compilers,
architectures, and releases. `tests/test_reference_vectors.cpp` enforces this
against the published Salmon et al. vectors; if it fails, the change is wrong,
not the test. A change that alters output is a different generator, not a fix
and not a new version.

## Tests

One file per concern in `tests/`, named `test_<concern>.cpp` and registered in
`tests/CMakeLists.txt`. New behavior needs a focused test. New kernels extend
the shared parity matrix in `test_kernel_parity.cpp` (which sweeps carrying
counters, edge keys, and block counts straddling every plausible SIMD width)
rather than getting bespoke architecture-only expectations — tails are where
kernels break.

## Conventions

- `.clang-format` is authoritative and CI-enforced: Google base, 4-space indent,
  100 columns, left-aligned pointers, consecutive-assignment alignment (chosen
  so a botched lane index in intrinsic code is visible at a glance).
- `snake_case` files and functions; PascalCase GoogleTest names like
  `TEST(Counter, CarriesAcrossEveryWord)`.
- Comments explain *why* — a note naming the lane layout or the reason for a
  shuffle beats one restating the intrinsic.
- Every source file starts with:
  ```
  // SPDX-License-Identifier: MIT OR Apache-2.0
  // Copyright (c) 2026 Parth Sinha
  ```
- Version is CalVer `YYYY.0M.MICRO`, and the root `VERSION` file is the only
  place it appears; CMake propagates it into the generated
  `vphilox/version.hpp`. Never hardcode a version elsewhere.
- Commits: concise imperative sentence-case subjects, no type prefixes, one
  logical change each. PRs state compatibility impact and, for performance
  work, before/after numbers in both cycles per byte and GB/s.
- Pasted code needs its license checked; MIT/BSD/Apache-2.0 can be relocated
  with attribution, copyleft cannot.

## Current state

Phase 1 (scalar reference, KATs, baseline benchmarks) is done. In Phase 2, AVX2
has landed (3.3x scalar, 1.41x `std::mt19937` buffered — see
`docs/benchmarks/avx2-2026-08-23.md`); AVX-512 and NEON are still stubs. Phase 4
statistical validation is done and does not need re-running: PractRand clean to
1 TB, byte-identical across backends over that terabyte, and TestU01 BigCrush
passing all 160 statistics (`docs/statistical-validation.md`).
`ROADMAP.md` tracks phases and `docs/` holds the theory, research, and recorded
benchmark runs.

Two measured results worth not re-deriving: the ~10x
scalar-Philox-vs-mt19937 slowdown from the literature did **not** reproduce on
any machine measured here (`docs/benchmarks/baseline-2026-08-21.md`), and the
refill buffer now costs ~42% of bulk throughput on AVX2 versus ~10% on scalar,
which is what makes issues #36 and #37 worth more than their original
estimates.
