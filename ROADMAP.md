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

**Status:** Phases 0-3 complete; **Phase 4 complete.** The instruction-cache
study is measured — a bare-metal Tiger Lake laptop cleared the co-location gate
that no earlier host could — and the cuRAND cross-check is done without needing
a GPU at all. **Phase 5 is under way:** the paper in `paper/` is a complete
draft with every Phase 4 result folded in, and the changelog covers Phases 2
through 4. What is left needs things this repository cannot produce on its own:
the XGBoost thread URL, an affiliation, a tag, a Zenodo DOI, a TechRxiv
preprint, an arXiv endorsement, and vcpkg/Conan packaging. The route through
those services, and the reason for their order, is in
[`docs/publishing-guide.md`](docs/publishing-guide.md).

Current version
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
- [x] README badges once CI has run at least once on the remote

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
  - [x] Measure downclocking — dispatch preferring AVX-512 is correct on all three
        generations tested; see
        [`docs/benchmarks/avx512-downclocking-2026-08-24.md`](docs/benchmarks/avx512-downclocking-2026-08-24.md)
    - [x] Sapphire Rapids: 1.80x AVX2, 90% of the ideal 2x, no downclocking visible
    - [x] **Skylake-SP: 1.83x AVX2** — the part the concern was written about, and the penalty
          does not appear. Intel's frequency licences are tiered by instruction kind, not just
          width, and Philox's inner loop is entirely light-tier integer work (`vpmuludq`,
          `vpaddd`, `vpxord`, shuffles) with no floating point at all. Counter-based generators
          sidestep the AVX-512 downclocking trap by construction
    - [x] **Cascade Lake: 1.56x AVX2** — measured 2026-08-25, and the prediction above was
          wrong. It converts 78% of the doubled lane width where the other two convert ~90%,
          and the shortfall is confined to the 512-bit path: its AVX2 kernel is 2.83x scalar,
          identical to Skylake-SP's. A light-tier licence drop fits — that instance is
          nominally 2.80 GHz against Skylake-SP's 2.00 GHz, so it has clock to lose — but GCP
          exposes no vPMU, `cycles`/`ref-cycles` both report `<not supported>`, and the one
          measurement that would settle it could not be taken. Recorded as a hypothesis.
          **Dispatch is unchanged**: 1.56x AVX2 and 4.42x scalar, so preferring AVX-512 is
          still right on every part measured. See
          [`docs/benchmarks/avx512-cascade-lake-2026-08-25.md`](docs/benchmarks/avx512-cascade-lake-2026-08-25.md)
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
  - [x] AVX-512 variant — a third clone under `VPHILOX_TARGET("avx512f")`. Verified to emit
        the sixteen-lane form (`vpsrld $9 / vpord / vaddps / vmovups %zmm`) on GCC 15, which
        is worth checking because vector width is a *tune* decision and a target attribute
        does not change the tune. `avx512f` alone suffices — the `dq` subset the kernel needs
        is not required for a shift, an or and a subtract
  - [x] No NEON variant, and that is the answer rather than a gap: NEON is baseline on
        aarch64, so the consumer's own translation unit already compiles the plain loop to
        `ushr`/`orr`/`fsub`. The ISA gap this file exists to close does not exist there
  - [x] The converter is now **derived from the resolved backend** instead of repeating the
        CPU probe, so the two cannot disagree. That also fixed a latent inconsistency:
        `VPHILOX_BACKEND=neon` on x86 gave the AVX2 kernel and the *baseline* converter
  - [x] Measured on Sapphire Rapids
        ([`float-conversion-widths-2026-08-25.md`](docs/benchmarks/float-conversion-widths-2026-08-25.md)).
        At `float_tile_words` — the 256-word chunk `generate_n` actually converts — AVX-512
        is **1.89x** the baseline conversion and 1.68x AVX2; at a 32 MiB footprint, 3.19x
        and 1.76x. In between, at a 512 KiB L2-resident footprint, all three sit within 8%:
        that regime is bandwidth-bound and the width is irrelevant there
    - [x] The conversion is a small term either way — 0.039 cycles/byte against an AVX-512
          kernel at roughly 0.25. The case for the variant is that it costs one attribute
          and a branch already being taken, not that it moves the headline
    - [x] A GCC 12 consumer at `-O2` gets **scalar** conversion from every variant: the
          target attribute pins the ISA but not the vectoriser, and GCC 12's `-O2` cost
          model rejects this loop. `-O3` gives `%ymm`/`%zmm` as intended, and GCC 15
          vectorises at `-O2`. Header-only means the consumer's flags decide
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
        bytes. Stronger and a quarter the cost. The AVX-512 kernel (#24) has since landed and
        is covered by the same byte-identity argument: `tests/test_kernel_parity.cpp` and
        `test_cross_platform_parity.cpp` hold it to the scalar stream, so a fifth battery
        over the same bytes would add nothing.
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
- [x] **Throughput matrix**
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
  - [x] Run the matrix on frequency-pinned hardware and write it up, via
        `scripts/benchmarks/run_matrix.sh`, which pins the governor, records provenance, and
        refuses a run that lost the cycle counter or came in above 1% CV
    - [x] **AArch64 — Raspberry Pi 5**, run twice. The pre-NEON column is
          [`throughput-matrix-pi-2026-08-24.md`](docs/benchmarks/throughput-matrix-pi-2026-08-24.md)
          and is superseded for ranking; the current one is
          [`neon-unroll-pi-2026-08-25.md`](docs/benchmarks/neon-unroll-pi-2026-08-25.md).
          With the NEON kernel and its two-group unroll, `generate_n` is **1.4652
          cycles/byte** — 2.19x the scalar kernel, 1.33x `std::mt19937`, 1.11x PCG64 —
          and xoshiro256++ still leads it by 2.68x. Every row is inside the sub-1% CV bar
          except `BM_mt19937` at 1.21%, which is the row vphilox is being compared
          *against*, so the noise does not flatter the result. What the pre-NEON run
          established still holds: the refill buffer costs 25.2% of bulk throughput and
          `generate_n` recovers 100.0% of the kernel, the x86 bulk-path result from #37
          reproducing on a second architecture
    - [x] **x86-64, all three backends** — the laptop run that validated the harness sat
          at 4-10% CV, so the column was taken again on a quiet whole-socket Cascade Lake
          (GCP `n2-standard-32`, one pinned core, every row 0.04-0.46% CV). Same binary,
          `VPHILOX_BACKEND` forced, so scalar, AVX2 and AVX-512 are one measurement in
          three files: `generate_n` is **2.2883 / 0.8740 / 0.4747 cycles/byte**, against
          `std::mt19937` at 1.4210, xoshiro256++ at 0.4135 and PCG64 at 0.7469. The AVX-512
          column is 2.99x `std::mt19937` and 1.84x AVX2, and the refill buffer costs
          1.19x / 1.60x / 2.03x bulk as the kernel gets faster. Written up in
          [`avx512-cascade-lake-2026-08-25.md`](docs/benchmarks/avx512-cascade-lake-2026-08-25.md)
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
  - [x] Investigate the knee (memory bandwidth vs false sharing)
    - [x] No knee on ARM, and that is about the kernel not the memory system: 3.19
          cycles/byte over 4 cores is ~3 GB/s, far under what LPDDR4X supplies, so the 4 MiB
          arm could not provoke one. Coffee Lake with AVX2 degrades 6.66x by 32 threads at
          the same working set. With NEON landed the ARM kernel is now 1.47 cycles/byte,
          roughly 6.5 GB/s over 4 cores — still under what LPDDR4X supplies, so this axis
          needs the x86 host below rather than another Pi run
    - [x] **Located on x86, and it is not memory** — 16 physical cores of a two-socket
          Cascade Lake, in
          [`scaling-cascade-lake-2026-08-25.md`](docs/benchmarks/scaling-cascade-lake-2026-08-25.md).
          With one thread per *physical* core, aggregate cycles/byte on the L2-resident arm
          is flat from 1 to 16 threads (0.4750 → 0.4833, +1.7%) at 29.25 GiB/s. The first
          knee is **hyperthread co-location**: the same sixteen workers on eight cores plus
          their siblings cost 0.6536, a 35% loss, because the kernel is execution-port-bound
          on the integer multipliers and a sibling has nothing idle to use. Memory is the
          *second* limit and tracks footprint per socket against the 33 MiB L3, not thread
          count — 16 MiB/socket is flat, 32 MiB/socket costs 1.36x whether it arrives as 8
          threads on one socket or 16 across two, and 64 MiB/socket costs 2.00x. False
          sharing is excluded by the flat L2 arm, NUMA by `numactl --membind` moving
          nothing, and frequency by direct measurement
    - [x] **Frequency excluded by measurement, not argument.** RDTSC counts reference
          cycles, so a turbo ceiling moving with thread count is indistinguishable from
          contention, and GCP exposes no vPMU. `scripts/benchmarks/freq_probe.cpp` times a
          dependent `addq` chain of known length against both a wall clock and RDTSC to
          recover the real core clock: 3.371 → 3.366 GHz from 1 to 16 active cores, and
          identical under scalar, AVX2 and AVX-512 load. That also settles #27's open
          hypothesis — Philox's AVX-512 kernel triggers no measurable licence drop
  - [x] OpenMP variant alongside the `std::thread` one — `BM_thread_scaling_omp`
        and `BM_thread_scaling_omp_bulk` run the identical job over the identical
        buffers and disjoint counter ranges, so the only variable is the construct
        that starts the workers. Both runtimes get persistent threads (libgomp keeps
        its team alive between parallel regions exactly as the pool does), which
        makes the comparison the cost of the parallel construct rather than of
        spawning threads. The point is to show the scaling result belongs to the
        generator and not to this project's own pool. Registered only when CMake
        finds OpenMP; two guards keep a bad row from being mistaken for a good one
        — `omp_set_dynamic(0)` plus a team-size check, because a short team would
        otherwise leave `bytes` counting work nobody did, and a per-slot thread
        identity check, because a `perf_event_open` counter measures the thread
        that opened it and OpenMP does not promise a stable thread-number-to-thread
        mapping across regions. Both report a skipped row rather than a number
    - [x] **Measured, and the answer is cleaner than expected**
          (`docs/benchmarks/openmp-runtime-tigerlake-2026-08-25.md`). Normalised
          to each runtime's own single-worker cost, libgomp is flat to one thread
          per physical core — 1.000x, 1.000x, 1.003x at 1, 2, 4 workers — where
          the `std::thread` pool drifts to 1.220x at four. The two runtimes are
          indistinguishable at 1-2 workers (0.5084 against 0.5086, both CV <=
          0.14%) and both show the hyperthread knee at eight. So the flat result
          is the generator; the deviation belongs to the harness. The leading
          explanation is thread count rather than threading model — OpenMP's team
          master is a worker, so four workers is four runnable threads on four
          cores, while the pool's dispatcher is a fifth — but that is an
          inference, not a measurement, since the benchmark registers only
          1/2/4/8 workers and the direct test needs three
    - [x] Second-order: libgomp's barrier spin costs 9.0% at four workers (where
          sleeping wastes futex wakeups) and *saves* 4.0% at eight (where every
          spin steals execution ports from a working sibling). It also
          contaminates neighbours — under random interleaving the two noisiest
          rows in the 48-row sweep were *single-threaded* OpenMP at ~11% CV,
          falling to 1.45-2.41% under `OMP_WAIT_POLICY=passive`, because a
          previous 32-worker region's team was still spinning
  - [x] Instruction-cache miss rates — harness done, measurement pending a host
        worth quoting. `--perf-counters` on a scaling run gives per-worker
        `icache_mpki` and `instructions_per_byte`, written to `<tag>-icache.json`
        so an instruction-supply study never overwrites the throughput curve it
        explains.
    - [x] **Not libpfm, and not `--benchmark_perf_counters`.** Both were the
          original plan and both are wrong here. Google Benchmark's counters
          start and stop on the thread running the benchmark loop, which in this
          pool-driven benchmark is the dispatcher — it blocks on a condition
          variable while the workers do every instruction, so the numbers would
          have been a near-empty thread on the `std::thread` path and one
          worker's share on the OpenMP one, both looking like an aggregate. Per-
          worker counters, summed as `cycle_counter` already is, are the only
          shape that answers the question. That also removes the dependency:
          retired instructions and L1 i-cache read misses are both generic
          `perf_event_open` events, so libpfm is not needed at all
    - [x] A denied PMU drops the counter rather than reporting a zero, and
          `run_matrix.sh` exits non-zero if an `--perf-counters` run produced no
          `icache_mpki`. An MPKI of 0.00 is a plausible answer to this question,
          so it must never be what a missing PMU produces
    - [x] `instructions_per_byte` doubles as the self-check: it is a property of
          the kernel, so it must not move with thread count. Measured invariant
          to six digits from 1 to 8 threads, and 0.00% CV across repetitions
    - [x] **The host requirement is narrower than it looked, and no machine on
          record meets it.** The study needs SMT *and* a working PMU *and*
          controllable placement. The 16-core Cascade Lake host from #51 has the
          cores but GCP exposes no vPMU at all (`perf stat -e cycles` reports
          `<not supported>`), so it cannot count. The Pi 5 is the only bare-metal
          machine on record and has a working PMU but no SMT, so it cannot cross
          the co-location boundary the question is about. A WSL2 laptop with
          4C/8T and a working PMU was tried and rejected: `taskset` there fixes
          virtual CPU IDs only, and two workers on one core's siblings measured
          *faster* than two on separate cores, which is impossible under real
          co-location. What is needed is bare-metal x86 with SMT
    - [x] **Measured on bare metal, and instruction supply is excluded**
          (`docs/benchmarks/icache-placement-tigerlake-2026-08-25.md`). A Tiger
          Lake i5-11300H, 4C/8T, `systemd-detect-virt` reporting `none`, cleared
          the co-location gate at 38.9-40.6% against #51's 35% on an unrelated
          part. Four workers moved from one thread per physical core onto two
          cores' siblings cost **+59.0% cycles/byte while i-cache MPKI fell
          35.7%** (0.2108 -> 0.1356). Instruction supply moves *opposite* to the
          penalty, so it is not the mechanism and #51's port-contention
          conclusion stands. The direction is explicable: both siblings run the
          identical kernel, so sharing one 32 KiB L1i is constructive. The weak
          hint from the rejected WSL2 host pointed the right way
    - [x] The self-check passed exactly: `instructions_per_byte` read 0.958554 in
          both arms, identical to six decimal places at 0.00% CV. As a second
          check MPKI is clock-independent, and the turbo-on and turbo-off runs
          agree to ~1% across a 42% change in clock
    - [x] Honest limit: the co-located arm's cycles/byte CV is 2.22%, above this
          project's 1% bar, because a four-core laptop running a desktop cannot
          hold an unpinned two-core arm quiet. Its median reproduces to 0.04%
          across independent runs, and the finding is a direction rather than a
          threshold, so the conclusion survives — but the cycles/byte column here
          is a weaker version of what #51 already measured on better hardware
