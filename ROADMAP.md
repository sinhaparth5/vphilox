# vphilox Roadmap

Execution plan for **vphilox**, a header-only C++20 SIMD-vectorized Philox4x32-10
generator. Derived from `docs/VPhilox Development Phases.md` and
`docs/Vector Philox Development Strategy.md`.

**Status:** Phase 0 complete, Phase 1 substantially complete. Current version
`2026.08.0` (see [VERSIONING.md](VERSIONING.md)).

Legend: `[x]` done · `[ ]` open · `[~]` partially done, detail in the sub-items

---

## Phase 0 — Project scaffolding

Not in the original five-phase plan, but nothing else can start without it.

- [x] Git repository, dual-licensed MIT OR Apache-2.0 (`LICENSE-MIT` + `LICENSE-APACHE`)
- [x] CMake 3.24+ build, `vphilox::vphilox` INTERFACE target, C++20
- [x] CalVer versioning: `VERSION` file → `cmake/VphiloxVersion.cmake` → generated `vphilox/version.hpp`
- [x] Header layout under `include/vphilox/`, kernels in `detail/`
- [x] Install + export rules; `find_package(vphilox)` verified from a downstream project
- [x] GoogleTest / Google Benchmark wired via `find_package` with `FetchContent` fallback
- [x] `CMakePresets.json` for the standard configure/build/test cycle
- [x] GitHub Actions CI (build + test matrix)
- [x] `.clang-format`, `CONTRIBUTING.md`, `CHANGELOG.md`, `CITATION.cff`
- [x] License story settled — dual MIT/Apache-2.0 per the docs; SPDX headers on every source file
- [ ] README badges once CI has run at least once on the remote

---

## Phase 1 — Foundation & reference scalar engine

**Goal:** a zero-dependency scalar Philox4x32-10 establishing ground truth,
tests, and the benchmark baseline.

- [x] `detail/kernel_scalar.hpp` — `mulhilo32`, `philox_round`, `bump_key`, `philox4x32<R>`
- [x] Wide multiply via explicit 64-bit cast (`(u64)a * b`, split hi/lo)
- [x] `constants.hpp` — multipliers `M0/M1`, Weyl increments `W0/W1`, `counter4`, `key2`
- [x] `counter.hpp` — 128-bit little-endian counter arithmetic with carry propagation
- [x] Whole core is `constexpr`; verified by a `static_assert` on a KAT value
- [x] Batched kernel entry point (`kernel_scalar::generate`) with the shared kernel contract
- [x] **Known-answer tests against the Random123 `kat_vectors`** — all-zeros, all-ones, and
      digits-of-pi inputs all match bit for bit (`tests/test_reference_vectors.cpp`)
- [x] Counter arithmetic tests including the seek-equals-step property
- [x] Baseline micro-benchmarking
  - [x] `benchmarks/bench_engines.cpp` comparing vphilox / bulk kernel / `std::mt19937` / scalar Philox
  - [x] First run recorded in [`docs/benchmarks/baseline-2026-08-21.md`](docs/benchmarks/baseline-2026-08-21.md)
  - [x] **The ~10× scalar-Philox slowdown did not reproduce** — on Coffee Lake / GCC 15 scalar
        Philox runs at 0.96× of `std::mt19937`; on Raspberry Pi 5 / GCC 12 it runs at 0.61×,
        and on an Ice Lake AVX-512 host / GCC 13 it runs at 0.61×, not 0.1×. Treat the
        approximately 10× slowdown as reported context rather than a project result. See the
        baseline documents for the analysis.
  - [x] Add a cycles-per-byte counter (serialized `RDTSC` on x86 or Linux
        `perf_event_open` hardware cycles on ARM); keep GB/s as the secondary metric
  - [x] Re-run on native, frequency-pinned hardware; the Raspberry Pi 5 run used the
        performance governor, affinity to CPU 3, and reported sub-1% cycles/byte CV

