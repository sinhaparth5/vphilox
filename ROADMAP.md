# vphilox Roadmap

Execution plan for **vphilox**, a header-only C++20 Philox4x32-10 generator whose
state can be written on one platform and read on another, and which is fast
enough that portability costs nothing.

Those are two goals, in that order. The generator is counter-based, so its state
is a key and a position rather than a bag of internal machinery — which is what
makes a checkpoint survive a change of compiler, standard library or CPU, gives
`discard()` in constant time, and hands each thread its own substream with no
coordination. `std::mt19937` gives none of those: its serialized state is not
portable between standard libraries at all.

SIMD is the means, not the end. It exists so that nobody has to trade throughput
for those properties — the objection that sank the original attempt was
"the performance here is 1/10 of mt", and answering it is why the kernels are
here. Speed against generators that offer none of the above (xoshiro256++ in
particular) is a comparison worth *reporting* honestly, not a target to chase.

Derived from `docs/VPhilox Development Phases.md` and
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
  - [x] Measure downclocking — dispatch preferring AVX-512 is correct on both generations
        tested; see
        [`docs/benchmarks/avx512-downclocking-2026-08-24.md`](docs/benchmarks/avx512-downclocking-2026-08-24.md)
    - [x] Sapphire Rapids: 1.80x AVX2, 90% of the ideal 2x, no downclocking visible
    - [x] **Skylake-SP: 1.83x AVX2** — the part the concern was written about, and the penalty
          does not appear. Intel's frequency licences are tiered by instruction kind, not just
          width, and Philox's inner loop is entirely light-tier integer work (`vpmuludq`,
          `vpaddd`, `vpxord`, shuffles) with no floating point at all. Counter-based generators
          sidestep the AVX-512 downclocking trap by construction
    - [ ] Cascade Lake — untested, sits between the two parts measured, and very unlikely to
          differ given the light-tier argument. Not blocking
  - [x] Unexpected: **the ranking against xoshiro256++ flips between parts.** vphilox is
        fastest in the matrix on Skylake-SP (1.12x ahead) and second on Sapphire Rapids (1.27x
        behind). xoshiro is a latency-bound dependency chain that gains from a newer core;
        the vphilox kernel is throughput-bound across sixteen lanes and gains from width. The
        defensible claim is that they are within ~15% either way on AVX-512 parts
  - [x] `refill_blocks` 8 → 16 to match the kernel width. At 8 every engine refill would have
        fallen entirely into the AVX-512 scalar tail. Verified stream-identical by the
        cross-platform digest, which is what that test is for
- [x] `detail/kernel_neon.hpp` — `vmull_u32` + `vmull_high_u32` wide multiply, `vshrn` hi
      extraction. **Four** counters per register, not the two originally planned: the plan read
      `vmull_u32`'s `uint32x2_t` argument as capping the layout, but A64's `vmull_high_u32`
      reaches the upper half for one more instruction, so the whole register stays productive
  - [x] Word permutation is `vst4q_u32`, not `vtrn`/`vzip`/`vext` — the de-interleaving store
        *is* the SoA-to-AoS transpose, one instruction against AVX2's eight-step unpack and
        AVX-512's two rounds of `shuffle_i32x4`. NEON is genuinely cleaner here than x86
  - [x] Verified on real aarch64 hardware (Raspberry Pi 5, Cortex-A76), not cross-compilation
  - [x] **2.19x scalar after the two-group unroll**: 1.465 cycles/byte, against
        `std::mt19937` 1.948 and PCG64 1.625 — vphilox leads both on ARM
        (`docs/benchmarks/neon-unroll-pi-2026-08-25.md`)
    - [x] The first landing was 1.46x scalar at 2.204 c/B, which left vphilox last on ARM.
          The projection of 2.0-2.5x was arithmetically wrong — half of AVX2's 3.30x is
          ~1.65x, and the measured 1.46x was close to that
    - [x] Interleave two independent 4-lane groups (8 blocks/iteration). The diagnosis held:
          at 2.20 c/B a round cost ~14 cycles for ~18 NEON ops, ~1.3 ops/cycle against
          Cortex-A76's 2/cycle peak, so the kernel was latency-bound on the round dependency
          chain rather than width-bound. Measured 1.465 c/B, a 1.504x speedup and better than
          the 1.6-1.8 predicted. The scalar, PCG64 and xoshiro256++ control rows did not move
    - [x] **This is where the NEON optimisation stops.** At 1.465 c/B a round costs ~9.4
          cycles for the same ~18 ops, i.e. **1.92 ops/cycle against a 2/cycle peak**. The
          kernel is now issue-bound, not latency-bound; a third group has ~4% of headroom to
          chase and would cost register pressure
