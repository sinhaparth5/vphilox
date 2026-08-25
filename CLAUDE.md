# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

vphilox is a header-only C++20 implementation of the Philox4x32-10 counter-based
PRNG with SIMD kernels. `CONTRIBUTING.md` has the full contributor detail.

## Why this exists

Read this before optimising anything.

The library came out of a bug in XGBoost. Its checkpoints stored a
`std::mt19937` state, and that state is not portable: the format flags differ
between standard libraries (libstdc++ has `operator<<` force `dec` and ignore a
caller's `std::hex`, the MSVC STL does not) and so does the layout (624 words
raw plus a position, against 624 rotated with none). A checkpoint written on
Linux fails on Windows and loads *silently wrong* on macOS. Counter-based state
— a key and a position — cannot fail that way.

The fix was rejected upstream with "the performance here is 1/10 of mt. This is
due to no specialisation for wide multiplies." **The SIMD work exists to answer
that sentence, not to win a throughput contest.** Two measured results settle
it: the ~10x penalty did not reproduce on any of five CPUs (0.61-0.87x, not
0.10x), and specialising the wide multiplies puts the raw kernel at 2.6-4.6x
`std::mt19937` and 4.1-5.3x unspecialised Philox.

The practical consequence: **do not optimise toward xoshiro256++.** It is faster
here on three of the four parts benchmarked, it is a latency-bound scalar chain
that a wider kernel does not catch, and it offers none of the properties this
library is for — portable state, O(1) seek, identical output regardless of
thread count. Report the comparison honestly; do not chase it. Speed only has to
be good enough that nobody can raise the original objection.

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

Benchmarks report **cycles per byte** first, GB/s second (`bench_engines.cpp`
reads RDTSC on x86 and `perf_event_open` on Linux ARM). Results are written up
under `docs/benchmarks/`, raw JSON in `docs/benchmarks/raw/` and `results/`.

The CSVs and figures under `docs/benchmarks/raw/` and `docs/benchmarks/plots/`
are **generated, not hand-written** — `scripts/benchmarks/publish_results.py`
derives them from `results/*.json`, and CI runs it with `--check`. Archive a run
and re-run the script; never edit the output. It is standard library only on
purpose, which is also why it writes its own SVG *and* its own PDF rather than
shelling out to a converter: both are byte-reproducible text, so `--check` can
police them. Each figure ships three ways — `.png` for the docs, `.pdf` for the
paper (`\includegraphics` cannot read an SVG without `--shell-escape`), `.svg`
as the checked source. The PNG is the exception `--check` skips: it is a
rasterisation of the SVG by whichever of `rsvg-convert`, Inkscape or Pillow is
installed, and those do not agree byte-for-byte. **ImageMagick is not one of
them** — without librsvg it renders every gridline black and drops `<path>`
entirely, so it publishes a wrong figure rather than failing. Cross-machine figures plot ratios measured within each machine, since
cycles/byte does not transfer across hosts, and `machines.csv` carries a
`quality` column so a harness-validation run cannot be quoted as a result.
Every benchmark binary stamps the resolved backend into the JSON `context`
(`benchmarks/bench_main.hpp`), which is the one fact the environment capture
cannot know.

Do not run benchmarks by hand. `scripts/benchmarks/run_matrix.sh` pins the
governor and restores it on exit, records provenance next to the numbers, and
refuses runs that are not worth writing up:

```bash
scripts/benchmarks/run_matrix.sh --tag <machine> --cpu <isolated-cpu>
scripts/benchmarks/run_matrix.sh --tag <machine> --bench scaling   # unpinned; needs every core
scripts/benchmarks/run_matrix.sh --tag <machine> --bench scaling --perf-counters  # i-cache study
```

The placement study that #53 needs is `run_placement.sh`, which runs one
worker count under two placements and compares `icache_mpki`:

```bash
scripts/benchmarks/run_placement.sh --tag <machine>          # bare metal only
```

It reads the sibling map from sysfs rather than assuming an enumeration, and
**it gates on a measured co-location penalty before it will run anything**: two
workers on one core's siblings must cost at least 15% more cycles/byte than two
on separate cores, or it aborts. That gate is the whole point. A VM publishes a
correct sibling map and accepts every `taskset` mask while the hypervisor places
vCPUs itself, so both arms silently become the same placement — see the VM rule
above.