**Deliverable:** a passing scalar baseline that reproduces Salmon et al.'s
published vectors and quantifies the CPU penalty vphilox exists to remove.

---

## Phase 2 — AVX2 / AVX-512 / NEON SIMD kernels

**Goal:** kill the scalar multiply bottleneck by interleaving N independent
Philox counters across SIMD lanes.

- [x] **Settle the lane layout** — 8 counters/register wins; see
      `docs/benchmarks/simd-lane-layout.md`
  - [x] Benchmark 4 counters/register (one per 64-bit lane, half the register idle on non-multiply ops)
  - [x] Benchmark 8 counters/register (full width, two `_mm256_mul_epu32` per multiply + even/odd shuffles)
  - [x] Commit to the winner and set `preferred_blocks` on each kernel to match
- [x] `detail/kernel_avx2.hpp` — real implementation replacing the scalar fallthrough;
      3.30x the scalar kernel, see [`docs/benchmarks/avx2-2026-08-23.md`](docs/benchmarks/avx2-2026-08-23.md)
  - [x] SoA load: one `__m256i` per counter word, with the lane carry chain done in-register
  - [x] Wide multiply via `_mm256_mul_epu32`, split across even and odd 32-bit lanes
  - [x] `_mm256_srli_epi64` (hi extraction), `_mm256_xor_si256` (key XOR)
  - [x] Word permutation via `_mm256_blend_epi32` to repack the split products
  - [x] Weyl key bump via `_mm256_add_epi32`, keys held broadcast across rounds
  - [x] Store path back to AoS block order (unpack/permute transpose, unaligned stores)
  - [x] Tail handling for block counts that are not a multiple of the SIMD width, with
        chunking independence asserted directly in `tests/test_kernel_parity.cpp`
- [x] `detail/kernel_avx512.hpp` — `__m512i`, **16** counters per register (not the 8 the stub
      advertised: 512/32 is sixteen 32-bit lanes, and issue #12's argument for full lane width
      does not change with a wider register). 4.11x scalar, 1.80x AVX2 — see
      [`docs/benchmarks/avx512-sapphire-rapids-2026-08-24.md`](docs/benchmarks/avx512-sapphire-rapids-2026-08-24.md)
  - [x] `_mm512_mul_epu32`, with `_mm512_shuffle_i32x4` for the store transpose rather than
        `permutexvar` — the SoA-to-AoS step is a 128-bit lane gather, not an element permute
  - [x] Gate on `avx512f` **and** `avx512dq`, matching the existing `detect_cpu()` probe. Only
        F instructions are used; DQ is carried because the probe requires it
  - [~] Measure downclocking — do not let dispatch prefer AVX-512 until it demonstrably beats
        AVX2 on the same part
    - [x] Sapphire Rapids: 1.80x AVX2, 90% of the ideal 2x, no downclocking visible. Dispatch
          preferring it is correct **on this part**
    - [ ] Skylake-SP / Cascade Lake, where the frequency penalty was the original concern —
          untested, and the reason this item is not closed
  - [x] `refill_blocks` 8 → 16 to match the kernel width. At 8 every engine refill would have
        fallen entirely into the AVX-512 scalar tail. Verified stream-identical by the
        cross-platform digest, which is what that test is for
- [ ] `detail/kernel_neon.hpp` — `vmull_u32` wide multiply, `vshrn_n_u64` hi extraction
  **Parked 2026-08-24**, pending access to the aarch64 development machine. Not blocked on
  anything in the repo: the pre-NEON baseline it has to improve on is pinned at 3.194
  cycles/byte in
  [`docs/benchmarks/throughput-matrix-pi-2026-08-24.md`](docs/benchmarks/throughput-matrix-pi-2026-08-24.md),
  and the Pi 5 can verify a finished kernel even though it is not the box to write one on.
  Go in expecting NEON to land near PCG64 and to stay 2.3-2.9x behind xoshiro256++ — half of
  AVX2's 3.30x on half the vector width. That is worth doing for the ARM users, but it is
  not a route to winning the throughput matrix.
  - [ ] `vtrn`/`vzip`/`vext` for the word permutation
  - [ ] Verify on real aarch64 hardware, not just cross-compilation
