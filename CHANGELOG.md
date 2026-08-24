# Changelog

Versions are CalVer (`YYYY.0M.MICRO`); see [VERSIONING.md](VERSIONING.md).

The **generated bit stream for a given (key, counter) is frozen permanently**.
Any entry that would change it would be a new algorithm, not a release.

## [Unreleased]

### Added
- Bulk float generation: `generate_n(float*, count)` and
  `generate(std::span<float>)`, equivalent to the same number of
  `next_float()` calls in both output and resulting engine state. Roughly 2x
  the word-at-a-time path; see `docs/benchmarks/float-conversion-2026-08-24.md`
  for why the exact figure is not recorded yet.
- `detail/float_bulk.hpp`: the u32 -> float conversion loop compiled twice,
  once at the consumer's baseline ISA and once under `VPHILOX_TARGET("avx2")`,
  selected by the same CPU probe and `VPHILOX_BACKEND` override as the
  kernels. No intrinsics: the compiler already emits
  `vpsrld` / `vpor` / `vaddps` for the plain loop, so the target attribute is
  the only part that was missing. Conversion is elementwise, so all widths are
  bit-identical and the choice never affects the stream.
- Seeding from a `std::seed_seq`: `basic_engine(Sseq&)` and `seed(Sseq&)`,
  alongside a matching `seed(u64)`, so `std::mt19937 rng(seq)` call sites port
  by changing the type and nothing else. Only the key is drawn -- two words,
  the whole of it -- and the counter always starts at zero. The constructor is
  constrained to actual seed sequences so the engine does not advertise
  constructibility from arbitrary lvalues.
- Vendored benchmark baselines under `benchmarks/third_party/`: xoshiro256++
  with splitmix64 (public domain, Blackman and Vigna) and pcg-cpp
  (Apache-2.0 OR MIT, O'Neill), both byte-for-byte upstream with SHA-256s
  recorded in `benchmarks/third_party/README.md`. Nothing there enters the
  library or the install target. xoshiro gets a thin instantiable wrapper
  because upstream keeps its state in file-scope statics, which the matrix and
  the scaling benchmark cannot share; `tests/test_third_party_generators.cpp`
  runs that wrapper against the vendored C side by side and requires
  bit-for-bit agreement, so a transliteration slip cannot quietly bias the
  comparison.
- Bulk generation API on the engine: `generate_n(u32*, count)` and
  `generate(std::span<u32>)`. Both produce exactly what the same number of
  `operator()` calls would and leave the engine where those calls would, so
  bulk and single-word access mix freely on one engine. Generating straight
  into the caller's buffer reaches 100.0% of the raw kernel — 1.72x the
  buffered engine and 2.40x `std::mt19937`. See
  `docs/benchmarks/buffer-overhead-2026-08-23.md`.
- AVX2 backend (`detail/kernel_avx2.hpp`): eight interleaved counters per
  `__m256i`, split even/odd `_mm256_mul_epu32` wide multiplies, in-register
  counter carry expansion, and an unpack/permute transpose back to block order.
  Dispatch selects it automatically on AVX2 hardware. Measured at 3.30x the
  scalar kernel and 1.41x `std::mt19937` through the buffered engine; see
  `docs/benchmarks/avx2-2026-08-23.md`.
- TestU01 harness (`tools/vphilox_testu01`) and build scripts for PractRand and
  TestU01 under `scripts/statistical/`. The harness drives `vphilox::engine`
  through TestU01's callback interface, so the refill buffer and runtime
  dispatch are under test rather than just a byte stream. The target is
  optional and absent when TestU01 is not installed.
- Float conversion tests for lost entropy and uniformity: an exhaustive
  round-trip over all 2^23 representable outputs, plus chi-square and
  Kolmogorov-Smirnov checks. Mutation testing confirms the round-trip catches a
  stuck low mantissa bit that no distributional test can see.
- Chunking-independence check in `tests/test_kernel_parity.cpp`: a run split at
  every offset must equal the unsplit run, which also exercises unaligned
  destination pointers.

### Changed
- `tools/vphilox_stream` fills its output chunk through the bulk path instead
  of word by word: 1.80x on AVX2, with byte-identical output on every backend.
- `tools/vphilox_stream` now ignores `SIGPIPE`. PractRand closes the pipe the
  moment it reaches `-tlmax`, and the default disposition killed the process
  before `fwrite` could return, so the graceful-exit path was unreachable and a
  *completed* 1 TB run reported exit 141 — which any runner using
  `set -o pipefail` reads as a failure.
- Corrected an off-by-one in the `float_cast.hpp` documentation, which claimed
  24 of 32 bits survive on a 2^-24 grid (and 2^-53 for double). The code keeps
  23 and 52; the 24th significand bit is the implicit leading 1, pinned by the
  fixed exponent, and carries no entropy.
- x86 CPU detection now uses a raw CPUID + `XGETBV` probe on every compiler
  instead of `__builtin_cpu_supports`. MSVC previously had no detection at all
  and reported no features, so MSVC builds ran scalar even on AVX2 hardware;
  they now resolve the same backend as GCC and Clang. `tests/test_cpu_features.cpp`
  checks the probe against `__builtin_cpu_supports` wherever that builtin
  exists, so the path MSVC depends on is exercised on every Linux and macOS run.

### Notes
- Statistical validation to 1 TB: PractRand's core battery reports no anomalies
  in 304 test results across eleven checkpoints, and TestU01 passes both
  SmallCrush (15/15) and BigCrush (all 160 statistics). Exactly one result was flagged in the whole terabyte — an `unusual`
  (PractRand's mildest severity) at the 4 GB checkpoint that never recurred at
  any larger length. Backends were shown equivalent by direct byte comparison
  over the same 1 TB rather than by repeating the battery per backend, which
  measures the same bytes four times. Write-up in
  `docs/statistical-validation.md`.
- Running the float32 stream through PractRand is not a meaningful test and was
  replaced rather than performed. Mantissa injection subtracts 1.0 from a value
  in [1,2), and that renormalisation leaves the sign bit clear, the exponent
  field geometrically skewed, and low mantissa bits frequently zero — so the
  bytes are non-uniform by construction and every correct float generator fails
  a bit-level battery on them. The failing log is archived with the explanation.
- The engine buffer overhead was profiled for issue #36 and the original
  hypothesis did not hold: the per-call bounds check is not the cost. The
  compiled `operator()` loop is already nine instructions with the cursor held
  in a register, and cursor-representation changes and larger refills all
  measured neutral or worse. The cost is a second pass over every byte — the
  kernel stores 128 bytes vectorised, then `operator()` hands them back four at
  a time. That is why the gap grew from ~10% on the scalar kernel to ~42% on
  AVX2, and why the fix is a path that skips the pass rather than a faster
  check. Callers stuck on `std::shuffle` and the standard distributions still
  pay it.
- The bit stream is unchanged. The AVX2 kernel is bit-for-bit identical to the
  scalar reference; the parity matrix and the Random123 vectors both hold.
- MSVC builds now select AVX2 too: with detection fixed and the kernel landed,
  the Windows parity matrix runs the AVX2 path for real rather than skipping it.

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
