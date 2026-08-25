# Changelog

Versions are CalVer (`YYYY.0M.MICRO`); see [VERSIONING.md](VERSIONING.md).

The **generated bit stream for a given (key, counter) is frozen permanently**.
Any entry that would change it would be a new algorithm, not a release.

## [Unreleased]

### Added
- **Portable serialized state** (`include/vphilox/serialize.hpp`), which is the
  problem the library was built around. `engine_state` records a *position* —
  key, the block holding the next output, and the word within it — never the
  engine's internals, so the format does not depend on the standard library,
  the locale, or the engine's buffer size. It is deliberately not included by
  `vphilox.hpp`, because it needs `<istream>`, `<ostream>` and `<string>` and
  most callers never serialize. `state()` / `set_state()` are the exact round
  trip; `basic_engine(g.key(), g.counter())` is not, because `counter()` is the
  *next refill's* counter and skips whatever is still buffered, and a test
  asserts that it skips.
- **AVX-512 backend** (`detail/kernel_avx512.hpp`): sixteen interleaved
  counters per `__m512i`, one counter per 32-bit lane. Measured at 4.1x the
  scalar kernel and 1.80–1.83x AVX2, with **no downclocking penalty** on either
  Sapphire Rapids or Skylake-SP. Philox's inner loop is entirely light-tier
  integer work, so it never enters a frequency licence that costs anything; see
  `docs/benchmarks/avx512-downclocking-2026-08-24.md`. That measurement also
  retired the open licence hypothesis behind issue #27.
- **ARM NEON backend** (`detail/kernel_neon.hpp`): four counters per
  `uint32x4_t` with **two independent groups interleaved per iteration**. The
  two-group unroll is not width, it is latency: the Cortex-A76 kernel was
  latency-bound rather than width-bound, so `preferred_blocks` is 8 on NEON for
  a different reason than it is 8 on AVX2. 2.19x scalar, 1.33x `std::mt19937`
  and 1.11x PCG64 on a Cortex-A76; see
  `docs/benchmarks/neon-unroll-pi-2026-08-25.md`.
- **Cross-check against NVIDIA cuRAND** (`tools/vphilox_curand_parity`,
  `docs/curand-parity.md`): 16/16 cases identical over 4096 words each, under
  all three x86 backends, together with the documented seeding mapping between
  the two libraries. cuRAND's `subsequence` is the counter's high 64 bits and
  `offset / 4` the low 64, with `offset % 4` a word index — so a cuRAND
  `offset` counts *words* where a vphilox counter counts *blocks of four*. The
  tool needs no GPU and no `nvcc`: cuRAND guards its decoration behind
  `QUALIFIERS`, so defining it compiles NVIDIA's reference implementation as
  host C++. Like the TestU01 harness, the target simply does not exist unless
  the cuRAND headers are found, and **CUDA never becomes a dependency of the
  library.**
- **Per-worker instruction counters** in `bench_scaling` (`--perf-counters`),
  attaching retired-instruction and L1 i-cache-miss counters through
  `perf_event_open` to each worker rather than to the benchmark loop's thread.
  The distinction is the whole point: the thread running the loop is the
  dispatcher, which blocks on a condition variable while the workers execute
  every instruction, so a framework's built-in counters would measure the wrong
  thread. `instructions_per_byte` is the built-in check — it is a property of
  the kernel and must not move with thread count.
- **An OpenMP arm** in `bench_scaling` alongside the `std::thread` pool, so the
  scaling result can be attributed to the generator rather than to the harness.
  Optional: without libgomp the benchmark still builds and registers half the
  rows.
- **`scripts/benchmarks/run_placement.sh`**, which runs one worker count under
  two placements and compares `icache_mpki`. It reads the sibling map from
  sysfs rather than assuming an enumeration, and **gates on a measured
  co-location penalty of at least 15% before it will run anything.** That gate
  is the reason it exists: a VM publishes a correct sibling map and accepts
  every `taskset` mask while the hypervisor places vCPUs itself, so both arms
  silently become the same placement.