- [~] Attach per-kernel ISA flags to the kernel TUs only (`VPHILOX_FLAGS_AVX2`/`_AVX512` are staged in CMake), or use `[[gnu::target]]` — never `-march=native` on the whole build
  - [x] AVX2 uses `[[gnu::target("avx2")]]` via `VPHILOX_TARGET`, which is what a header-only
        kernel needs; the build stays free of ISA flags and still starts on a non-AVX2 CPU
  - [x] AVX-512 uses `VPHILOX_TARGET("avx512f,avx512dq")` the same way
  - [ ] Same treatment for NEON when that kernel lands
- [~] Flip `implemented = true` on each kernel as it lands; dispatch picks it up automatically
  - [x] AVX2 — dispatch resolves to it, `Engine.ReportsItsBackend` reports `avx2`
  - [x] AVX-512 — verified on a Sapphire Rapids host, `Engine.ReportsItsBackend` reports `avx512`
  - [ ] NEON
- [x] Parity test harness (`tests/test_kernel_parity.cpp`) — generic over kernels, currently skipping; goes live the moment `implemented` flips
- [~] All parity tests green: bit-for-bit equality with the scalar kernel across every counter, key, and block count
  - [x] AVX2 green, including partial-vector tails and split-at-every-offset chunking
  - [x] AVX-512 green on Sapphire Rapids, first run, including tails and chunking. The
        cross-platform digest also matches under `VPHILOX_BACKEND=scalar|avx2|avx512`
  - [ ] NEON — still skipping until that kernel is implemented

**Deliverable:** AVX2/AVX-512/NEON kernels beating `std::mt19937` single-threaded.
AVX2 clears it: the buffered engine runs at 1.41x `std::mt19937`, the raw kernel at
2.42x. AVX-512 and NEON are still outstanding.

---

## Phase 3 — Fast float conversion & C++20 interface

**Goal:** single-cycle int→float conversion and a standard-conforming wrapper.

- [x] `float_cast.hpp` — scalar mantissa injection for `float` (`0x3F800000 | (u >> 9)`) and `double`
- [x] Range, monotonicity, low-bit-discard, and coarse-uniformity tests
- [x] `philox.hpp` — `basic_engine<Rounds>` satisfying `std::uniform_random_bit_generator` (enforced by `static_assert`)
- [x] `alignas(64)` refill buffer, 8 blocks / 128 bytes — one AVX-512 iteration, two AVX2 iterations, so no backend splits a refill
- [x] O(1) `discard(n)` via counter arithmetic; tested against repeated calls and on a 2^40 jump
- [x] Verified against `std::uniform_real_distribution`, `std::normal_distribution`, `std::shuffle`
- [x] `detail/cpu_features.hpp` — raw CPUID + `XGETBV` probe, one shared x86 path, computed once
- [x] `detail/dispatch.hpp` — resolves to the fastest kernel that is compiled in **and** implemented **and** CPU-supported
- [x] `VPHILOX_BACKEND` env override for pinning a backend in tests and benchmarks
- [~] SIMD float conversion — `_mm256_or_si256` + `_mm256_sub_ps` and the AVX-512/NEON equivalents, keeping conversion inside vector registers
  - [x] **No intrinsics needed.** GCC already emits `vpsrld`/`vpor`/`vaddps` for the plain
        `to_float01` loop; hand-writing them would be transcription and would freeze the width
        at 8 where the loop widens on its own. See
        [`docs/benchmarks/float-conversion-2026-08-24.md`](docs/benchmarks/float-conversion-2026-08-24.md)
  - [x] The real gap was the ISA, not the instructions: header-only means the loop compiles
        with the *consumer's* flags, so `detail/float_bulk.hpp` compiles it twice — baseline and
        `VPHILOX_TARGET("avx2")` — and selects at runtime like the kernels do
  - [x] Bulk float API to consume it: `generate_n(float*, n)` / `generate(std::span<float>)`,
        equivalent to the same number of `next_float()` calls, ~2x that path
  - [ ] AVX-512 / NEON variants — blocked on #24 and #28, not on hardware access
  - [ ] Decide whether to fuse conversion into the kernels and remove the second pass.
        Needs a frequency-pinned host: this laptop put the cost anywhere between 10% and 26%