`--perf-counters` adds per-worker retired-instruction and L1 i-cache-miss
counters (#53) and writes `<tag>-icache.json` rather than `<tag>-scaling.json`.
Keep the two apart: the extra ioctls land in wall time, so **only `icache_mpki`
and `instructions_per_byte` are quotable from an i-cache run** — its
`bytes_per_second` is not comparable with the published scaling curve. The
counters are per-worker rather than Google Benchmark's
`--benchmark_perf_counters` because those start and stop on the thread running
the benchmark loop, which here is the dispatcher: it blocks on a condition
variable while the workers do every instruction. `instructions_per_byte` is
the built-in check — it is a property of the kernel, so it must not move with
thread count, and a run where it does has counters attached to the wrong
threads.

It writes `results/<tag>-{matrix,scaling}.json` plus a matching
`-environment.txt`, and exits non-zero if the cycle counter did not report or
any row exceeds **1% cycles/byte CV**, which is this project's bar.

Four measurement rules, each learned by getting them wrong:

- **Always rebuild before measuring.** A leftover `build/` will happily run a
  months-old binary and print a plausible table. The script builds
  unconditionally and checks the expected rows exist.
- **RDTSC counts reference cycles, not core cycles.** Figures compare *within*
  one machine, never across two with different nominal frequencies. On an
  unpinned machine, a moving turbo ceiling lands directly in the column.
- **A cloud VM is fine for correctness, not for throughput** — steal time is
  invisible. The exception is a dedicated-vCPU instance that still comes in
  under the CV bar, which should be labelled as cloud in the write-up.
- **A VM cannot control thread placement, even when it reports the topology.**
  Inside WSL2 the guest publishes a correct-looking sibling map and `taskset`
  accepts the mask, but the hypervisor schedules vCPUs onto host cores on its
  own, so the mask fixes virtual CPU IDs and nothing physical. The tell is that
  two workers on one core's siblings come out *faster* than two on separate
  cores (0.63 against 0.63-0.77 cycles/byte, and stable against a 20% spread),
  which is impossible for a port-bound kernel under real co-location. Any
  placement study needs bare metal. Checking that the PMU works and that SMT is
  reported is not enough — check that co-location actually costs what #51 says
  it costs before trusting a placement result.
- **Report results that cut against the library.** xoshiro256++ leads on three
  of four parts, and on ARM the buffered engine is still behind `std::mt19937`
  even though the NEON kernel now leads it; both are in `docs/benchmarks/` and
  the roadmap because omitting them would be the problem, not the numbers.

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
  `discard()` O(1): seeking N blocks is `counter += N`. `counter_sub` /
  `counter_retreated` are the inverse, needed by serialization because the
  engine's counter runs ahead of the caller-visible position by whatever is
  still buffered.
- `detail/kernel_scalar.hpp` — the reference implementation and ground truth,
  fully `constexpr`.
- `detail/kernel_avx2.hpp` — eight interleaved counters per `__m256i`,
  `[[gnu::target("avx2")]]` rather than a TU flag because the kernel lives in a
  header. `detail/kernel_avx512.hpp` interleaves sixteen counters per `__m512i`
  and `detail/kernel_neon.hpp` four per `uint32x4_t` with two groups
  interleaved per iteration; both are real
  implementations, verified on Sapphire Rapids / Skylake-SP and Cortex-A76.
- `detail/cpu_features.hpp` — one-shot runtime CPU probe. The x86 path is a raw
  `CPUID` + `XGETBV` probe written once for every compiler (`__cpuidex` on MSVC,
  `__cpuid_count` elsewhere) rather than `__builtin_cpu_supports`, so the Linux
  and macOS runs exercise the same detection logic MSVC depends on.
- `detail/dispatch.hpp` — resolves one `kernel_fn` per `Rounds` instantiation on
  first use.
- `philox.hpp` — `basic_engine<Rounds>` / `engine`, a
  `std::uniform_random_bit_generator` over a 64-word (256-byte) aligned refill
  buffer sized so no backend ever splits a refill. `generate_n(u32*, count)` and
  `generate(std::span<u32>)` are the bulk path: they run the kernel straight
  into the caller's buffer, skipping the refill copy (100% of raw kernel
  throughput, 1.72x the buffered engine). Bulk and `operator()` must stay
  interchangeable — N bulk words produce exactly what N `operator()` calls
  produce and leave the engine in the same state.
- `float_cast.hpp` — division-free `u32/u64 -> float/double` by IEEE-754
  mantissa injection.
- `serialize.hpp` — portable text form for `engine_state`, the reason the
  library exists. **Deliberately not included by `vphilox.hpp`**: it needs
  `<istream>`, `<ostream>` and `<string>`, and most callers never serialize.
  `engine_state` (in `philox.hpp`) records a *position* — key, the block holding
  the next output, and the word within it — never the engine's internals.
  Writing `next_`/`cursor_` would bake `refill_blocks` into the format, and that
  changed from 8 to 16 when the AVX-512 kernel landed. Note `counter()` is the
  next refill's counter, so `basic_engine(g.key(), g.counter())` skips whatever
  is buffered; `state()`/`set_state()` are the exact round trip, and a test
  asserts the naive route skips.

### The kernel contract

