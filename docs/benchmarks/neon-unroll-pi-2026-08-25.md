# NEON two-group unroll, Raspberry Pi 5 — 2026-08-25

Issue #89. The NEON kernel landed in `21cddfc` at 2.2035 cycles/byte, which was
1.46x the scalar kernel and **0.89x `std::mt19937`** — the one target where
Phase 2's "SIMD kernels beating `std::mt19937` single-threaded" was unmet.

The diagnosis was that the kernel was latency-bound rather than width-bound:
Philox's ten rounds are a serial chain, and one four-lane group issues its
multiplies faster than their latency retires them. `cce395b` carries two
independent groups per iteration, eight blocks, alternating their rounds.

**Result: 1.4652 cycles/byte, a 1.504x speedup, and vphilox now leads both
`std::mt19937` and PCG64 on this machine.**

## Environment

| | |
|---|---|
| CPU | Raspberry Pi 5, 4-core Cortex-A76 at 2.4 GHz |
| Caches | L1d 64 KiB ×4, L2 512 KiB ×4, L3 2048 KiB shared |
| OS | 64-bit Raspberry Pi OS, Linux 6.12.96+rpt-rpi-2712 |
| Affinity | CPU 3 |
| Governor | `performance` |
| Cycle source | `perf_event_open` (`PERF_COUNT_HW_CPU_CYCLES`) |
| Resolved backend | `neon` |
| Commit | `cce395b` |

Seven randomly interleaved repetitions, one-second minimum sample time, via
`scripts/benchmarks/run_matrix.sh --tag pi5 --cpu 3`. Medians reported. Raw
JSON and the full provenance file are in `results/pi5-matrix.json` and
`results/pi5-matrix-environment.txt`.

## Results

| Benchmark | Cycles/byte | Throughput | Cycles/byte CV |
|---|---:|---:|---:|
| `BM_xoshiro256pp` | 0.5472 | 3.974 GiB/s | 0.00% |
| **`BM_vphilox_bulk`** | **1.4652** | **1.509 GiB/s** | 0.00% |
| `BM_vphilox_generate_n/65536` | 1.4654 | 1.509 GiB/s | 0.00% |
| `BM_vphilox_generate_n/1024` | 1.4801 | 1.494 GiB/s | 0.00% |
| `BM_vphilox_generate_n/256` | 1.5058 | 1.468 GiB/s | 0.03% |
| `BM_pcg64` | 1.6252 | 1.362 GiB/s | 0.00% |
| `BM_vphilox_generate_n/32` | 1.7974 | 1.232 GiB/s | 0.43% |
| `BM_mt19937` | 1.9477 | 1.138 GiB/s | 1.21% ⚠ |
| `BM_vphilox_generate_n/8` | 2.0213 | 1.097 GiB/s | 0.57% |
| `BM_vphilox` | 2.2422 | 0.989 GiB/s | 0.01% |
| `BM_philox_scalar` | 3.2102 | 0.693 GiB/s | 0.02% |

## Before and after

Against the run that landed the kernel (`21cddfc`, same machine, same script,
same CPU):

| Benchmark | 21cddfc | cce395b | Speedup |
|---|---:|---:|---:|
| `BM_vphilox_bulk` | 2.2035 | 1.4652 | **1.504x** |
| `BM_vphilox_generate_n/65536` | 2.2040 | 1.4654 | 1.504x |
| `BM_vphilox_generate_n/1024` | 2.2167 | 1.4801 | 1.498x |
| `BM_vphilox_generate_n/256` | 2.2383 | 1.5058 | 1.486x |
| `BM_vphilox_generate_n/32` | 2.4791 | 1.7974 | 1.379x |
| `BM_vphilox_generate_n/8` | 2.7779 | 2.0213 | 1.374x |
| `BM_vphilox` | 2.9697 | 2.2422 | 1.325x |
| `BM_philox_scalar` | 3.2096 | 3.2102 | 1.000x |
| `BM_mt19937` | 1.9564 | 1.9477 | 1.004x |
| `BM_pcg64` | 1.6252 | 1.6252 | 1.000x |
| `BM_xoshiro256pp` | 0.5472 | 0.5472 | 1.000x |

The bottom four rows are the control. The scalar Philox kernel is unchanged to
0.02%, and PCG64 and xoshiro256++ reproduce to four decimal places across a
full rebuild and a one-day gap. Nothing moved except the NEON path, which is
what a change confined to `kernel_neon.hpp` should look like.

The speedup shrinks as the request gets smaller — 1.50x at 65536 words down to
1.37x at 8 — because an 8-block request is exactly one iteration of the new
loop, so there is no second iteration for the scheduler to overlap into.

## Why it worked

The issue predicted 1.6-1.8 cycles/byte. The measured 1.4652 is better than
that, and the ops/cycle figure says why.

At 1.4652 cycles/byte a group of four blocks costs 64 × 1.4652 ≈ 93.8 cycles,
or 9.4 cycles per round for the ~18 NEON operations a round issues:
**1.92 operations per cycle, against the Cortex-A76's 2/cycle peak.** Before
the change the same arithmetic gave ~1.3.

That is the hypothesis confirmed rather than merely a number that improved: the
kernel was never doing wasteful work, it was waiting, and the wait is now
filled. It also sets the ceiling. With ~4% of issue headroom left, a third
group has essentially nothing to gain and would cost register pressure. **#89
is where this line of optimisation stops.**

## What this settles, and what it does not

**Settled.** Phase 2's single-threaded deliverable now holds on all three SIMD
targets. On the Pi, `BM_vphilox_bulk` is 1.33x `std::mt19937` and 1.11x PCG64.
The issue only predicted parity with PCG64.

**Not settled — the buffered engine.** `BM_vphilox` at 2.2422 is still 0.87x
`std::mt19937`. The kernel got 1.50x faster and the refill drain did not, so
the copy now dominates this path: bulk-to-buffered overhead on ARM has gone
from 35% to 53% of the total. This is the same effect already recorded on x86,
where the faster the kernel the more the drain costs — issues #36 and #37.
Callers who want the ARM win must use `generate_n`/`generate`, and a request of
256 words or more gets essentially all of it.

**Not chased — xoshiro256++.** Still 2.68x ahead at 0.5472. It is a
latency-bound scalar chain that a wider kernel does not catch, and it offers
none of this library's properties. Reported, not targeted.

## Measurement caveat

`BM_mt19937` recorded a 1.21% cycles/byte CV, above this project's sub-1% bar,
so `run_matrix.sh` exited non-zero. That row is flagged rather than dropped,
and it is not a new problem: `std::mt19937` has missed the bar on three of the
four matrix runs on this machine (0.94%, 0.78%, 1.73%, 1.21%), which
[`throughput-matrix-pi-2026-08-24.md`](throughput-matrix-pi-2026-08-24.md)
already attributes to its 2.5 KiB state and bursty refill against a 512 KiB L2
rather than to the harness.

Its median across those four runs spans 1.918 to 1.956, a 2% spread. The margin
being claimed here is 33%, so the ranking does not depend on the noisy row. The
`BM_vphilox_bulk` row that carries the actual result has a CV of 0.00%.