- [x] MSVC CPU detection (`__cpuidex` leaf 7 + `XGETBV` OS-state check) — no longer stubbed to `false`.
      The CPUID probe is now the single x86 path on every compiler, so Linux and macOS runs
      exercise the same code MSVC depends on; `tests/test_cpu_features.cpp` checks it against
      `__builtin_cpu_supports` wherever that builtin exists
- [x] **Close the buffer overhead gap** — profiled and closed for bulk callers; see
      [`docs/benchmarks/buffer-overhead-2026-08-23.md`](docs/benchmarks/buffer-overhead-2026-08-23.md)
  - [x] The per-call bounds check was **not** the cost. The generated loop is already
        nine instructions with the cursor in a register, and pointer cursors, 32-bit
        cursors, `noinline`+`cold` refills, and refills from 8 up to 128 blocks all
        measured neutral or worse
  - [x] The real cost is a second pass over every byte: the kernel writes 128 bytes with
        vector stores and `operator()` hands them back four at a time. Draining a buffer
        that never refills already costs 1.09 cycles/byte against a kernel that produces
        the same data at 1.06 — which is why the gap grew from ~10% on scalar to ~42% on AVX2
  - [x] `generate_n` removes the pass rather than tuning it: 100.0% of the raw kernel,
        1.72x the buffered engine, 2.40x `std::mt19937`
  - [ ] Still open by construction for `std::shuffle` and the standard distributions,
        which consume one word at a time and cannot be given a bulk path
- [x] Bulk generation API — `engine::generate_n(u32*, n)` and `engine::generate(std::span<u32>)`,
      producing exactly what the same number of `operator()` calls would, so the two can be
      mixed on one engine
  - [x] Goes direct to the kernel only when the request is at least `preferred_blocks` wide;
        a narrower one would run entirely in the kernel's scalar tail and come out ~3x slower
        than the buffer it was avoiding
  - [x] `tools/vphilox_stream` converted to it — 1.80x on AVX2, byte-identical output
- [x] `SeedSeq` constructor for drop-in compatibility with existing `std::mt19937` call sites
  - [x] `basic_engine(Sseq&)` plus `seed(Sseq&)` and `seed(u64)`; key only, counter starts at 0
  - [x] Constrained to real seed sequences — unconstrained, the template accepts any non-const
        lvalue and `std::is_constructible_v<engine, Widget&>` answers true, with the mismatch
        surfacing only inside the constructor body (`tests/test_seeding.cpp`)

**Deliverable:** a header-only C++20 library that drops straight into standard algorithms.

---

## Phase 4 — Empirical benchmarking & statistical testing

**Goal:** evidence, not claims.