- [x] Attach per-kernel ISA flags to the kernel TUs only (`VPHILOX_FLAGS_AVX2`/`_AVX512` are staged in CMake), or use `[[gnu::target]]` — never `-march=native` on the whole build
  - [x] AVX2 uses `[[gnu::target("avx2")]]` via `VPHILOX_TARGET`, which is what a header-only
        kernel needs; the build stays free of ISA flags and still starts on a non-AVX2 CPU
  - [x] AVX-512 uses `VPHILOX_TARGET("avx512f,avx512dq")` the same way
  - [x] NEON needs neither: it is baseline on aarch64, so there is no target attribute and
        no runtime probe, and `VPHILOX_ARCH_ARM64` never covers armv7
- [x] Flip `implemented = true` on each kernel as it lands; dispatch picks it up automatically
  - [x] AVX2 — dispatch resolves to it, `Engine.ReportsItsBackend` reports `avx2`
  - [x] AVX-512 — verified on a Sapphire Rapids host, `Engine.ReportsItsBackend` reports `avx512`
  - [x] NEON — verified on a Raspberry Pi 5, `Engine.ReportsItsBackend` reports `neon`
- [x] Parity test harness (`tests/test_kernel_parity.cpp`) — generic over kernels, currently skipping; goes live the moment `implemented` flips
- [x] All parity tests green: bit-for-bit equality with the scalar kernel across every counter, key, and block count
  - [x] AVX2 green, including partial-vector tails and split-at-every-offset chunking
  - [x] AVX-512 green on Sapphire Rapids, first run, including tails and chunking. The
        cross-platform digest also matches under `VPHILOX_BACKEND=scalar|avx2|avx512`
  - [x] NEON green on Cortex-A76, first run, including tails and chunking

**Deliverable:** AVX2/AVX-512/NEON kernels beating `std::mt19937` single-threaded.
**Met on all three targets.** Against `std::mt19937` on the raw kernel: AVX-512 **4.63x**
(Skylake-SP) and **2.64x** (Sapphire Rapids), AVX2 **1.41x buffered / 2.42x raw** (Coffee
Lake), NEON **1.33x** (Cortex-A76). NEON was the one target that missed, at 0.89x on its
first landing; the two-group unroll closed it and also put vphilox 1.11x ahead of PCG64.

One caveat carried into Phase 4: on ARM the *buffered* engine is still 0.87x
`std::mt19937` (2.242 c/B). The kernel got 1.50x faster and the refill drain did not, so
the copy now takes 53% of that path, up from 35%. Callers get the ARM win through
`generate_n`/`generate`; closing it for `operator()` is issues #36 and #37.

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