Every backend implements `generate(base, key, out, blocks)`, writing
`blocks * 4` words where block *i* is `philox4x32(base + i, key)`. Output must
not depend on how the caller chunks the request — that independence is what
makes parity testing meaningful and lets the engine refill in any size. Kernels
handle their own tails and must not assume `out` is aligned.
`preferred_blocks` advertises the tail-free width: scalar 1, AVX2 8, AVX-512 16,
NEON 8. On x86 that is one counter per 32-bit lane, which is what
`docs/benchmarks/simd-lane-layout.md` settled, and both figures are double what
was originally planned — those plans underestimated the reachable lane count.
NEON is 8 for a different reason: four lanes per register, but two independent
groups per iteration, because the Cortex-A76 kernel was latency-bound rather
than width-bound (issue #89).

### Dispatch and the `implemented` flag

A kernel is selected only if it is compiled in (`VPHILOX_HAS_*`), has
`implemented = true`, and the CPU supports it. All four kernels now set
`implemented = true`; the flag stays because it is what keeps a half-finished
kernel out of dispatch, so `active_backend()` never reports a backend that did
not actually run. Landing a new kernel means: implement `generate()`, flip
`implemented` to `true`, confirm `preferred_blocks` matches the real
interleaving width — dispatch, `tests/test_kernel_parity.cpp` and
`tests/test_cross_platform_parity.cpp` pick it up with no further wiring.

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

`test_cross_platform_parity.cpp` is the one to understand before changing
anything that touches output. It folds an 8191-block stream into an FNV-1a
digest checked against a constant in the file, covering what the KAT vectors
miss: SIMD tails, the refill buffer, chunked `generate_n`, the counter carry
chain, and float conversion. It is what proved the `refill_blocks` 8 → 16 change
left the stream untouched. If it fails, the generated stream changed — that is a
wrong change, not a stale expectation, and the constants are not to be updated
to make it pass.

## Conventions

- `.clang-format` is authoritative and CI-enforced: Google base, 4-space indent,
  100 columns, left-aligned pointers, consecutive-assignment alignment (chosen
  so a botched lane index in intrinsic code is visible at a glance).
  **The version is pinned to 21.1.8 and that matters as much as the config.**
  clang-format is not stable across major versions: 18 (what `ubuntu-24.04`
  ships) and 21 disagree on the short block after the `#pragma omp parallel` in
  `bench_scaling.cpp`, and no source text satisfies both, so an unpinned check
  enforces the runner image rather than `.clang-format`. CI installs the pin
  into a venv; `CLANG_FORMAT_VERSION` at the top of `.github/workflows/ci.yml`
  is the only place the number is written. If a format failure looks absurd,
  check `clang-format --version` before changing any code.
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

Phase 1 (scalar reference, KATs, baseline benchmarks) is done. Phase 2 has met
its deliverable: **all three SIMD kernels exist, are verified on real hardware,
and each beats `std::mt19937` on the raw kernel.** AVX2 is 3.3x scalar
(`docs/benchmarks/avx2-2026-08-23.md`), AVX-512 is 4.1x scalar and 1.80-1.83x
AVX2 with no downclocking penalty on either Sapphire Rapids or Skylake-SP
(`docs/benchmarks/avx512-downclocking-2026-08-24.md`), and NEON is 2.19x scalar
on a Cortex-A76 after the two-group unroll
(`docs/benchmarks/neon-unroll-pi-2026-08-25.md`) — 1.33x `std::mt19937` and
1.11x PCG64. The one gap left there is the *buffered* engine on ARM, still
0.87x `std::mt19937` because the refill drain now costs more than the kernel
does; `generate_n` gets the full win. The engine also has a portable
serialized state (`include/vphilox/serialize.hpp`), which is the problem the
library was built around. Phase 4
statistical validation is done and does not need re-running: PractRand clean to
1 TB, byte-identical across backends over that terabyte, and TestU01 BigCrush
passing all 160 statistics (`docs/statistical-validation.md`).
`ROADMAP.md` tracks phases and `docs/` holds the theory, research, and recorded
benchmark runs.

Three measured results worth not re-deriving. The ~10x
scalar-Philox-vs-mt19937 slowdown from the literature did **not** reproduce on
any machine measured here (`docs/benchmarks/baseline-2026-08-21.md`). The
refill buffer now costs ~109% of bulk throughput on AVX-512, ~42% on AVX2 and
~10% on x86 scalar — the faster the kernel, the more the drain pass dominates —
which is what makes issues #36 and #37 worth more than their original
estimates. And **the multi-core knee is hyperthreading, not the memory system**
(`docs/benchmarks/scaling-cascade-lake-2026-08-25.md`): one thread per physical
core is flat to sixteen cores, two workers sharing a core cost 35% each because
the kernel is execution-port-bound, and memory only enters as a second limit
that tracks footprint per socket. Advise callers to size pools by physical
cores, not `hardware_concurrency()`. Frequency was excluded by direct
measurement rather than argument (`scripts/benchmarks/freq_probe.cpp`), which
also retired the open AVX-512 licence hypothesis from issue #27.
