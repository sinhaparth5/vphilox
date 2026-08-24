# Bulk float conversion — 2026-08-24

Issue #34, which asked for "SIMD float conversion — `_mm256_or_si256` +
`_mm256_sub_ps` and the AVX-512/NEON equivalents, keeping conversion inside
vector registers".

Two findings, one of them a non-result: the intrinsics were already there, and
this host cannot measure what remains.

## The intrinsics were already being emitted

`to_float01` is `bit_cast<float>(0x3F800000 | (u >> 9)) - 1.0f`. Compiled as a
plain loop at `-O3 -mavx2`, GCC 15 produces:

```
.L4:
    vmovdqu (%rdi,%rax), %ymm0
    vpsrld  $9, %ymm0, %ymm0
    vpor    %ymm1, %ymm0, %ymm0
    vaddps  %ymm0, %ymm2, %ymm0     # ymm2 = -1.0f
    vmovups %ymm0, (%rsi,%rax)
    addq    $32, %rax
    cmpq    %rax, %rcx
    jne     .L4
```

That is the sequence the issue asks for, eight lanes wide, with an SSE tail
after it. `vaddps` of `-1.0f` is GCC's canonical form of `- 1.0f`; it is the
same instruction count and the same cost as `vsubps`. Hand-written intrinsics
would be transcription, and they would freeze the width at 8 where the plain
loop widens on its own (it emits SSE2 at plain `-O3`, and would emit 512-bit
under `-mavx512f`).

## What was actually missing: the ISA, not the intrinsics

vphilox is header-only, so that loop compiles with whatever flags the
*consumer's* translation unit carries. A consumer building at `-O2` with no
`-mavx2` got SSE2 conversion while the AVX2 kernel still ran at full width,
because the kernel carries `[[gnu::target("avx2")]]` with it and the
conversion carried nothing.

So `detail/float_bulk.hpp` compiles the same loop twice — once at the
consumer's baseline, once under `VPHILOX_TARGET("avx2")` — and picks between
them with the same CPU probe and the same `VPHILOX_BACKEND` override the
kernels use. Verified: a plain loop inside a `target("avx2")` function
auto-vectorises to full AVX2 even when the translation unit has no `-mavx2`.

The conversion is elementwise and branch-free, so every width produces
bit-identical output. `BulkGenerateFloat.ConversionWidthsAgreeBitForBit`
asserts that against a length deliberately chosen not to be a vector multiple.

## The number this document cannot give you

`engine::generate_n(float*, n)` generates into a 1 KiB L1-resident tile and
converts out of it. The conversion is a second pass over every word, which is
exactly the cost that `generate_n(u32*, n)` was introduced to remove for
integers (see `buffer-overhead-2026-08-23.md`). The prize for fusing
conversion into the kernels themselves is the gap between the float bulk path
and the raw integer bulk path, and `bench_float` measures it directly.

**It was not measurable here.** The host is a Core i5-8400H under WSL2 with no
governor control, and sustained AVX2 throttles it hard. Four pinned runs
(`taskset -c 3`, 9 repetitions, 2 s minimum), in order:

| Run | word-at-a-time | float bulk | u32 bulk | bulk / word | bulk / u32 |
|---|---:|---:|---:|---:|---:|
| 1 | 436.5 M/s | 869.6 M/s | 980.5 M/s | 1.99x | 88.7% |
| 2 | 437.1 M/s | 878.3 M/s | 970.5 M/s | 2.01x | 90.5% |
| 3 | 311.5 M/s | 237.9 M/s | 264.3 M/s | 0.76x | 90.0% |
| 4 | 112.4 M/s | 291.5 M/s | 395.4 M/s | 2.59x | 73.7% |
| 5 | 351.0 M/s | 611.3 M/s | 747.0 M/s | 1.74x | 81.8% |

Within-run coefficients of variation were all under 0.3%, which is the trap:
each run is internally stable and they disagree with each other by a factor of
four. Runs 1 and 2 agree and are presumably the un-throttled truth. Run 3 puts
the word-at-a-time path *ahead* of the bulk path, which cannot be true and
means its rows were sampled at different points on a thermal ramp. Runs 4 and
5 are throttled to different degrees, and the ratio that actually matters
tracks them: it falls with absolute throughput (90.5% at 970 M/s, 81.8% at
747 M/s, 73.7% at 395 M/s). Whatever that relationship is, it is a property
of this host's clock behaviour and not of the code.

So the honest summary is:

- **Bulk float is about 2x the word-at-a-time path.** Robust across every
  usable run.
- **The conversion pass costs somewhere between 10% and 26% of integer bulk
  throughput.** Not pinned down, and the spread is thermal, not algorithmic.

If the real figure is near 10%, fusing conversion into the kernels is a much
weaker lever than the refill buffer was (42% on AVX2) and probably not worth
four backend-specific float entry points. If it is near 26%, it is worth
reconsidering. **Re-run on a frequency-pinned machine before deciding** — the
same class of host used for `avx512-baseline` would do.

## Reproducing

```sh
cmake --preset bench
cmake --build --preset bench --target bench_float
taskset -c 3 build/bench/benchmarks/bench_float \
  --benchmark_min_time=2s --benchmark_repetitions=9 \
  --benchmark_report_aggregates_only=true \
  --benchmark_filter='bulk|float_mantissa'
```

Discard any run whose rows are not in the order
`u32 bulk > float bulk > word-at-a-time`; that ordering is structural, and a
run that violates it was throttling mid-sample.