- [x] `tools/vphilox_stream.cpp` — raw32/float32 output on stdout with `--seed`, `--bytes`, `--backend`
- [x] **PractRand** — see `docs/statistical-validation.md`
  - [x] Smoke run: `./vphilox_stream | RNG_test stdin32 -tlmax 1GB` — no anomalies in 194 results
  - [x] Full run to 1 TB with `-tf 1 -multithreaded` — no anomalies in 304 results, 11 clean
        checkpoints. One `unusual` at 4 GB (p≈0.9998) that never recurred.
  - [x] Repeat per backend — settled by proving the streams are *byte-identical* over the same
        1 TB (`cmp`, both hashing to `cffff568…`) rather than by four batteries over the same
        bytes. Stronger and a quarter the cost. AVX-512 still open: dispatch correctly refuses
        the stub, so `--backend avx512` resolves to avx2 until #24 lands.
  - [x] float32 stream — the literal test is not meaningful and was replaced. Raw IEEE-754 bits
        are non-uniform *by construction* (clear sign, skewed exponent, zero-filled low mantissa
        after renormalisation), so every correct float generator fails it; ours fails 114 tests.
        Replaced by an exhaustive round-trip over all 2^23 outputs plus chi-square and KS
        uniformity tests. The failing log is archived with the explanation.
  - [x] Archive the pass logs — in `results/practrand/`, each with a provenance header
- [x] **TestU01 BigCrush** — full battery, zero failures
  - [x] Harness: `tools/vphilox_testu01`, driving `vphilox::engine` directly rather than a byte
        stream, so the refill buffer and dispatch are under test too
  - [x] SmallCrush — 15/15
  - [x] BigCrush — all 160 statistics passed, 2h31m. One sub-statistic
        (`sknuth_MaxOft` chi2, p=0.9993) carries TestU01's suspect marker; it is not among the
        160 scored statistics, and over 254 computed p-values the expected count outside the
        band is 0.51, so one is unremarkable.
- [~] **Throughput matrix**
  - [x] `std::mt19937`, scalar Philox4x32, xoshiro256++, PCG64, vphilox (each backend, pinned
        with `VPHILOX_BACKEND`) — all wired into `bench_engines.cpp`. Every row generates the
        same 256 KiB rather than the same number of calls, so the 64-bit generators are not
        handed twice the output for the same loop count
  - [x] Vendor xoshiro256++ and PCG64 into `benchmarks/third_party/` rather than taking dependencies
    - [x] Byte-for-byte upstream with SHA-256s recorded; vendored files are exempt from
          clang-format and the SPDX convention, since reformatting them would void the hashes
    - [x] xoshiro needs a wrapper (upstream state is file-scope statics, unusable for the
          matrix or for one generator per thread) — pinned to the vendored C by a
          side-by-side test rather than trusted
  - [x] Report GB/s **and** cycles/byte — every row reports both through `report_metrics`
  - [~] Run the matrix on frequency-pinned hardware and write it up, via
        `scripts/benchmarks/run_matrix.sh`, which pins the governor, records provenance, and
        refuses a run that lost the cycle counter or came in above 1% CV
    - [x] **AArch64 — Raspberry Pi 5**, every row inside the sub-1% CV bar; see
          [`docs/benchmarks/throughput-matrix-pi-2026-08-24.md`](docs/benchmarks/throughput-matrix-pi-2026-08-24.md).
          vphilox runs the scalar kernel there until #28 lands and so places last:
          xoshiro256++ is 5.84x it, PCG64 1.97x, `std::mt19937` 1.67x. The four rows shared
          with the August baseline reproduce within 2% (the two pure-kernel rows within
          0.1%), and the refill buffer costs 25.2% with `generate_n` recovering 100.0% of the
          kernel — the x86 bulk-path result from #37 holding on a second architecture
    - [ ] **x86-64 AVX2** — the laptop run that validated the harness sits at 4-10% CV, so
          this column still needs a pinned host