- **`scripts/benchmarks/publish_results.py`**, which derives every CSV and
  figure under `docs/benchmarks/` from `results/*.json`; CI runs it with
  `--check`. Standard library only, which is also why it writes its own SVG
  *and* its own PDF rather than shelling out to a converter — both are
  byte-reproducible text, so `--check` can police them. Cross-machine figures
  plot ratios measured *within* each machine, because cycles/byte does not
  transfer across hosts, and `machines.csv` carries a `quality` column so a
  harness-validation run cannot be quoted as a result.
- **`tests/test_cross_platform_parity.cpp`**, which folds an 8191-block stream
  into an FNV-1a digest checked against a constant in the file. It covers what
  the known-answer vectors miss: SIMD tails, the refill buffer, chunked
  `generate_n`, the counter carry chain and float conversion. If it fails, the
  generated stream changed, which is a wrong change rather than a stale
  expectation.
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
- The engine's refill buffer grew from 8 blocks to 16 (128 to 256 bytes) so
  that no backend ever splits a refill, which the AVX-512 kernel's sixteen-block
  width would otherwise force. **Previously written serialized states remained
  readable**, because the format records a position rather than the engine's
  internals — that round trip is exactly the property the format was designed
  for, and the cross-platform digest test proved the stream itself was
  untouched.
- `clang-format` is pinned to 21.1.8 in CI, and the version matters as much as
  the config. clang-format is not stable across major versions: 18 (what
  `ubuntu-24.04` ships) and 21 disagree on the short block after the
  `#pragma omp parallel` in `bench_scaling.cpp`, and no source text satisfies
  both, so an unpinned check enforces the runner image rather than
  `.clang-format`. `CLANG_FORMAT_VERSION` in `.github/workflows/ci.yml` is the
  only place the number is written.
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
- **Size thread pools by physical cores, not `hardware_concurrency()`.** The
  first multi-core limit is hyperthread co-location, not the memory system: one
  thread per physical core is flat to sixteen cores, while two workers sharing
  one core cost about 35% each because the kernel is execution-port-bound.
  Memory enters only as a second limit, tracking footprint per socket. Both
  competing explanations were then excluded by measurement rather than by
  argument. Instruction supply is not the mechanism — co-location costs +59%
  cycles/byte while i-cache MPKI *falls* 36%, because both siblings run the
  identical kernel and sharing one L1i is therefore constructive
  (`docs/benchmarks/icache-placement-tigerlake-2026-08-25.md`). And the flat
  result belongs to the generator rather than to this project's worker pool —
  libgomp reproduces it at 1.000/1.000/1.003x to one thread per physical core,
  where the `std::thread` pool drifts to 1.220x because its dispatcher is an
  extra thread once the workers own every core
  (`docs/benchmarks/openmp-runtime-tigerlake-2026-08-25.md`). Frequency was
  excluded by direct measurement (`scripts/benchmarks/freq_probe.cpp`).
- **The reported ~10x scalar-Philox penalty against `std::mt19937` did not
  reproduce on any of five CPUs measured** (0.61–0.87x, not 0.10x); see
  `docs/benchmarks/baseline-2026-08-21.md`. Specialising the wide multiplies
  puts the raw kernel at 2.6–4.6x `std::mt19937` and 4.1–5.3x unspecialised
  Philox.
- **Results that cut against the library are recorded rather than omitted.**
  xoshiro256++ is faster on four of the five machines measured; it leads on the
  Pi 5, on Sapphire Rapids and on both Cascade Lake hosts, and vphilox's bulk
  path wins only on Skylake-SP (0.4210 against 0.4703 cycles per byte). It is a
  latency-bound scalar chain that a wider kernel does not catch, and it offers
  none of the properties this library exists for. Separately, on ARM the
  *buffered* engine is still 0.87x `std::mt19937` even though the NEON kernel
  leads it, because the refill drain now costs more than the kernel does;
  `generate_n` gets the full win.
- A cloud VM is fine for correctness but not for throughput, because steal time
  is invisible, and **a VM cannot control thread placement even when it reports
  the topology**. Inside WSL2 the guest publishes a correct-looking sibling map
  and `taskset` accepts the mask, but the hypervisor schedules vCPUs onto host
  cores on its own. The tell was that two workers on one core's siblings came
  out *faster* than two on separate cores, which is impossible for a port-bound
  kernel under real co-location.
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