- [x] **Cross-platform bit parity** — same key and counter produce identical output on x86-64,
      aarch64, and (if reachable) a cuRAND/GPU reference. This is the reproducibility claim;
      it needs a test, not an assertion.
  - [x] `tests/test_cross_platform_parity.cpp` folds an 8191-block stream into an FNV-1a
        digest checked against a constant in the file, covering the paths the KAT vectors
        miss: SIMD tails, the refill buffer, chunked `generate_n`, the counter carry chain,
        and float conversion. Verified identical under `VPHILOX_BACKEND=scalar|avx2` and
        under `VPHILOX_FORCE_SCALAR`, and the constant itself was checked against an
        independent FNV implementation rather than recorded from the code it tests
  - [x] Confirm the same digest on real aarch64 — **done**. The Raspberry Pi 5 runs
        110/110 tests green at `cce395b` with the backend resolving to `neon`, all eight
        `CrossPlatformParity` cases included. The FNV-1a digest of an 8191-block stream is
        therefore now known to be identical on x86-64 scalar, AVX2, AVX-512 and aarch64
        NEON — four kernels, two architectures, three compilers. That is the
        reproducibility claim discharged by measurement rather than by assertion
  - [x] **Cross-checked against NVIDIA cuRAND** (`docs/curand-parity.md`). 16/16
        cases identical over 4096 words each, under `scalar`, `avx2` and
        `avx512`. Two independent assertions per case: the *core*, feeding
        cuRAND's own reported (counter, key) into vphilox, and the *mapping*,
        that those values match what `(seed, subsequence, offset)` predicts.
        Both were mutation-tested — swapping the predicted counter halves fails
        mapping only (7/16), flipping one counter bit fails core only (0/16) —
        so they are two tests rather than one written twice
    - [x] **No GPU or nvcc needed, which makes it portable rather than weaker.**
          cuRAND guards its decoration with `#if !defined(QUALIFIERS)`, so the
          tool compiles NVIDIA's reference implementation as host C++. The only
          target-dependent line in the generator is `mulhilo32`, whose host and
          device branches both compute the high 32 bits of a 32x32 product. What
          is unverified is NVIDIA's *device codegen*, not the algorithm, and
          `docs/curand-parity.md` says so rather than glossing it
    - [x] The interop mapping is now documented: cuRAND's `subsequence` occupies
          the counter's high 64 bits, `offset / 4` the low 64, and `offset % 4`
          is a word index into the block — so cuRAND's `offset` counts words
          where a vphilox counter counts blocks of four. That asymmetry is the
          part a caller would otherwise get wrong
    - [x] Optional exactly as `vphilox_testu01` is: the target does not exist
          unless the cuRAND headers are found, and configure logs which case you
          are in. CUDA never becomes a dependency of the library
