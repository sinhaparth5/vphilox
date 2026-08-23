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
- [ ] `detail/kernel_avx512.hpp` — `__m512i`, 8 counters per register
  - [ ] `_mm512_mul_epu32`; `_mm512_permutexvar_epi32` for the permutation
  - [ ] Gate on `avx512f` **and** `avx512dq`
  - [ ] Measure downclocking — do not let dispatch prefer AVX-512 until it demonstrably beats AVX2 on the same part
- [ ] `detail/kernel_neon.hpp` — `vmull_u32` wide multiply, `vshrn_n_u64` hi extraction
  - [ ] `vtrn`/`vzip`/`vext` for the word permutation
  - [ ] Verify on real aarch64 hardware, not just cross-compilation
- [~] Attach per-kernel ISA flags to the kernel TUs only (`VPHILOX_FLAGS_AVX2`/`_AVX512` are staged in CMake), or use `[[gnu::target]]` — never `-march=native` on the whole build
  - [x] AVX2 uses `[[gnu::target("avx2")]]` via `VPHILOX_TARGET`, which is what a header-only
        kernel needs; the build stays free of ISA flags and still starts on a non-AVX2 CPU
  - [ ] Same treatment for AVX-512 and NEON when those kernels land
- [~] Flip `implemented = true` on each kernel as it lands; dispatch picks it up automatically
  - [x] AVX2 — dispatch resolves to it, `Engine.ReportsItsBackend` reports `avx2`
  - [ ] AVX-512, NEON
- [x] Parity test harness (`tests/test_kernel_parity.cpp`) — generic over kernels, currently skipping; goes live the moment `implemented` flips
- [~] All parity tests green: bit-for-bit equality with the scalar kernel across every counter, key, and block count
  - [x] AVX2 green, including partial-vector tails and split-at-every-offset chunking
  - [ ] AVX-512, NEON — still skipping until those kernels are implemented

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
- [x] `detail/cpu_features.hpp` — `__builtin_cpu_supports` probe, computed once
- [x] `detail/dispatch.hpp` — resolves to the fastest kernel that is compiled in **and** implemented **and** CPU-supported
- [x] `VPHILOX_BACKEND` env override for pinning a backend in tests and benchmarks
- [ ] SIMD float conversion — `_mm256_or_si256` + `_mm256_sub_ps` and the AVX-512/NEON equivalents, keeping conversion inside vector registers
- [ ] MSVC CPU detection (`__cpuidex` leaf 7 + `XGETBV` OS-state check) — currently stubbed to `false`
- [ ] **Close the buffer overhead gap** — measured at ~22% (954 MiB/s buffered vs 1.231 GiB/s bulk).
      Phase 3's zero-cost-abstraction claim does not hold yet; profile the per-call bounds check
- [ ] Bulk generation API (`generate_n` / span-filling) so callers can bypass the buffer entirely
- [ ] `SeedSeq` constructor for drop-in compatibility with existing `std::mt19937` call sites

**Deliverable:** a header-only C++20 library that drops straight into standard algorithms.

---

## Phase 4 — Empirical benchmarking & statistical testing

**Goal:** evidence, not claims.

- [x] `tools/vphilox_stream.cpp` — raw32/float32 output on stdout with `--seed`, `--bytes`, `--backend`
- [ ] **PractRand**
  - [ ] Smoke run: `./vphilox_stream | RNG_test stdin32 -tlmax 1GB`
  - [ ] Full run to 1 TB with `-tf 1 -multithreaded`
  - [ ] Repeat per backend (`--backend scalar|avx2|avx512`) — SIMD interleaving must not perturb statistics
  - [ ] Test the float32 stream separately; mantissa injection discards low bits and deserves its own run
  - [ ] Archive the pass logs in `docs/statistical/`
- [ ] **TestU01 BigCrush** — full battery, zero failures
- [ ] **Throughput matrix**
  - [ ] `std::mt19937`, scalar Philox4x32, xoshiro256++, PCG64, vphilox (each backend)
  - [ ] Vendor xoshiro256++ and PCG64 into `benchmarks/third_party/` rather than taking dependencies
  - [ ] Report GB/s **and** cycles/byte
- [ ] **Scaling** — `benchmarks/bench_scaling.cpp` at 1/2/4/8/16/32 threads
  - [ ] Confirm linear scaling; investigate any knee (memory bandwidth vs false sharing)
  - [ ] OpenMP variant alongside the `std::thread` one
  - [ ] Instruction-cache miss rates via libpfm
- [ ] **Cross-platform bit parity** — same key and counter produce identical output on x86-64, aarch64, and (if reachable) a cuRAND/GPU reference. This is the reproducibility claim; it needs a test, not an assertion.
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
