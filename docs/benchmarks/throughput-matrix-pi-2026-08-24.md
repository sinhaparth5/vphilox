# Throughput matrix, Raspberry Pi 5 — 2026-08-24

> **Superseded for ranking.** #28 landed on 2026-08-25 and vphilox no longer
> places last on this machine. The current AArch64 column is
> [`neon-unroll-pi-2026-08-25.md`](neon-unroll-pi-2026-08-25.md). This page is
> kept as the pre-NEON baseline the kernel was measured against, and because
> its projection is now checkable — see [The projection, resolved](#the-projection-resolved).

First run of the completed Phase 4 comparison matrix (issues #46, #47, #49),
now that xoshiro256++ and PCG64 are wired into `bench_engines`. This is the
AArch64 column.

**NEON is still a stub (#28), so vphilox dispatches to the scalar kernel
here.** Every `vphilox` row below is scalar Philox4x32-10, not a SIMD kernel.
The value of this run is that it fixes the pre-NEON ARM baseline: the number
the NEON kernel has to improve on is 3.194 cycles/byte.

## Environment

| | |
|---|---|
| CPU | Raspberry Pi 5, 4-core Cortex-A76 at 2.4 GHz |
| OS | 64-bit Raspberry Pi OS, Linux 6.12.96+rpt-rpi-2712 |
| Compiler | GCC 12.2.0 (Debian 12.2.0-14+deb12u1) |
| Affinity | CPU 3 |
| Governor | `performance` |
| Cycle source | `perf_event_open` (`PERF_COUNT_HW_CPU_CYCLES`) |
| Resolved backend | `scalar` |
| Temperature before run | 55.4 °C |
| Throttling before run | none (`0x0`) |
| Commit | `2669e74` |

Seven randomly interleaved repetitions, one-second minimum sample time, via
`scripts/benchmarks/run_matrix.sh --tag pi-arm --cpu 3`. The table reports
medians. Every row is inside this project's sub-1% cycles/byte CV bar.

## Results

| Benchmark | Cycles/byte | Throughput | Cycles/byte CV |
|---|---:|---:|---:|
| `BM_xoshiro256pp` | 0.5472 | 3.974 GiB/s | 0.01% |
| `BM_pcg64` | 1.6252 | 1.362 GiB/s | 0.00% |
| `BM_mt19937` | 1.9183 | 1.155 GiB/s | 0.78% |
| `BM_vphilox_bulk` | 3.1942 | 0.696 GiB/s | 0.01% |
| `BM_vphilox_generate_n/65536` | 3.1943 | 0.696 GiB/s | 0.01% |
| `BM_vphilox_generate_n/1024` | 3.2024 | 0.694 GiB/s | 0.00% |
| `BM_philox_scalar` | 3.2112 | 0.692 GiB/s | 0.01% |
| `BM_vphilox_generate_n/256` | 3.2268 | 0.689 GiB/s | 0.01% |
| `BM_vphilox_generate_n/32` | 3.3968 | 0.655 GiB/s | 0.02% |
| `BM_vphilox_generate_n/8` | 3.9698 | 0.560 GiB/s | 0.10% |
| `BM_vphilox` | 3.9981 | 0.557 GiB/s | 0.05% |

`BM_mt19937` at 0.78% is an order of magnitude noisier than every other row,
and was also the noisiest row in the 2026-08-22 baseline on this machine
(0.944%). That reads as a property of the generator's 2.5 KiB state and bursty
refill rather than of the harness.

## Reproduction of the 2026-08-22 baseline

The four benchmarks shared with
[`raspberry-pi-2026-08-22.md`](raspberry-pi-2026-08-22.md) come back within 2%
across a full rebuild and a two-day gap:

| Benchmark | 2026-08-22 | 2026-08-24 | Δ |
|---|---:|---:|---:|
| `BM_vphilox_bulk` | 3.193 | 3.1942 | +0.04% |
| `BM_philox_scalar` | 3.209 | 3.2112 | +0.07% |
| `BM_vphilox` | 3.958 | 3.9981 | +1.01% |
| `BM_mt19937` | 1.956 | 1.9183 | −1.92% |

The two pure-kernel rows land within 0.1%. The two that moved are the buffered
engine and `std::mt19937`, which are also the two rows carrying the most
measurement noise — consistent with the CVs above rather than with a change in
the code.

Scalar vphilox comes in at 0.60x the throughput of `std::mt19937`, against
0.61x in August.

`BM_vphilox_bulk` (3.1942) and `BM_philox_scalar` (3.2112) agree to 0.5%,
which is the internal check that dispatch really did resolve to the scalar
kernel rather than to something else that happens to be slow.

## What the new rows say

Against the scalar kernel that vphilox currently runs on ARM:

- **xoshiro256++ is 5.84x faster** (0.5472 vs 3.1942 cycles/byte)
- **PCG64 is 1.97x faster**
- **`std::mt19937` is 1.67x faster**

vphilox is last on this machine, and on raw single-threaded throughput that is
the honest summary. It is a statement about the missing NEON kernel rather
than about Philox: on x86, where the AVX2 kernel exists, the same comparison
puts vphilox ahead of `std::mt19937`.

What the ranking does establish is that the ARM gap is not a small one. If the
matrix is going to be published next to a claim that vphilox is competitive,
that claim needs the AArch64 kernel behind it.

## The refill buffer, second architecture

`BM_vphilox` (3.9981) against `BM_vphilox_bulk` (3.1942) puts the refill
buffer at **25.2%** of bulk throughput on scalar AArch64. The recorded x86
figures are ~10% on scalar and ~42% on AVX2
([`buffer-overhead-2026-08-23.md`](buffer-overhead-2026-08-23.md)), so ARM
sits between them: the drain pass costs more relative to a Cortex-A76's scalar
kernel than to Coffee Lake's.

`generate_n` closes it completely — 3.1943 against the kernel's 3.1942, or
100.0% of raw kernel throughput, matching what #37 measured on x86. The
crossover is visible in the argument sweep: 8 words per call is no better than
the buffer (3.9698), 32 recovers most of it (3.3968), and by 256 words the
remaining cost is around 1%.

That the bulk-path result reproduces on a second architecture, with a
different cycle counter and a different vector story, is worth more than
either measurement alone.

## Implications for the NEON kernel (#28)

Projection, not measurement. NEON is 128 bits against AVX2's 256, so the
lane-interleaving win should be roughly half of AVX2's measured 3.30x. At a
2.0-2.5x speedup the scalar 3.194 cycles/byte would land near 1.28-1.60,
which would move vphilox past `std::mt19937` and level with or ahead of PCG64
on this part.

It would still leave xoshiro256++ ahead by roughly 2.3-2.9x. Catching it on
raw AArch64 throughput is not a realistic goal for the NEON kernel, and the
write-up should not be framed as though it were — vphilox's case against
xoshiro is the counter-based properties (O(1) `discard`, a reproducible stream
from any (key, counter), independent substreams without `jump()`), none of
which xoshiro offers at any speed.

## The projection, resolved

Added 2026-08-25, after the fact.

The section above projected 2.0-2.5x from the NEON kernel, landing the scalar
3.194 cycles/byte somewhere in **1.28-1.60**, enough to pass `std::mt19937`
and to sit level with or ahead of PCG64, and still roughly 2.3-2.9x behind
xoshiro256++.

Measured, at `cce395b`: **1.4652 cycles/byte** — inside the projected band,
1.33x `std::mt19937`, 1.11x PCG64, and 2.68x behind xoshiro256++. Every part
of the projection held, including the part that said catching xoshiro was not
a realistic goal.

That is worth recording for one reason only: it is the *reasoning* that was
validated, not just the number. The projection came from halving AVX2's
measured 3.30x on the argument that NEON is 128 bits against 256. It reached
the right band by the wrong route — the delivered speedup came from unrolling
two independent groups to hide multiply latency (#89), not from lane width,
and the kernel had to be measured at 1.46x before the real cause was found. A
projection landing in its band is not evidence that its mechanism was right.

## Artifacts

- [`results/pi-arm-matrix.json`](../../results/pi-arm-matrix.json)
- [`results/pi-arm-matrix-environment.txt`](../../results/pi-arm-matrix-environment.txt)
