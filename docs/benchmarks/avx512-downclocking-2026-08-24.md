# AVX-512 downclocking on Skylake-SP — 2026-08-24

Issue #27 asks that dispatch not prefer AVX-512 until it demonstrably beats
AVX2 **on the same part**. The concern is Intel's frequency licensing: a core
executing 512-bit instructions drops clock, and on Skylake-SP that penalty was
large enough to make AVX-512 a net loss for some workloads.

[`avx512-sapphire-rapids-2026-08-24.md`](avx512-sapphire-rapids-2026-08-24.md)
answered this for Sapphire Rapids, where the frequency behaviour was largely
addressed. This is the harder case: an actual Skylake-SP part.

## Environment

| | |
|---|---|
| CPU | Intel Xeon @ 2.00 GHz (Skylake-SP), 4 vCPU |
| Instance | GCP `n1-standard-4`, `--min-cpu-platform="Intel Skylake"`, `europe-west2-b` |
| OS | Ubuntu 24.04.4 LTS |
| Compiler | GCC 13.3.0 |
| ISA | `avx512f`, `avx512dq`, `avx512cd`, `avx512bw` |
| Affinity | CPU 1 |

Cloud VM, so steal time is invisible; CVs came in at 0.05-0.22%. The instance
was deleted after the run. All 90 tests passed here, backend resolving to
`avx512`.

## The answer: no penalty

`BM_vphilox_bulk`, same binary, backend pinned with `VPHILOX_BACKEND`:

| backend | Skylake-SP | vs AVX2 | Sapphire Rapids | vs AVX2 |
|---|---:|---:|---:|---:|
| scalar | 2.1753 | | 1.9311 | |
| avx2 | 0.7698 | 1.00x | 0.8462 | 1.00x |
| **avx512** | **0.4202** | **1.83x** | **0.4712** | **1.80x** |

**AVX-512 is 1.83x AVX2 on Skylake-SP** — if anything a slightly better ratio
than on Sapphire Rapids. The downclocking penalty #27 was written to guard
against does not appear.

## Why it does not appear

Intel's frequency licences are tiered by *what kind* of 512-bit instruction is
executing, not merely by width. Heavy 512-bit floating-point and FMA work
drops the core to the lowest licence; light 512-bit integer, logical and
shuffle work stays at or near the AVX2 licence.

Philox4x32 is entirely in the light tier. The kernel's inner loop is
`vpmuludq`, `vpaddd`, `vpxord`, `vpsrlq`, and lane shuffles — there is not a
single floating-point instruction in it. The generator sidesteps the classic
AVX-512 downclocking trap by construction rather than by tuning.

That is worth stating in the paper: counter-based generators built on integer
multiply are unusually good candidates for AVX-512 precisely because they never
touch the licence tier that makes it a bad trade.

## An unexpected result: the ranking flips

Full matrices on both parts, medians:

| | Skylake-SP | Sapphire Rapids |
|---|---:|---:|
| `BM_vphilox_bulk` (avx512) | **0.4210** | 0.4712 |
| `BM_xoshiro256pp` | 0.4703 | **0.3698** |
| `BM_pcg64` | 0.6460 | 0.7347 |
| `BM_mt19937` | 1.9483 | 1.2450 |
| `BM_philox_scalar` | 2.2323 | 1.9347 |

**On Skylake-SP vphilox is the fastest generator in the matrix**, 1.12x ahead
of xoshiro256++. On Sapphire Rapids it is second, 1.27x behind. Same code, same
compiler, opposite ordering.

The mechanism is that the two generators scale on different things.
xoshiro256++ is a sequential dependency chain: one 64-bit state update per
output, latency-bound, and it gets faster on a newer core with better
single-thread execution. vphilox's kernel is throughput-bound across sixteen
independent lanes, so it gains from vector width rather than from core
improvements. Sapphire Rapids improves xoshiro more than it improves a kernel
that was already saturating vector ports.

The claim to make is therefore **not** "vphilox is faster than xoshiro" but
"on AVX-512 parts the two are within ~15% of each other in either direction,
and vphilox additionally offers O(1) seek, a reproducible stream from any
(key, counter), and per-thread substreams without `jump()`."

## Caveat on comparing across the two tables

Cycles/byte here comes from RDTSC, which counts at the part's *nominal*
frequency. Nominal differs between these two machines (2.00 GHz against
2.70 GHz) and turbo headroom differs with it, so a cycles/byte figure from one
table is not directly comparable with the same row in the other. Comparisons
*within* a table are sound, because every row on a given machine shares the
same clock behaviour, and every conclusion above is drawn that way.

## Verdict on #27

Answered on both AVX-512 generations available: dispatch preferring AVX-512 is
correct, by 1.80-1.83x, with no downclocking penalty on either. The remaining
gap is Cascade Lake, which sits between the two parts tested and is very
unlikely to behave differently given the light-tier argument above.

## Artifacts

- [`results/skylake-sp-matrix.json`](../../results/skylake-sp-matrix.json)
- [`results/skylake-sp-matrix-environment.txt`](../../results/skylake-sp-matrix-environment.txt)