- [~] **Scaling** — `benchmarks/bench_scaling.cpp` at 1/2/4/8/16/32 threads
  - [x] Harness rebuilt to be interpretable: aggregate cycles/byte (flat = linear scaling),
        a `generate_n` arm beside the buffered one, the working set as a second axis so an
        L2-resident curve and a DRAM-resident one can be told apart, and a persistent worker
        pool so thread creation is no longer inside `bytes_per_second`
  - [x] Confirm linear scaling — **done on the Pi 5**, see
        [`docs/benchmarks/scaling-pi-2026-08-24.md`](docs/benchmarks/scaling-pi-2026-08-24.md).
        4 threads on 4 cores gives 3.95x at 98.8% efficiency, and aggregate cycles/byte
        spans 0.063% across every thread count from 1 to 32 and both working sets. Beyond
        the core count throughput plateaus while cost per byte still does not move, which is
        what zero shared state should look like. The bulk path is 28.9% cheaper by a
        constant margin — the same refill-buffer overhead the matrix put at 25.2%,
        reproduced by a second benchmark
    - [x] **Corrects the laptop reading.** Coffee Lake suggested the buffered arm degraded
          from 2 threads; on a pinned clock with no SMT both arms scale within 0.24%. RDTSC
          counts reference cycles, so a moving turbo ceiling landed in the column, and 8
          threads on 4C/8T is hyperthreading rather than scaling
  - [~] Investigate the knee (memory bandwidth vs false sharing)
    - [x] No knee on ARM, and that is about the kernel not the memory system: 3.19
          cycles/byte over 4 cores is ~3 GB/s, far under what LPDDR4X supplies, so the 4 MiB
          arm could not provoke one. Coffee Lake with AVX2 degrades 6.66x by 32 threads at
          the same working set. This axis starts discriminating on ARM once #28 lands
    - [ ] Locate the knee on a pinned x86 host with the AVX2 kernel
  - [ ] OpenMP variant alongside the `std::thread` one
  - [ ] Instruction-cache miss rates via libpfm
- [~] **Cross-platform bit parity** — same key and counter produce identical output on x86-64,
      aarch64, and (if reachable) a cuRAND/GPU reference. This is the reproducibility claim;
      it needs a test, not an assertion.
  - [x] `tests/test_cross_platform_parity.cpp` folds an 8191-block stream into an FNV-1a
        digest checked against a constant in the file, covering the paths the KAT vectors
        miss: SIMD tails, the refill buffer, chunked `generate_n`, the counter carry chain,
        and float conversion. Verified identical under `VPHILOX_BACKEND=scalar|avx2` and
        under `VPHILOX_FORCE_SCALAR`, and the constant itself was checked against an
        independent FNV implementation rather than recorded from the code it tests
  - [ ] Confirm the same digest on real aarch64 — the test ships, it just has not been run
        on the Pi yet
  - [ ] cuRAND/GPU reference, if reachable
- [ ] Publish plots and raw CSVs under `docs/benchmarks/`

**Deliverable:** a complete benchmark dataset and statistical pass logs.

---

## Phase 5 — Paper & release

- [ ] **Paper** (LaTeX, ACM template) — *vPhilox: SIMD-Accelerated Counter-Based Pseudo-Random Number Generation for Parallel CPU Systems*
  - [ ] Scalar wide-multiply bottleneck analysis
  - [ ] AVX2/AVX-512 lane interleaving theory
  - [ ] IEEE-754 mantissa injection math
  - [ ] Benchmark figures from Phase 4
- [ ] arXiv preprint (cs.PF / cs.MS)
- [ ] Zenodo DOI for the repository
- [ ] Tag and publish `sinhaparth5/vphilox`
- [x] `CITATION.cff` for one-click BibTeX
- [ ] Update `CITATION.cff` with the DOI once Zenodo assigns one
- [ ] Package for vcpkg and Conan
- [ ] Release notes in `CHANGELOG.md`

---

## Out of scope (for now)

Recorded so these get decided deliberately rather than by drift:

- **Philox4x64** — the 64-bit variant. Different constants, different lane
  arithmetic, roughly a second kernel family.
- **SVE / SVE2** — ARM's scalable vectors. Worth revisiting after NEON ships.
- **GPU backends** — cuRAND and rocRAND already occupy this space well; vphilox
  is the CPU-side counterpart that stays bit-compatible with them.
- **Threefry / ARS** — the other Random123 families.
- **A C API** — would broaden reach (Python, Rust, Julia bindings) but is a
  separate surface to maintain.