- [x] Publish plots and raw CSVs under `docs/benchmarks/` —
      `scripts/benchmarks/publish_results.py` generates
      [`docs/benchmarks/raw/`](docs/benchmarks/raw) and
      [`plots/`](docs/benchmarks/plots) from the JSON in `results/`, and CI runs
      it with `--check` so an archived run cannot leave the published figures
      behind. Standard library only: matplotlib is not installed on the Pi or
      the GCP images, and a plot needing a pip install is a plot nobody
      regenerates. Cross-machine figures plot ratios measured within each
      machine, because reference-cycle RDTSC and the Pi's `perf_event` counter
      are not the same unit; `machines.csv` carries a `quality` column marking
      the four archived runs that built the harness but do not meet the CV bar,
      and no figure uses them

**Deliverable:** a complete benchmark dataset and statistical pass logs.

---

## Phase 5 — Paper & release

- [~] **Paper** (LaTeX) — working title *Portable, Seekable Random Streams for
      Parallel CPU Simulation*, retitled from *vPhilox: SIMD-Accelerated Counter-Based
      Pseudo-Random Number Generation for Parallel CPU Systems*.

      **Full draft in `paper/vphilox.tex`; see `paper/README.md` for what is still open.**
      Twelve sections, five figures, six tables, every number traced to an archived run.
      **One `\todo` marker remains**, for the Zenodo DOI, which cannot be filled before
      the release exists. The XGBoost citation that used to block publication is in:
      §1 quotes the review comment on dmlc/xgboost#12485 verbatim, discloses in a
      footnote that this author submitted the proposal it declined, and cites
      dmlc/xgboost#12459 as an unrelated user's report of the same failure in the field.
      One gap is a red placeholder rather than a `\todo`: the affiliation line in the
      author block.

      All four Phase 4 results are now folded in. §8.3 reports the instruction-supply
      measurement (#53), §8.4 the OpenMP runtime contrast (#52), §9.1 the cuRAND
      cross-check (#54), and the machines table carries the bare-metal Tiger Lake host
      those studies needed. §10 no longer lists any measurement as pending hardware.

      Written against `IEEEtran` in the Computer Society journal format, which is what
      TPDS, TC and TSE use; a conference submission is a class-option change. Builds
      clean at 12 pages — no errors, no overfull boxes, no undefined references — and
      **CI now builds it**, so that is verified rather than asserted. The `paper builds`
      job also uploads the PDF as a run artifact, and warns when `vphilox.tex` was
      committed more recently than the tracked `vphilox.pdf`.

      The original title names the method, not the contribution, and it invites exactly the
      comparison the measurements lose: xoshiro256++ is faster on four of the five machines
      tested. Speed is the *objection this work removes*, not the result it reports. Lead with
      what no competing generator offers — a checkpoint that survives a change of standard
      library, O(1) seek, and results independent of thread count — and use the throughput
      figures to show those properties are free.
  - [x] **The portability failure** — §1 and §3. `std::mt19937`'s serialized state is not
        portable: format flags differ (libstdc++ has `operator<<` force `dec`; the MSVC STL
        does not) and so does the layout (624 words raw plus a position, against 624 rotated
        with none). A checkpoint written on Linux fails on Windows and loads *silently wrong*
        on macOS. Counter-based state — a key and a position — cannot fail this way
  - [x] **Reproducibility under parallelism** — §8. Identical output regardless of thread count
        and scheduling, with the measured scaling behind it (3.95x on 4 cores, 98.8%
        efficiency, aggregate cycles/byte flat to 0.063% from 1 to 32 threads), plus the
        placement result: the first knee is hyperthread co-location at 35%/worker, not memory.
        §8.3 and §8.4 then close out both alternative explanations by measurement: instruction
        supply *improves* 36% under co-location while the penalty is +59%, and libgomp
        reproduces the flat curve to 0.3% where the `std::thread` pool drifts to 1.220x
  - [x] Scalar wide-multiply bottleneck analysis — §4.2 and §7.2, with the finding that the
        reported ~10x penalty against `std::mt19937` did **not** reproduce on any of five CPUs
        measured (1.10-1.65x cost per byte, not 10x)
  - [x] AVX2/AVX-512/NEON lane interleaving theory — §4 and §7.4, including why the AVX-512
        downclocking penalty stays small here (Philox's inner loop is entirely light-tier
        integer work). §7.4 reports all four pinned-backend runs and separates the two Cascade
        Lake instances: the 78% width conversion belongs to the shared 4-vCPU slice, the same
        stepping on a whole host converts 92%, and the scalar kernel is slower there too, which
        no licence can explain
  - [x] IEEE-754 mantissa injection math — §5, including exactness by Sterbenz's lemma and why
        the exhaustive round trip catches what no distributional test can
  - [x] Benchmark figures from Phase 4, reported honestly — §10 carries both results that cut
        against us, that xoshiro256++ leads on four of five machines and that NEON leaves the
        buffered engine behind `std::mt19937` on ARM. The two Phase 4 results added since are
        tables rather than figures on purpose: both are two-arm contrasts of four numbers,
        which a plot makes harder to read, and adding them to `publish_results.py` would put
        two more byte-reproducible SVG/PDF pairs under CI's `--check` for no gain
  - [x] **Portable-state analysis** — §1, §3 and §9.1. The failure mode, the counter-based
        answer, and now an external check that the answer interoperates: 16/16 cases identical
        against NVIDIA cuRAND, with the seeding mapping documented in `docs/curand-parity.md`
The publishing route is written down in
[`docs/publishing-guide.md`](docs/publishing-guide.md), and the order matters:
Zenodo archives the *software*, TechRxiv hosts the *paper*, and arXiv comes last
because it needs an endorsement that is easier to obtain once the preprint and
the DOI both exist. The same manuscript must not go to both Zenodo and TechRxiv,
or one paper ends up with two competing DOIs.

- [ ] Tag and publish `sinhaparth5/vphilox` — everything below depends on it.
      The tag bumps MICRO: `2026.08.0` was never tagged but already has a dated
      changelog section describing the Phase 0/1 scaffold, so reusing it would
      ship release notes that say there is no speedup yet. `VERSIONING.md` has
      the steps and CI's `release readiness` job enforces them
- [ ] Zenodo DOI for the tagged release (software archive, not the manuscript)
- [ ] Update `CITATION.cff` with the DOI once Zenodo assigns one
- [ ] TechRxiv preprint (IEEE-operated, moderated, supplies the paper's DOI)
- [ ] arXiv preprint (cs.PF primary, cs.MS secondary) once endorsed
- [x] `CITATION.cff` for one-click BibTeX
- [ ] Package for vcpkg and Conan — needs the release tarball URL and hash, so
      it cannot start before the tag
- [x] Release notes in `CHANGELOG.md` — `[Unreleased]` now covers the whole of Phases 2
      through 4: the serialized state, all three SIMD kernels, the cuRAND cross-check, the
      per-worker counters and placement gate, the figure pipeline, the cross-platform
      parity digest, and the four measured results worth not re-deriving. It still needs a
      release heading rather than `[Unreleased]` when the tag goes out, and CI's
      `release readiness` job now fails the tag if that has not happened

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