- [x] **Portable serialized state** — the problem the library was built around. `std::mt19937`
      cannot be checkpointed across standard libraries: the format flags differ (libstdc++ has
      `operator<<` force `dec` and ignore a caller's `std::hex`; the MSVC STL does not) and so
      does the layout (libstdc++ writes 624 words in raw order plus a position, libc++ and MSVC
      write them rotated with no position). A snapshot written on Linux fails to load on
      Windows and loads *silently wrong* on macOS
  - [x] `engine_state` records a **position** — key, the block holding the next output, and the
        word within it — not the engine's internals. Writing `next_`/`cursor_` directly would
        bake `refill_blocks` into the format, and that changed from 8 to 16 when the AVX-512
        kernel landed; a state written before it would now restore to the wrong place
  - [x] `state()` / `set_state()` round-trip exactly, at every cursor offset across several
        refills and through the bulk path. Note `basic_engine(g.key(), g.counter())` does
        **not**: `counter()` is the next refill's counter, so it skips whatever is buffered.
        A test asserts that it skips, so the day `counter()` changes meaning something says so
  - [x] `serialize.hpp` — text form that never touches the locale (digits written and parsed by
        hand, never `num_put`/`num_get`), carries a version tag, and refuses what it does not
        recognise: wrong tag, wrong field count, value past 2^32-1, offset outside a block,
        doubled or missing separator, trailing bytes. A failed read sets `failbit` and leaves
        the engine untouched. Not pulled in by `vphilox.hpp`, since it needs `<istream>`,
        `<ostream>` and `<string>` and most callers never serialize
  - [x] `counter_sub` / `counter_retreated` — the arithmetic recovering a caller-visible
        position needs, since the engine runs ahead of it by whatever is buffered
  - [x] **Agrees with C++26's `std::philox_engine`** ([rand.eng.philox]).
        `vphilox::engine{20111115}` — the standard's `default_seed` — produces **1955073260**
        at the 10000th invocation, which is what `[rand.predef]` requires of a
        default-constructed `std::philox4x32`. That pins the whole engine layer: the
        seed-to-key mapping, the counter start, and the order the four words of a block come
        out in. `tests/test_std_philox_parity.cpp` holds it on any toolchain, and compiles in
        a direct whole-stream comparison against `std::philox4x32` wherever
        `__cpp_lib_philox_engine` is defined (not yet in libstdc++ 15).
        This settles what the library is *for*: vphilox is an implementation of the standard
        engine with SIMD kernels, not a competitor to it, and `engine` can become an alias
        once libraries ship the type
  - [x] **Cross-machine restore proved by fixture, not by argument.** A state string written
        on x86-64 is checked into `tests/test_serialization.cpp` with a digest of the 4096
        outputs that must follow. Any machine loading it has to produce the same stream, so
        CI re-proves the portability claim on Linux, macOS arm64 and Windows MSVC on every
        commit. The position is deliberately awkward — past 2^33 outputs, mid-block, key using
        both words — so a restore that rounded to a block boundary or dropped the high counter
        word would fail there and pass on a tidier one

**Deliverable:** a header-only C++20 library that drops straight into standard algorithms,
and whose state can be written on one platform and read on another.

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

- [ ] **Paper** (LaTeX, ACM template) — working title *Portable, Seekable Random Streams for
      Parallel CPU Simulation*, retitled from *vPhilox: SIMD-Accelerated Counter-Based
      Pseudo-Random Number Generation for Parallel CPU Systems*.

      The original title names the method, not the contribution, and it invites exactly the
      comparison the measurements lose: xoshiro256++ is faster on three of the four parts
      tested. Speed is the *objection this work removes*, not the result it reports. Lead with
      what no competing generator offers — a checkpoint that survives a change of standard
      library, O(1) seek, and results independent of thread count — and use the throughput
      figures to show those properties are free.
  - [ ] **The portability failure** — the motivating problem, and currently missing from the
        outline entirely. `std::mt19937`'s serialized state is not portable: format flags
        differ (libstdc++ has `operator<<` force `dec`; the MSVC STL does not) and so does the
        layout (624 words raw plus a position, against 624 rotated with none). A checkpoint
        written on Linux fails on Windows and loads *silently wrong* on macOS. Counter-based
        state — a key and a position — cannot fail this way
  - [ ] **Reproducibility under parallelism** — identical output regardless of thread count and
        scheduling, with the measured scaling behind it (3.95x on 4 cores, 98.8% efficiency,
        aggregate cycles/byte flat to 0.063% from 1 to 32 threads)
  - [ ] Scalar wide-multiply bottleneck analysis — and the finding that the reported ~10x
        penalty against `std::mt19937` did **not** reproduce on any of five CPUs measured
        (0.61-0.87x, not 0.10x)
  - [ ] AVX2/AVX-512/NEON lane interleaving theory, including why AVX-512 shows no
        downclocking penalty here: Philox's inner loop is entirely light-tier integer work
  - [ ] IEEE-754 mantissa injection math
  - [ ] Benchmark figures from Phase 4, reported honestly — including the two results that cut
        against us, that xoshiro256++ leads on three of four parts and that NEON leaves vphilox
        behind `std::mt19937` on ARM
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
